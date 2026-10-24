#ifndef KERN_XELP_FORCEWAKE_IRQ_HPP
#define KERN_XELP_FORCEWAKE_IRQ_HPP
#include <stdint.h>
namespace XELPForceWakeIRQ {
constexpr bool required(uint64_t flags,unsigned domains) {
    return domains && (flags&0x1800)==0x800;
}
// The exact native regular method holds a non-IRQ-safe spinlock across its
// refcount and multithreaded register/ACK transaction. Prevent same-CPU GT IRQ
// submit from recursively acquiring that lock. Preserve prior IF, including
// already-disabled interrupt callers, and every native count/client/ACK.
template<class Set,class Call> auto protect(Set set,Call call) {
    const auto prior=set(false);
    call();
    set(prior);
    return prior;
}
}
#endif
