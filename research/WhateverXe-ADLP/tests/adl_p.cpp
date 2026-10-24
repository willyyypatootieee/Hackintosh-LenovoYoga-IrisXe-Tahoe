#include "kern_alderlake_p.hpp"
#include <cassert>
#include <cstdio>
int main() {
    using namespace AlderLakeP;
    Resources r;
    assert(matches(Identity,RevisionD0));
    assert(!matches(Identity,0) && !matches(Identity,0x0d));
    assert(!matches(0x46A88086,RevisionD0));
    assert(resources(1,0x3f,0,0xf0,r));
    assert(r.slices==1 && r.dss==6 && r.nativeSubslices==12 && r.eus==96 && r.l3Banks==8 && r.urbKiB==512);
    const uint32_t invalidDSS[] {0U,1U,0x1fU,0x7fU};
    for(const auto dss : invalidDSS) assert(!resources(1,dss,0,0xf0,r));
    assert(!resources(1,0x3f,1,0xf0,r));
    assert(!resources(1,0x3f,0,0,r));
    assert(!resources(0,0x3f,0,0xf0,r));
    std::puts("ADL-P 46A6 D0 six-DSS/96-EU GT2 profile and revision guard passed");
}
