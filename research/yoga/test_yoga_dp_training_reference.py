"""Compile pinned Linux helpers beside the local trainer; host only, no devices."""
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
REF = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'
PINS = {
    'display__intel_dp_link_training.c': '4bcc480e04e0aa202763d96a41bfc5e059a43d447dc3e3a3c15e12d55a8fa39f',
    'include__drm_dp.h': '70ea7e2c08c2155450f998d30d53e147070443168f616f06584b671e2b37dfd2',
    'display__drm_dp_helper.c': 'd3ca8955a877e78c63a426a3fa41851b860a65f93e2ed3b9e406882e941d7abf',
    'display__intel_ddi.c': '75403786004708ecb011a9be131c68b2a406b9e7e653c00a25d1ea1d8a475ac8',
}


def function(source, name):
    match = re.search(r'^(?:static )?(?:bool|int|u8)\s+' + re.escape(name) + r'\(', source, re.M)
    if not match:
        raise ValueError('Missing Linux function: ' + name)
    brace = source.index('{', match.start())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


class LinuxDpTrainingReferenceTests(unittest.TestCase):
    def test_compiled_linux_status_adjustment_and_delay(self):
        files = {}
        for name, digest in PINS.items():
            data = (REF / name).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), digest, name)
            files[name] = data.decode()
        # Select only the original macros used by these extracted functions.
        names = {'DP_LINK_STATUS_SIZE','DP_LANE0_1_STATUS','DP_LANE_ALIGN_STATUS_UPDATED',
                 'DP_ADJUST_REQUEST_LANE0_1','DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT',
                 'DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT','DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT',
                 'DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT','DP_TRAIN_VOLTAGE_SWING_SHIFT',
                 'DP_TRAIN_PRE_EMPHASIS_SHIFT','DP_CHANNEL_EQ_BITS','DP_INTERLANE_ALIGN_DONE',
                 'DP_LANE_CR_DONE','DP_LANE_CHANNEL_EQ_DONE','DP_LANE_SYMBOL_LOCKED',
                 'DP_TRAIN_PRE_EMPHASIS_MASK','DP_TRAIN_MAX_PRE_EMPHASIS_REACHED',
                 'DP_TRAIN_MAX_SWING_REACHED'}
        names.update('DP_TRAIN_PRE_EMPH_LEVEL_' + str(i) for i in range(4))
        names.update('DP_TRAIN_VOLTAGE_SWING_LEVEL_' + str(i) for i in range(4))
        defines = []
        lines = iter(files['include__drm_dp.h'].splitlines())
        for line in lines:
            match = re.match(r'^#\s*define\s+(DP_\w+)\s+', line)
            if match and match[1] in names:
                while line.endswith('\\'):
                    line += '\n' + next(lines)
                defines.append(line)
        self.assertEqual(len(defines), len(names))
        body = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <initializer_list>
#include "DpTrainingSequence.hpp"
typedef uint8_t u8;
#define USEC_PER_MSEC 1000
#define drm_dbg_kms(...) ((void)0)
struct drm_dp_aux { void *drm_dev; const char *name; };
#define min(a,b) ((a)<(b)?(a):(b))
'''
        body += '\n'.join(defines) + '\n'
        for name in ['dp_link_status', 'dp_get_lane_status', 'drm_dp_channel_eq_ok',
                     'drm_dp_clock_recovery_ok', 'drm_dp_get_adjust_request_voltage',
                     'drm_dp_get_adjust_request_pre_emphasis', '__8b10b_clock_recovery_delay_us',
                     '__8b10b_channel_eq_delay_us']:
            body += function(files['display__drm_dp_helper.c'], name)
        body += function(files['display__intel_dp_link_training.c'], 'dp_voltage_max')
        body += r'''
int main() {
    unsigned comparisons = 0;
    for (unsigned lanes : {1u,2u,4u}) for (unsigned packed=0; packed<65536; ++packed)
        for (unsigned align=0; align<2; ++align) {
            u8 status[6] = {u8(packed),u8(packed>>8),u8(align),0,0,0};
            assert(YogaTypec::dpClockRecoveryVerified(lanes,status,6) == drm_dp_clock_recovery_ok(status,lanes));
            assert(YogaTypec::dpSinkLinkVerified(lanes,status,6) == drm_dp_channel_eq_ok(status,lanes));
            comparisons += 2;
        }
    for (unsigned request=0; request<16; ++request) for (unsigned lane=0; lane<4; ++lane) {
        u8 status[6] = {};
        status[4+lane/2] = request << (4*(lane%2));
        u8 v=drm_dp_get_adjust_request_voltage(status,lane);
        u8 p=drm_dp_get_adjust_request_pre_emphasis(status,lane);
        if (p >= DP_TRAIN_PRE_EMPH_LEVEL_3) p=DP_TRAIN_PRE_EMPH_LEVEL_3|DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
        v=min(v,dp_voltage_max(p));
        if (v >= DP_TRAIN_VOLTAGE_SWING_LEVEL_3) v=DP_TRAIN_VOLTAGE_SWING_LEVEL_3|DP_TRAIN_MAX_SWING_REACHED;
        YogaTypec::DpDriveLevel level{};
        assert(YogaTypec::dpAdlpTrainingByte(request,level) == (v|p));
        ++comparisons;
    }
    for (unsigned interval=0; interval<=4; ++interval) {
        assert(__8b10b_clock_recovery_delay_us(nullptr,interval) == int(interval ? interval*4000 : 100));
        assert(__8b10b_channel_eq_delay_us(nullptr,interval) == int(interval ? interval*4000 : 400));
        comparisons += 2;
    }
    printf("PASS: %u compiled pinned Linux status/drive/delay comparisons\n",comparisons);
}
'''
        with tempfile.TemporaryDirectory(prefix='yoga-dp-reference-', dir='/tmp/opencode') as tmp:
            tmp = Path(tmp)
            (tmp / 'reference.cpp').write_text(body)
            compile_run = subprocess.run(['clang++', '-std=c++14', '-fsanitize=address,undefined',
                '-I', str(ROOT / 'yoga-typec'), str(tmp / 'reference.cpp'), '-o', str(tmp / 'reference')],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(compile_run.returncode, 0, compile_run.stderr)
            result = subprocess.run([str(tmp / 'reference')], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, '')
            self.assertIn('PASS: 786506 compiled pinned Linux', result.stdout)


if __name__ == '__main__':
    unittest.main()
