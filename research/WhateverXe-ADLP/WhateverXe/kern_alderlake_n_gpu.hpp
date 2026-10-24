// Limited pre-native S1 BCS bring-up, derived from the Linux v6.12 LRC/ELSQ
// and MI command contracts. No donor ABI, display modeset or GT reset.
#ifndef kern_alderlake_n_gpu_hpp
#define kern_alderlake_n_gpu_hpp
#include "kern_alderlake_n.hpp"
namespace AlderLakeN {
constexpr uint32_t GPUBytes=16*4096, GPUPages=16, GPUBase=ScanoutSlot;
constexpr uint32_t GPUResultOffset=8*4096+0x100, GPUTail=32;
constexpr uint32_t GPUValue=0x4E393742, GPUToken=0xC001CAFE;
enum class GPUStatus : uint64_t {
    NotAdmitted, BadMemory, BusyGGTT, ForcewakeBusy, ForcewakeTimeout,
    EngineBusy, Mapped, Complete, TimedOut, ReleaseFailed, RestoreFailed
};
struct GPUResult {
    uint64_t version {1}, status {}, stage {}, retained {}, pteWrites {}, pteErrors {},
        apertureSamples {}, apertureErrors {}, fwBefore {}, fwHeld {}, fwAfter {}, fwReleased {},
        engineWrites {}, polls {}, descriptor {}, dmaFirst {}, dmaLast {}, result {}, token {},
        head {}, tail {}, miMode {}, elStatusLo {}, elStatusHi {}, csbPointer {}, csbWrite {},
        gucStatus {}, irqEnable {}, errors {}, ipeir {}, ipehr {}, faultAddress {};
    uint64_t csb[12] {};
};
static_assert(sizeof(GPUResult)==44*8,"AlderLakeN BCS property layout");

inline bool gpuImage(uint8_t *memory,const uint64_t *dma) {
    if(!memory || !dma)return false;
    for(unsigned p=0;p<GPUPages;++p)
        if(!dma[p] || (dma[p]&4095) || dma[p]>0x7FFFFFF000ULL)return false;
    for(unsigned i=0;i<GPUBytes;++i)memory[i]=0;
    auto hws=reinterpret_cast<uint32_t *>(memory);
    // Gen11+ CSB: 12 qwords at dword0x10, write pointer at dword0x2f.
    for(unsigned i=0;i<24;++i)hws[0x10+i]=UINT32_MAX;
    hws[0x2f]=11;
    auto regs=reinterpret_cast<uint32_t *>(memory+2*4096);
    // Gen12 XCS initial restore list, intel_lrc.c gen12_xcs_offsets.
    constexpr uint32_t first[] {0x244,0x034,0x030,0x038,0x03c,0x168,0x140,
        0x110,0x1c0,0x1c4,0x1c8,0x180,0x2b4};
    constexpr uint32_t second[] {0x3a8,0x28c,0x288,0x284,0x280,0x27c,0x278,0x274,0x270};
    regs[1]=(0x22U<<23)|0x80000|0x1000|25;
    for(unsigned i=0;i<13;++i)regs[2+2*i]=0x22000+first[i];
    regs[33]=(0x22U<<23)|0x80000|0x1000|17;
    for(unsigned i=0;i<9;++i)regs[34+2*i]=0x22000+second[i];
    regs[52]=(0x0AU<<23)|1; // End initial restore batch.
    regs[3]=0x00090009; // inhibit synchronous switch + first restore inhibit.
    regs[5]=0;regs[7]=GPUTail;regs[9]=GPUBase+3*4096;regs[11]=1; // 4KiB valid ring.
    regs[0x31]=uint32_t(dma[4]>>32);regs[0x33]=uint32_t(dma[4]); // owned PML4.
    regs[0x61]=0x01000000; // Context-saved MI_MODE: clear STOP_RING mask.
    // Owned scratch page table tree. Commands below select GGTT explicitly;
    // any unused low PPGTT address still resolves to our scratch, not firmware.
    for(unsigned level=4;level<7;++level)
        reinterpret_cast<uint64_t *>(memory+level*4096)[0]=dma[level+1]|3;
    for(unsigned i=0;i<512;++i)reinterpret_cast<uint64_t *>(memory+7*4096)[i]=dma[9]|3;
    auto ring=reinterpret_cast<uint32_t *>(memory+3*4096);
    ring[0]=(0x20U<<23)|(1U<<22)|2; // MI_STORE_DWORD_IMM, GGTT.
    ring[1]=GPUBase+GPUResultOffset;ring[2]=0;ring[3]=GPUValue;
    ring[4]=(0x26U<<23)|(1U<<14)|2; // MI_FLUSH_DW: ordered post-sync write.
    ring[5]=(GPUBase+GPUResultOffset+8)|4; // GGTT, qword aligned.
    ring[6]=0;ring[7]=GPUToken;
    return true;
}

// Caller has verified the complete firmware lease, native-start exclusion,
// exact kernel/PCI/GGC and wired MapperNone memory. On any engine write, all
// mapped pages stay pinned until reboot even if the token completes.
template<class IO> GPUResult testGPU(IO &io,uint8_t *memory,const uint64_t *dma,
                                     const uint64_t *predecessor,bool admitted) {
    GPUResult out;
    if(!admitted)return out;
    out.status=uint64_t(GPUStatus::BadMemory);
    if(!predecessor || !gpuImage(memory,dma))return out;
    out.dmaFirst=dma[0];out.dmaLast=dma[GPUPages-1];
    const uint32_t pte=0x800000+(GPUBase>>9);
    for(unsigned p=0;p<GPUPages;++p)if(io.readPTE(pte+8*p)!=predecessor[p]) {
        out.status=uint64_t(GPUStatus::BusyGGTT);return out;
    }
    out.stage=1;out.fwBefore=io.read(0xA188);
    uint64_t ack=io.read(0x130044);
    if(out.fwBefore==UINT32_MAX || ack==UINT32_MAX || ((out.fwBefore|ack)&1)) {
        out.status=uint64_t(GPUStatus::ForcewakeBusy);return out;
    }
    const auto waitFW=[&](bool held,uint64_t &last) {
        for(unsigned n=0;n<500;++n) {
            last=io.read(0x130044);
            if(last==UINT32_MAX)return false;
            if(bool(last&1)==held)return true;
            io.delay();
        }
        return false;
    };
    io.write(0xA188,0x10001);io.read(0xA188);
    bool mapped=false;
    do {
        out.status=uint64_t(GPUStatus::ForcewakeTimeout);
        if(!waitFW(true,out.fwHeld))break;
        out.stage=2;out.status=uint64_t(GPUStatus::EngineBusy);
        out.gucStatus=io.read(0xC000);out.irqEnable=io.read(0x190030);
        out.miMode=io.read(0x2209c);out.elStatusLo=io.read(0x22234);out.elStatusHi=io.read(0x22238);
        // No GuC uKernel and no render/copy interrupts owned by firmware.
        if((out.gucStatus&0xFF00) || out.gucStatus==UINT32_MAX || out.irqEnable ||
           out.miMode!=0x200 || out.elStatusLo!=1 || out.elStatusHi || io.read(0x2229c)!=0 ||
           io.read(0x22030) || io.read(0x22034) || io.read(0x22038) || io.read(0x2203c))break;
        // Refuse even an idle firmware queue with any valid descriptors.
        bool queueBusy=false;
        for(unsigned p=0;p<8;++p)if(io.read(0x22510+8*p)&1)queueBusy=true;
        if(queueBusy)break;
        io.flushMemory(memory,GPUBytes);
        out.stage=3;out.status=uint64_t(GPUStatus::Mapped);mapped=true;
        for(unsigned p=0;p<GPUPages;++p) {
            io.writePTE(pte+8*p,dma[p]|1);++out.pteWrites;
            if(io.readPTE(pte+8*p)!=(dma[p]|1))++out.pteErrors;
        }
        io.fence();if(out.pteErrors)break;
        for(unsigned p=0;p<GPUPages;++p)for(uint32_t offset=0;offset<=4092;offset+=4092) {
            ++out.apertureSamples;
            if(io.apertureRead(p*4096+offset)!=*reinterpret_cast<uint32_t *>(memory+p*4096+offset))++out.apertureErrors;
        }
        if(out.apertureErrors)break;
        out.stage=4;out.retained=1;
        const auto write=[&](uint32_t reg,uint32_t value) {io.write(reg,value);++out.engineWrites;};
        write(0x22098,UINT32_MAX); // HWSTAM, no ISR-copy writes.
        write(0x2229c,0x00080008); // Gen11+ disable legacy / enable execlists.
        write(0x2209c,0x01000000); // clear STOP_RING, preserve idle indication.
        write(0x22080,GPUBase);io.read(0x22080); // global HWSP.
        write(0x223a0,0xFFFF0B0B);io.read(0x223a0); // 12-entry CSB.
        // ELSQ slots persist across LOAD. Clear all 8, then publish one context.
        for(unsigned p=0;p<8;++p) {write(0x22510+8*p,0);write(0x22514+8*p,0);}
        out.descriptor=uint64_t(GPUBase+4096+0x319)|(1ULL<<37)|(1ULL<<61); // BCS class1, ID1.
        write(0x22510,uint32_t(out.descriptor));write(0x22514,uint32_t(out.descriptor>>32));
        io.fence();write(0x22550,1);io.read(0x22550);
        out.stage=5;out.status=uint64_t(GPUStatus::TimedOut);
        for(unsigned n=0;n<20000;++n) { // 2s bound; no GPU reset on timeout.
            io.flushMemory(memory+GPUResultOffset,64);
            out.result=*reinterpret_cast<volatile uint32_t *>(memory+GPUResultOffset);
            out.token=*reinterpret_cast<volatile uint32_t *>(memory+GPUResultOffset+8);
            out.head=io.read(0x22034);out.tail=io.read(0x22030);out.miMode=io.read(0x2209c);++out.polls;
            if(out.result==GPUValue && out.token==GPUToken && (out.head&0x1FFFFC)==GPUTail &&
               out.tail==GPUTail && (out.miMode&0x200)) {out.status=uint64_t(GPUStatus::Complete);break;}
            io.delay();
        }
        out.elStatusLo=io.read(0x22234);out.elStatusHi=io.read(0x22238);
        out.csbPointer=io.read(0x223a0);io.flushMemory(memory,4096);
        out.csbWrite=reinterpret_cast<volatile uint32_t *>(memory)[0x2f];
        for(unsigned i=0;i<12;++i)out.csb[i]=reinterpret_cast<volatile uint64_t *>(memory+0x40)[i];
        out.errors=io.read(0x220b0);out.ipeir=io.read(0x22064);out.ipehr=io.read(0x22068);
        out.faultAddress=uint64_t(io.read(0x22078))|(uint64_t(io.read(0x22060))<<32);
    } while(false);
    if(mapped && !out.retained) {
        for(unsigned p=0;p<GPUPages;++p) {io.writePTE(pte+8*p,predecessor[p]);++out.pteWrites;}
        io.fence();
        for(unsigned p=0;p<GPUPages;++p)if(io.readPTE(pte+8*p)!=predecessor[p]) {
            out.retained=1;out.status=uint64_t(GPUStatus::RestoreFailed);
        }
    }
    io.write(0xA188,0x10000);out.fwAfter=io.read(0xA188);
    out.fwReleased=waitFW(false,ack) && ((out.fwAfter&0xFFFF)==(out.fwBefore&0xFFFF));
    if(!out.fwReleased)out.status=uint64_t(GPUStatus::ReleaseFailed);
    return out;
}
}
#endif
