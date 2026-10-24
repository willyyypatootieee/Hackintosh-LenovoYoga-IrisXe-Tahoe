"""Exact-binary specifications; importing this module never writes a disk."""
from pathlib import Path
import copy
import hashlib
import plistlib
import struct

ROOT = Path(__file__).resolve().parent
SNAP = ROOT / 'installed-external-readonly-20261023-231719/EFI'
EXPECTED = '351aff4ee7b3c2ddda914475951088be3b8e99becdb47f9e026a0cf808317d9f'
FB = ROOT / 'installed-signatures-readonly-20261023-233323/copies/data/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer'
NG = SNAP / 'OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen'
FB_HASH = '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df'
NG_HASH = 'e89bb36d1a23559b8d60194e79e620addf7b82f98e2e37d282bad0d82978dd44'
FB_BASE = '__ZN24AppleIntelBaseController13FBMemMgr_InitEv'
NG_BASE = '__ZN5Gen1128injectAcceleratorPersonalityEb'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def commands(data):
    assert data[:4] == b'\xcf\xfa\xed\xfe'
    pos = 32
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        cmd, size = struct.unpack_from('<II', data, pos)
        assert size >= 8 and pos + size <= len(data)
        yield cmd, pos, size
        pos += size


def symbol(data, name):
    pos = next(p for c, p, _ in commands(data) if c == 2)
    off, count, strings, _ = struct.unpack_from('<4I', data, pos + 8)
    values = []
    for i in range(count):
        idx, kind, section, desc, value = struct.unpack_from('<IBBHQ', data, off + i * 16)
        if kind & 0xe and data[strings + idx:data.index(b'\0', strings + idx)].decode() == name:
            values.append(value)
    assert len(set(values)) == 1, (name, values)
    return values[0]


def file_offset(data, address):
    for c, p, _ in commands(data):
        if c == 0x19:
            vm, size, off, fs = struct.unpack_from('<4Q', data, p + 24)
            if vm <= address < vm + fs:
                return off + address - vm
    raise ValueError(hex(address))


def spec(comment, identifier, base, limit, old, new):
    assert len(old) == len(new)
    return dict(Arch='x86_64', Base=base, Comment=comment, Count=1, Enabled=True,
                Find=old, Identifier=identifier, Limit=limit, Mask=b'',
                MaxKernel='25.99.99', MinKernel='25.0.0', Replace=new,
                ReplaceMask=b'', Skip=0)


PATCHES = [
    spec('Yoga FE GMS framebuffer: cursor aperture offset 4032MiB -> 60MiB',
         'com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer', FB_BASE, 192,
         bytes.fromhex('c1 e0 11 25 00 00 00 fe'), bytes.fromhex('b8 00 00 c0 03 90 90 90')),
    # IOPCIMatch is the existing PCI ID-list key short enough for this C string.
    # The following string is not moved; its original NUL terminator is retained.
    spec('Yoga IntelAccelerator matching: use PCI IDs, never all display adapters',
         'com.StezzaPilot.NootedGreen', '', 0x52000,
         b'IOPCIClassMatch\0', b'IOPCIMatch' + b'\0' * 6),
    # IOPCIFamily matches saved PCI config, not necessarily spoofed device-id.
    # Include physical 46A6 and configured 9A49; neither primary nor secondary
    # NVIDIA 25A9:10DE / 3B39:17AA can match this exact, unmasked list.
    spec('Yoga IntelAccelerator IDs: 46A6 Intel physical / 9A49 Intel spoof only',
         'com.StezzaPilot.NootedGreen', '', 0x52000,
         b'0x03000000&0xff000000\0', b'0x46a68086 0x9a498086\0'),
]


def apply_to_binary(data, patch):
    start = file_offset(data, symbol(data, patch['Base'])) if patch['Base'] else 0
    end = min(len(data), start + patch['Limit']) if patch['Limit'] else len(data)
    assert data.count(patch['Find']) == 1, patch['Comment']
    at = data.find(patch['Find'], start, end)
    assert at >= 0, patch['Comment']
    return data[:at] + patch['Replace'] + data[at + len(patch['Find']):], at


def build():
    original = (SNAP / 'OC/config.plist').read_bytes()
    assert sha(original) == EXPECTED
    config = plistlib.loads(original)
    assert any(p['Enabled'] and p['Base'] == '__ZN15IGMemoryManager12initSegmentsEv'
               for p in config['Kernel']['Patch']), 'Keep existing accelerator GMS fix'
    new = copy.deepcopy(config)
    assert not any(p['Comment'] in [q['Comment'] for q in PATCHES] for p in new['Kernel']['Patch'])
    new['Kernel']['Patch'].extend(copy.deepcopy(PATCHES))
    check = copy.deepcopy(new)
    del check['Kernel']['Patch'][-len(PATCHES):]
    assert check == config, 'Only three bounded kernel patches may change'
    fb, ng = FB.read_bytes(), NG.read_bytes()
    assert sha(fb) == FB_HASH and sha(ng) == NG_HASH
    fixed_fb, fb_at = apply_to_binary(fb, PATCHES[0])
    fixed_ng, key_at = apply_to_binary(ng, PATCHES[1])
    fixed_ng, ids_at = apply_to_binary(fixed_ng, PATCHES[2])
    assert fb_at == 0x5e227 and key_at == 0x42f3c and ids_at == 0x42ef3
    assert symbol(fb, FB_BASE) == 0x5e17a
    # Exact original LEA instructions in the executing injection function.
    assert ng[0x11f95:0x11f9c] == bytes.fromhex('48 8d 3d 57 0f 03 00')
    assert ng[0x1201f:0x12026] == bytes.fromhex('48 8d 35 16 0f 03 00')
    assert symbol(ng, NG_BASE) == 0x11e60
    assert len(fb) == len(fixed_fb) and len(ng) == len(fixed_ng)
    assert ((0xfec1 << 17) & 0xfe000000) == 4032 * 1024 * 1024
    assert ((0xfe & 15) + 1) * 4 * 1024 * 1024 == 60 * 1024 * 1024
    return original, plistlib.dumps(new, sort_keys=False), fb, fixed_fb, ng, fixed_ng
