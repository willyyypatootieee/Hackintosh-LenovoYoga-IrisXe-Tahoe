"""Host-only C++ contract test. Never accesses a disk device or EFI."""
import subprocess
import hashlib
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent


class TypecTransactionTests(unittest.TestCase):
    def run_sanitized(self, source, *args):
        with tempfile.TemporaryDirectory(prefix='yoga-typec-', dir='/tmp/opencode') as tmp:
            binary = Path(tmp) / 'transaction-test'
            subprocess.run([
                'clang++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                str(ROOT / 'yoga-typec' / source), '-o', str(binary)
            ], check=True, timeout=60, capture_output=True, text=True)
            run = subprocess.run([str(binary), *map(str, args)], check=True, timeout=30,
                                 capture_output=True, text=True)
            self.assertIn('PASS:', run.stdout)
            self.assertEqual(run.stderr, '')

    def test_sanitized_transaction_contract(self):
        self.run_sanitized('test_transaction.cpp')

    def test_sanitized_lane_plan(self):
        self.run_sanitized('test_lane_plan.cpp')

    def test_sanitized_native_aux_lease(self):
        self.run_sanitized('test_native_aux_lease.cpp')

    def test_sanitized_native_aux_command(self):
        self.run_sanitized('test_native_aux_command.cpp')

    def test_sanitized_native_aux_retry(self):
        self.run_sanitized('test_native_aux_retry.cpp')

    def test_native_aux_response_audit(self):
        from audit_typec_aux_response import audit
        report = audit()
        self.assertEqual(report['byte_checks'], 30)
        self.assertEqual(report['object_vptr_slot'], '0x188')
        self.assertEqual(report['receive_size_field']['mask'], '0x1f00000')
        self.assertFalse(report['original_validates_receive_size'])
        self.assertFalse(report['original_returns_actual_count'])
        self.assertFalse(report['original_clears_completion_after_transaction'])
        self.assertFalse(report['native_backend_enabled'])

    def test_sanitized_resumable_transaction(self):
        self.run_sanitized('test_resumable_transaction.cpp')

    def test_sanitized_dkl_access(self):
        self.run_sanitized('test_dkl_access.cpp')

    def test_sanitized_dkl_mode_plan(self):
        self.run_sanitized('test_dkl_mode_plan.cpp')

    def test_sanitized_dkl_signal_plan(self):
        self.run_sanitized('test_dkl_signal_plan.cpp')

    def test_sanitized_dkl_pll(self):
        self.run_sanitized('test_dkl_pll.cpp')

    def test_sanitized_dp_link(self):
        self.run_sanitized('test_dp_link.cpp')

    def test_sanitized_ddi_clock(self):
        self.run_sanitized('test_ddi_clock.cpp')

    def test_sanitized_native_clock_inputs(self):
        self.run_sanitized('test_native_clock_inputs.cpp')

    def test_sanitized_native_dssm_read(self):
        self.run_sanitized('test_native_dssm_read.cpp')

    def test_sanitized_dp_training(self):
        self.run_sanitized('test_dp_training.cpp')

    def test_native_training_pattern_audit(self):
        from audit_typec_training_pattern import audit
        report = audit()
        self.assertEqual(report['byte_checks'], 13)
        self.assertEqual(report['dpcd_to_native_argument']['0x00'], 5)
        self.assertEqual(report['dpcd_to_native_argument']['0x07'], 4)
        self.assertTrue(report['native_argument_zero_is_idle_not_disable'])
        self.assertFalse(report['native_success_has_readback'])

    def test_native_lock_coverage_audit(self):
        from audit_typec_lock_coverage import audit, inventory
        report = audit()
        self.assertEqual(report['lock_site_count'], 277)
        self.assertEqual(report['lock_owner_count'], 105)
        self.assertEqual(len(report['reviewed_bodies']), 14)
        self.assertEqual(report['diagnostic_clock_callers']['0xe072e'], '0xa5dae')
        self.assertFalse(report['whole_operation_exclusion_proven'])
        # A displacement relocation must bind to instruction-1, not an
        # unrelated instruction beginning at the displacement address.
        text = '0000000000000100 <f>:\n 100: e8 00 00 00 00 call 0x105\n'
        reloc = '00000101 True long True BRANCH False _IORecursiveLockLock'
        owners, locks, _ = inventory(text, reloc, lambda a,n: bytes.fromhex('e8 00 00 00 00'))
        self.assertEqual(locks[0]['instruction'], '0x100')
        with self.assertRaises(RuntimeError):
            inventory(text, reloc.replace('00000101', '00000102'), lambda a,n: bytes(n))

    def test_captured_yoga_vbt_clock_policy(self):
        vbt = ROOT / 'cursor-typec-firmware-readonly-20261024-021325/intel-display/i915_vbt'
        self.assertEqual(hashlib.sha256(vbt.read_bytes()).hexdigest(),
                         'cb9e14dbab2646ac0e2427206ec354bab966d8f321bc6dd0cb4109539907c687')
        self.run_sanitized('test_native_clock_inputs.cpp', vbt)

    def test_signal_tables_match_pinned_linux(self):
        refs = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'
        linux = (refs / 'display__intel_ddi_buf_trans.c').read_bytes()
        self.assertEqual(hashlib.sha256(linux).hexdigest(),
                         '4fe99978f5e41a06f09d1cf3d8f2a4809800de1869b1390e2cd2d7874c42ed0b')
        header = (ROOT / 'yoga-typec/AdlpDklSignalPlan.hpp').read_text()
        for local, upstream in [('adlpDpHbr', '_adlp_dkl_phy_trans_dp_hbr'),
                                ('adlpDpHbr2Hbr3', '_adlp_dkl_phy_trans_dp_hbr2_hbr3')]:
            block = re.search(re.escape(upstream) + r'\[\] = \{(.*?)\n\};', linux.decode(), re.S)[1]
            original = [tuple(int(x, 0) for x in row) for row in
                        re.findall(r'\.dkl = \{\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\s*\}', block)]
            block = re.search(re.escape(local) + r'\[10\] = \{(.*?)\n\};', header, re.S)[1]
            actual = [tuple(int(x.strip(), 0) for x in row.split(',')) for row in
                      re.findall(r'\{([^{}]+)\}', block)]
            self.assertEqual(len(original), 10)
            self.assertEqual(actual, original)

    def test_exact_native_dkl_audit(self):
        from audit_typec_dkl_access import audit, check_relative_branch, direct_edges
        report = audit()
        self.assertEqual(len(report['symbols']), 11)
        self.assertEqual(report['byte_checks'], 49)
        self.assertEqual(len(report['direct_dkl_calls']), 35)
        self.assertEqual(report['adlp_tc3_pll_enable']['address'], '0x46048')
        self.assertEqual(report['tc3_clock_mapping']['gate_bit'], 14)
        self.assertEqual(report['tc3_clock_mapping']['selector'], '0x46114')
        self.assertFalse(report['device_access'])
        self.assertFalse(report['runtime_validated'])
        self.assertEqual(direct_edges('00000001 <owner>:\n'
            '  2: e8 2d 02 09 00  call 0x90234 <read>\n'
            '  7: ff 90 50 01 00 00 call qword ptr [rax + 0x150]\n'),
            {2: (0x90234, 'owner')})
        for raw in [b'\x90\x00\x00\x00\x00', b'\xe8\x00\x00\x00\x00']:
            with self.assertRaises(RuntimeError):
                check_relative_branch(lambda _address, _length: raw, 0x100, 0x90234)

    def test_native_clock_input_audit(self):
        from audit_typec_clock_inputs import audit
        report = audit()
        self.assertEqual(report['captured_vbt_bdb_version'], 247)
        self.assertFalse(report['captured_vbt_afc_override'])
        self.assertEqual(report['byte_checks'], 15)
        self.assertEqual(report['register_manager']['bounds_failure_read_value'], 0)
        self.assertFalse(report['register_manager']['lock_covers_nonconflict_registers'])
        self.assertFalse(report['runtime_validated'])

    def test_shared_clock_gate_caller_coverage(self):
        from audit_typec_dkl_access import audit, shared_gate_users
        report = audit()
        users = report['shared_gate_immediate_users']
        self.assertEqual(len(users), 14)
        self.assertEqual(len(set(users.values())), 4)
        self.assertIn('BaseController', users['0xa61b1'])
        self.assertFalse(report['shared_gate_user_coverage_complete'])
        dispatch = report['controller_shared_gate_dispatch']
        self.assertEqual(dispatch['merged_enable_write'], '0xa69e1')
        self.assertEqual(dispatch['manager_write_target'], '0x8dce6')
        self.assertFalse(dispatch['native_whole_rmw_exclusion_proven'])
        with self.assertRaises((ValueError, AssertionError, RuntimeError)):
            shared_gate_users('', lambda _address, _length: bytes.fromhex('be 80 42 16 00'))

    def test_native_mmio_mapping_audit(self):
        from audit_typec_mmio_mapping import audit
        report = audit()
        self.assertEqual(report['byte_checks'], 10)
        self.assertEqual(report['controller_fields']['mapping'], '0xc70')
        self.assertEqual(report['manager_fields']['virtual_address'], '0x50')
        self.assertEqual(report['manager_init_vptr_slot'], '0x118')
        self.assertEqual(report['stop_map_clear'], '0x61a6a')
        self.assertFalse(report['runtime_validated'])


if __name__ == '__main__':
    unittest.main()
