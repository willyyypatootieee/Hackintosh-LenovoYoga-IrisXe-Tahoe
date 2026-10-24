"""Differential test using compiled functions extracted from pinned Linux.

Runs entirely on the Linux host. No kernel modules, disks or device accesses.
"""
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
REF = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'


def function(source, name):
    start = source.index('static int ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def macros(source):
    # Only arithmetic field macros needed by the extracted calculation. No
    # hardware operations are linked. Keep original macro bodies verbatim.
    result = []
    lines = iter(source.splitlines())
    for line in lines:
        if re.match(r'#define\s+(?:DKL_|MG_)', line):
            block = line
            while block.endswith('\\'):
                block += '\n' + next(lines)
            result.append(block)
    return '\n'.join(result)


class LinuxPllReferenceTests(unittest.TestCase):
    def test_compiled_linux_calculation_matches_local_plans(self):
        source = (REF / 'display__intel_dpll_mgr.c').read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(),
                         '80efd229c1d29cfbdd62e2b039e3181c6021d040922b19bc9b041010df18b749')
        dkl = (REF / 'display__intel_dkl_phy_regs.h').read_text()
        mg = (REF / 'display__intel_mg_phy_regs.h').read_text()
        stub = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define REG_BIT(n) (1u << (n))
#define REG_GENMASK(h,l) ((~0u >> (31-(h))) & (~0u << (l)))
#define REG_FIELD_PREP(mask,val) (((u32)(val) << __builtin_ctz(mask)) & (mask))
#define MISSING_CASE(x) abort()
#define fallthrough __attribute__((fallthrough))
#define EINVAL 22
#define do_div(n,d) ((n) /= (d))
#define mul_u32_u32(a,b) ((u64)(a)*(b))
#define DIV_ROUND_UP_ULL(n,d) (((n)+(d)-1)/(d))
#define INTEL_OUTPUT_HDMI 1
#define DISPLAY_VER(i915) 12
#define to_i915(device) (device)
#define intel_crtc_has_type(state,type) 0
struct icl_dpll_hw_state {
 u32 mg_refclkin_ctl, mg_clktop2_coreclkctl1, mg_clktop2_hsclkctl;
 u32 mg_pll_div0, mg_pll_div1, mg_pll_ssc, mg_pll_bias, mg_pll_tdc_coldst_bias;
 u32 mg_pll_lf, mg_pll_frac_lock, mg_pll_bias_mask, mg_pll_tdc_coldst_bias_mask;
};
struct intel_dpll_hw_state { struct icl_dpll_hw_state icl; };
struct drm_i915_private { struct {
 struct { struct { int nssc; } ref_clks; } dpll;
 struct { int override_afc_startup; u8 override_afc_startup_val; } vbt;
} display; };
struct crtc { struct drm_i915_private *dev; };
struct intel_crtc_state { struct { struct crtc *crtc; } uapi; int port_clock; };
'''
        main = r'''
int main(void) {
 unsigned rates[] = {162000,270000,540000,810000}, refs[] = {19200,24000,38400};
 for (unsigned r=0;r<4;r++) for(unsigned f=0;f<3;f++) for(unsigned a=0;a<=8;a++) {
  struct drm_i915_private dev = {0}; struct crtc c = {&dev};
  struct intel_crtc_state s = {{&c},rates[r]}; struct intel_dpll_hw_state out = {0};
  dev.display.dpll.ref_clks.nssc=refs[f];
  dev.display.vbt.override_afc_startup=!!a; dev.display.vbt.override_afc_startup_val=a ? a-1 : 0;
  if (icl_calc_mg_pll_state(&s,&out)) return 1;
  const struct icl_dpll_hw_state *p=&out.icl;
  unsigned h=(p->mg_clktop2_hsclkctl>>12)&3, divs[]={2,3,5,7};
  printf("%u %u %u %u %u",rates[r],refs[f],a,divs[h],(p->mg_clktop2_hsclkctl>>8)&15);
  unsigned offsets[]={0x212c,0x20d8,0x20d4,0x2200,0x2204,0x2210,0x2214,0x2218};
  unsigned masks[]={MG_REFCLKIN_CTL_OD_2_MUX_MASK,MG_CLKTOP2_CORECLKCTL1_A_DIVRATIO_MASK,
   MG_CLKTOP2_HSCLKCTL_TLINEDRV_CLKSEL_MASK|MG_CLKTOP2_HSCLKCTL_CORE_INPUTSEL_MASK|
   MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_MASK|MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO_MASK,
   DKL_PLL_DIV0_MASK|(a ? DKL_PLL_DIV0_AFC_STARTUP_MASK : 0),
   DKL_PLL_DIV1_IREF_TRIM_MASK|DKL_PLL_DIV1_TDC_TARGET_CNT_MASK,
   DKL_PLL_SSC_IREF_NDIV_RATIO_MASK|DKL_PLL_SSC_STEP_LEN_MASK|DKL_PLL_SSC_STEP_NUM_MASK|DKL_PLL_SSC_EN,
   DKL_PLL_BIAS_FRAC_EN_H|DKL_PLL_BIAS_FBDIV_FRAC_MASK,
   DKL_PLL_TDC_SSC_STEP_SIZE_MASK|DKL_PLL_TDC_FEED_FWD_GAIN_MASK};
  unsigned vals[]={p->mg_refclkin_ctl,p->mg_clktop2_coreclkctl1,p->mg_clktop2_hsclkctl,
   p->mg_pll_div0,p->mg_pll_div1,p->mg_pll_ssc,p->mg_pll_bias,p->mg_pll_tdc_coldst_bias};
  for(unsigned i=0;i<8;i++) printf(" %u %u %u",offsets[i],masks[i],vals[i]);
  putchar('\n');
 }
 return 0;
}
'''
        body = stub + macros(mg) + '\n' + macros(dkl) + '\n'
        body += function(source.decode(), 'icl_mg_pll_find_divisors') + '\n'
        body += function(source.decode(), 'icl_calc_mg_pll_state') + '\n' + main
        with tempfile.TemporaryDirectory(prefix='yoga-pll-reference-', dir='/tmp/opencode') as tmp:
            tmp = Path(tmp)
            (tmp / 'reference.c').write_text(body)
            # Kernel masks use signed literal shifts (e.g. 7<<29); Linux builds
            # with wrapping semantics. Match that instead of reporting mask UB.
            subprocess.run(['clang', '-std=gnu11', '-fwrapv', '-fsanitize=address,undefined',
                            '-Wno-unused-value', str(tmp / 'reference.c'), '-o', str(tmp / 'reference')],
                           check=True, capture_output=True, text=True, timeout=60)
            subprocess.run(['clang++', '-std=c++14', '-fsanitize=address,undefined',
                            str(ROOT / 'yoga-typec/test_dkl_pll.cpp'), '-o', str(tmp / 'local')],
                           check=True, capture_output=True, text=True, timeout=60)
            original = subprocess.run([str(tmp / 'reference')], capture_output=True, text=True, check=True, timeout=30)
            local = subprocess.run([str(tmp / 'local'), 'plans'], capture_output=True, text=True, check=True, timeout=30)
            self.assertEqual(original.stderr, '')
            self.assertEqual(local.stderr, '')
            self.assertEqual(len(original.stdout.splitlines()), 108)
            self.assertEqual(local.stdout, original.stdout)


if __name__ == '__main__':
    unittest.main()
