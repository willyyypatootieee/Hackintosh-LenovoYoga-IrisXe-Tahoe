// ADL-N fixed HDMI uses the shared Combo-only board property.
// Native nine-port identity and three-pipe resources are managed separately.
// TC HDMI has been deferred; fixed DP requires its own transport policy.
#ifndef KERN_DISPLAY13_HDMI_WIRING_HPP
#define KERN_DISPLAY13_HDMI_WIRING_HPP
#include "kern_tgl_hdmi_wiring.hpp"
using Display13HDMIWiring = TGLHDMIWiring;
#endif
