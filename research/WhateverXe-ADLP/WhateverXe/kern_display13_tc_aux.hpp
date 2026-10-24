// Display13 legacy TC AUX preparation and Dekel uC-health transaction.
// Linux v6.12 intel_dp_aux_regs.h/intel_dkl_phy_regs.h/power_well.c.
#ifndef KERN_DISPLAY13_TC_AUX_HPP
#define KERN_DISPLAY13_TC_AUX_HPP
#include "kern_display13_tc.hpp"
namespace Display13TC {
struct Aux {
    static constexpr uint32_t TBT=1U<<11,Busy=1U<<31;
    static constexpr uint32_t W1C=(1U<<30)|(1U<<28)|(1U<<25);
    static bool control(unsigned tc,uint32_t &reg) {
        if(tc>=4)return false;reg=0x64310+0x100*tc;return true;
    }
    template<class IO> static Result prepare(IO &io,unsigned tc) {
        Result r;uint32_t reg=0;if(!control(tc,reg) || !io.admitted(tc))return r;
        if(!io.lanesPowered()){r.status=Status::NoPower;return r;}
        if(!io.readable(reg)){r.status=Status::Unavailable;return r;}
        r.before=io.read(reg);
        if(r.before==UINT32_MAX){r.status=Status::Unavailable;return r;}
        if(r.before&Busy){r.status=Status::InUse;return r;}
        if(r.before&TBT) {
            // Do not acknowledge pending DONE/error status or launch an AUX
            // command when clearing the route selector. Other control stays.
            io.write(reg,r.before&~(TBT|W1C));++r.writes;
        }
        r.after=io.read(reg);
        if(r.after==UINT32_MAX){r.status=Status::Unavailable;return r;}
        if(r.after&Busy){r.status=Status::InUse;return r;}
        const auto ordinary=~(W1C|Busy);
        r.status=!(r.after&TBT) && (r.before&ordinary&~TBT)==(r.after&ordinary) ? Status::Complete : Status::Readback;
        return r;
    }
    template<class IO> static Result health(IO &io,unsigned tc,bool &healthy) {
        healthy=false;Result r;Port p;
        if(tc>=4 || !io.admitted(tc) || !Port::resolve(tc+3,p))return r;
        if(!io.lanesPowered() || !io.auxPowered(tc)){r.status=Status::NoPower;return r;}
        const auto reg=p.phyRegister(0x236C);
        if(!io.readable(p.hip) || !io.readable(reg)){r.status=Status::Unavailable;return r;}
        const auto irq=io.lock();const auto previous=io.read(p.hip);
        bool changed=false;
        if(previous==UINT32_MAX)r.status=Status::Unavailable;
        else {
            const auto selected=p.bank(previous,0x236C);
            if(selected!=previous){io.write(p.hip,selected);++r.writes;changed=true;}
            if(io.read(p.hip)!=selected)r.status=Status::Readback;
            else {
                r.before=r.after=io.read(reg);
                r.status=r.before==UINT32_MAX ? Status::Unavailable : Status::Complete;
                healthy=r.status==Status::Complete && (r.before&(1U<<15));
            }
        }
        if(changed) {
            io.write(p.hip,previous);++r.writes;
            if(io.read(p.hip)!=previous){r.status=Status::Readback;io.bankFault();}
        }
        io.unlock(irq);
        if(r.status!=Status::Complete)healthy=false;
        return r;
    }
};
}
#endif
