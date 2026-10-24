#include "NativeAuxRetry.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
using namespace YogaTypec;
struct Scheduler {
    uint64_t time=10;
    bool valid=true, lock=false, pauseOk=true, invalidate=false, regress=false;
    unsigned pauses=0, checks=0;
    uint64_t nowMicros() { assert(!lock); return time; }
    bool current(Route r,uint64_t e) { assert(!lock && r.tcIndex==2 && e==9); ++checks; return valid; }
    bool pause(uint64_t us) {
        assert(!lock && us==1000); ++pauses;
        time=regress ? 0 : time+us;
        if (invalidate) valid=false;
        return pauseOk;
    }
};
struct Command {
    Scheduler &s;
    NativeAuxResult result=NativeAuxResult::Defer;
    unsigned calls=0, failFor=1;
    uint64_t duration=0;
    bool loseIdentity=false;
    NativeAuxReply attempt(uint8_t *out,size_t count) {
        assert(!s.lock); s.lock=true; ++calls;
        if (out) memset(out,0x77,count);
        s.time+=duration;
        if (loseIdentity) s.valid=false;
        s.lock=false;
        return calls<=failFor ? NativeAuxReply{result,result==NativeAuxResult::Short ? count-1 : 0} :
                              NativeAuxReply{NativeAuxResult::Ok,count};
    }
    NativeAuxReply read(Route,uint64_t,uint32_t,uint8_t *out,size_t n) { return attempt(out,n); }
    NativeAuxReply write(Route,uint64_t,uint32_t,const uint8_t *,size_t n) { return attempt(nullptr,n); }
};
int main() {
    uint8_t data[6];
    for (unsigned kind=0;kind<=unsigned(NativeAuxResult::NativeError);++kind) for (bool write : {false,true}) {
        Scheduler s; Command c{s}; c.result=NativeAuxResult(kind);
        // Command must never return Ok with0actual in real transport.
        if (c.result==NativeAuxResult::Ok) continue;
        NativeAuxRetry<Command,Scheduler> retry(c,s); memset(data,0xcc,sizeof(data));
        const auto r=write ? retry.write({2},9,0x102,data,6) : retry.read({2},9,0x202,data,6);
        const bool again=c.result==NativeAuxResult::Defer || (!write &&
            (c.result==NativeAuxResult::Busy || c.result==NativeAuxResult::Timeout || c.result==NativeAuxResult::ReceiveError));
        assert(c.calls==(again ? 2u : 1u) && s.pauses==unsigned(again));
        assert(r.result==(again ? NativeAuxResult::Ok : c.result));
        assert(retry.writeIntentObserved()==write);
        if (!write) for (auto byte : data) assert(byte==(again ? 0x77 : 0xcc));
    }
    for (unsigned failures=0;failures<9;++failures) {
        Scheduler s; Command c{s}; c.failFor=failures;
        NativeAuxRetry<Command,Scheduler> retry(c,s);
        const auto r=retry.read({2},9,0x202,data,6);
        assert(c.calls==(failures<7 ? failures+1 : 7) && s.pauses==(failures<7 ? failures : 6));
        assert(r.result==(failures<7 ? NativeAuxResult::Ok : NativeAuxResult::Defer));
    }
    for (unsigned failure=0;failure<7;++failure) {
        Scheduler s; Command c{s}; NativeAuxRetry<Command,Scheduler> retry(c,s);
        if (failure==0) s.pauseOk=false;
        if (failure==1) s.invalidate=true;
        if (failure==2) s.regress=true;
        if (failure==3) c.duration=40000;
        if (failure==4) c.loseIdentity=true;
        if (failure==5) s.valid=false;
        if (failure==6) c.duration=39000;
        memset(data,0xcc,sizeof(data));
        const auto r=retry.read({2},9,0x202,data,6);
        assert(r.result!=NativeAuxResult::Ok && c.calls<2);
        for (auto byte : data) assert(byte==0xcc);
    }
    {
        Scheduler s; Command c{s}; NativeAuxRetry<Command,Scheduler> retry(c,s);
        assert(retry.read({1},9,0,data,6).result==NativeAuxResult::InvalidInput);
        assert(retry.write({2},0,0,data,6).result==NativeAuxResult::InvalidInput);
        assert(retry.read({2},9,0xfffff,data,6).result==NativeAuxResult::InvalidInput);
        assert(retry.read({2},9,0,nullptr,6).result==NativeAuxResult::InvalidInput);
        assert(retry.write({2},9,0,nullptr,6).result==NativeAuxResult::InvalidInput);
        assert(!c.calls && !s.checks && !retry.writeIntentObserved());
    }
    puts("PASS: bounded AUX retries, unchanged failure output, uncertain-write refusal and unlocked waits");
}
