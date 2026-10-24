"""Offline direct lock/branch inventory. Does NOT claim whole-call exclusion."""
import hashlib
import json
import re
import subprocess
from pathlib import Path

from audit_cursor_diag_inputs import FB, image
from audit_typec_native_integration import require
from yoga_fb_match_patch import symbol

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/typec-lock-coverage-audit-20261024'
LOCK_IMPORTS = {'_IOLockLock', '_IOLockUnlock', '_IOLockTryLock',
                '_IORecursiveLockLock', '_IORecursiveLockUnlock', '_IORecursiveLockTryLock',
                '_IOSimpleLockLock', '_IOSimpleLockUnlock', '_IOSimpleLockTryLock',
                '_OSSpinLockLock', '_OSSpinLockUnlock', '_OSSpinLockTry'}


def inventory(text, relocations, at):
    instructions, owners, owner = {}, {}, None
    for line in text.splitlines():
        header = re.match(r'^([0-9a-f]+) <([^>]+)>:', line)
        if header:
            owner = header[2]
            owners[owner] = int(header[1], 16)
        insn = re.match(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )*[0-9a-f]{2})\s+(.+)$', line)
        if insn:
            instructions[int(insn[1], 16)] = (owner, bytes.fromhex(insn[2]), insn[3])
    locks = []
    for line in relocations.splitlines():
        columns = line.split()
        if len(columns) != 7 or columns[4] != 'BRANCH' or columns[6] not in LOCK_IMPORTS:
            continue
        # Mach-O x86 BRANCH relocation identifies disp32, NOT the opcode.
        relocation = int(columns[0], 16)
        address = relocation - 1
        require(address in instructions, f'Lock relocation not owned: {relocation:#x}')
        owner, raw, assembly = instructions[address]
        require(owner and len(raw) == 5 and raw[0] in (0xe8, 0xe9), 'Not CALL/JMP rel32')
        require(at(address, 5) == raw and raw[1:] == bytes(4), 'Unreviewed lock import bytes')
        locks.append(dict(instruction=hex(address), relocation=hex(relocation),
                          owner=owner, import_name=columns[6], opcode=hex(raw[0])))
    require(locks, 'No native lock imports enumerated')
    calls = []
    for address, (owner, raw, assembly) in instructions.items():
        if raw[0] in (0xe8, 0xe9) and len(raw) == 5 and raw[1:] != bytes(4):
            target = address + 5 + int.from_bytes(raw[1:], 'little', signed=True)
            calls.append(dict(instruction=hex(address), owner=owner, target=hex(target)))
    return owners, locks, calls


def audit():
    data, at = image(FB)
    require(hashlib.sha256(data).hexdigest() ==
            '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df', 'Wrong framebuffer')
    text = subprocess.check_output(['llvm-objdump','-d','--x86-asm-syntax=intel',str(FB)], text=True)
    relocations = subprocess.check_output(['llvm-objdump','--macho','--reloc',str(FB)], text=True)
    owners, locks, calls = inventory(text, relocations, at)
    targets = {
        '__ZN24AppleIntelBaseController5startEP9IOService': 0x5a8a0,
        '__ZN24AppleIntelBaseController4stopEP9IOService': 0x60f42,
        '__ZN24AppleIntelBaseController12enableClocksEP14AppleIntelPortS1_P11ClockParams': 0xa5dae,
        '__ZN24AppleIntelBaseController13disableClocksEP14AppleIntelPortS1_': 0xa6ed4,
        '__ZN24AppleIntelBaseController14doLinkTrainingEP18AGDCDPPortConfig_tS1_': 0xa2132,
        '__ZN17AppleIntelPortHAL15ReadDekelPhyRegEj': 0x90234,
        '__ZN17AppleIntelPortHAL16WriteDekelPhyRegEjj': 0x902ea,
        '__ZN17AppleIntelPortHAL18enableDklPhyClocksEv': 0x92608,
        '__ZN17AppleIntelPortHAL19disableDklPhyClocksEv': 0x9302c,
        '__ZN17AppleIntelPortHAL15setFIALaneCountEj': 0x8fdc4,
        '__ZN17AppleIntelPortHAL11setPortModeEN14AppleIntelPort8PortModeE': 0x8f344,
        '__ZN17AppleIntelPortHAL12setDklDPModeEv': 0x903a4,
        '__ZN22AppleIntelPortHALDiags25eyeTunerEnableTestPatternEv': 0xe01c4,
        '__ZN22AppleIntelPortHALDiags26eyeTunerDisableTestPatternEv': 0xe0854,
    }
    reviewed = {}
    for name, address in targets.items():
        require(symbol(data, name) == owners.get(name) == address, name)
        sites = [entry for entry in locks if entry['owner'] == name]
        require(not sites, 'Expected no direct lock imports in ' + name)
        reviewed[name] = dict(address=hex(address), direct_lock_imports=sites,
            direct_callers=[entry for entry in calls if entry['target'] == hex(address)])
    require(len(locks) == 277 and len({entry['owner'] for entry in locks}) == 105, 'Lock inventory changed')
    aux = '__ZN17AppleIntelPortHAL13runAUXCommandEN14AppleIntelPort11AUX_CommandEPjjb'
    aux_sites = [entry for entry in locks if entry['owner'] == aux]
    require([entry['instruction'] for entry in aux_sites] == ['0x91be2','0x92111'], 'AUX locks')
    diagnostic_calls = {0xe072e: 0xa5dae, 0xe090d: 0xa6ed4}
    for address, target in diagnostic_calls.items():
        raw = at(address, 5)
        require(raw[0] == 0xe8 and address + 5 + int.from_bytes(raw[1:], 'little', signed=True) == target,
                'Diagnostic clock caller changed')
    return dict(lock_sites=locks, lock_site_count=len(locks), lock_owner_count=105,
                reviewed_bodies=reviewed, aux_lock_sites=aux_sites,
                diagnostic_clock_callers={hex(a): hex(t) for a,t in diagnostic_calls.items()},
                whole_operation_exclusion_proven=False, native_backend_enabled=False,
                limitations=[
                    'Zero direct lock imports does not prove no caller-held lock.',
                    'Symbol ownership is bounded disassembly ownership, not a full control-flow proof.',
                    'Direct rel32 CALL/JMP inventory excludes unresolved imports, virtual calls, function pointers and other kexts.',
                    'No proof yet that start/stop/remap and all shared DKL/FIA/clock users participate in one enforceable exclusion protocol.',
                    'The AUX recursive lock cannot be assumed to cover unlocked lifecycle and shared-register bodies.',
                    'Eye-tuner diagnostics directly enter controller clock enable/disable too; normal modeset/AGDC-only admission misses these callers.',
                ], device_access=False, external_writes=False)


def main():
    report = audit()
    OUT.mkdir(exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ['lock_site_count','lock_owner_count',
        'whole_operation_exclusion_proven','native_backend_enabled']}, indent=2))


if __name__ == '__main__':
    main()
