#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "kern_tgl_gmbus.hpp"
struct IO {
    unsigned time {},reads {},readyAt {},errorAt {};
    bool unavailable {};
    void delayUs(unsigned us) {assert(us==10);time+=us;}
    uint32_t readStatus() {++reads;return unavailable ? UINT32_MAX :
        errorAt && time>=errorAt ? 0x8600 : readyAt && time>=readyAt ? 0x8A00 : 0x8200;}
};
int main() {
    {IO io;io.readyAt=140;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8A00 && r.elapsedUs==140 && r.reads==14 && io.reads==14);}
    {IO io;io.readyAt=50000;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8A00 && r.elapsedUs==50000 && r.reads==5000);}
    {IO io;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8200 && r.elapsedUs==50000 && r.reads==5000);}
    {IO io;io.errorAt=90;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8600 && r.elapsedUs==90 && r.reads==9);}
    {IO io;io.unavailable=true;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==UINT32_MAX && r.elapsedUs==10 && r.reads==1);}
    for(auto initial:{0x8800U,0x8600U,UINT32_MAX}) {
     IO io;auto r=Display13GMBus::waitReady(io,initial);assert(r.value==initial && !r.elapsedUs && !r.reads && !io.reads);}
    puts("PASS real GMBUS HW_RDY/NAK polling: delayed readiness, exact deadline, finite timeout, error/unavailable and already-ready fast paths.");
}
