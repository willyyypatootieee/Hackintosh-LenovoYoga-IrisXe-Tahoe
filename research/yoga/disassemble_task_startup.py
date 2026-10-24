"""Read-only exact-binary task startup analysis; output local evidence only."""
from pathlib import Path
import json
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/task-startup-analysis'
OUT.mkdir(exist_ok=True)

def commands(data, base=0):
    assert struct.unpack_from('<I', data, base)[0] == 0xfeedfacf
    p = base + 32
    for _ in range(struct.unpack_from('<I', data, base+16)[0]):
        cmd, size = struct.unpack_from('<II', data, p)
        yield cmd, p, size
        p += size

def symbols(data, base=0):
    p = next(p for c,p,s in commands(data,base) if c == 2)
    off, count, strings, _ = struct.unpack_from('<4I', data, p+8)
    result = {}
    for i in range(count):
        idx, kind, sec, desc, value = struct.unpack_from('<IBBHQ',data,off+i*16)
        if kind & 0xe:
            name = data[strings+idx:data.index(b'\0',strings+idx)].decode()
            result[name] = value
    return result

def offset(data, base, address):
    for c,p,s in commands(data,base):
        if c == 0x19:
            vm, size, off, fs = struct.unpack_from('<4Q',data,p+24)
            if vm <= address < vm+fs:
                return off+address-vm
    raise ValueError(hex(address))

def disasm(data,base,syms,name,length,label):
    start = syms[name]
    end = min([v for v in syms.values() if v>start] or [start+length])
    length = min(length,end-start)
    off = offset(data,base,start)
    binary = OUT / (label+'.bin')
    binary.write_bytes(data[off:off+length])
    text = subprocess.check_output(['objdump','-D','-b','binary','-m','i386:x86-64',
                                   '--adjust-vma='+hex(start),str(binary)],text=True)
    (OUT/(label+'.txt')).write_text(name+'\n'+text)

kc = ROOT / 'kernel-evidence-readonly-20261023-221944/copies/system/System/Library/KernelCollections/SystemKernelExtensions.kc'
data = kc.read_bytes()
base = next(struct.unpack_from('<Q',data,p+16)[0] for c,p,s in commands(data)
            if c == 0x80000035 and data[p+struct.unpack_from('<I',data,p+24)[0]:p+s].split(b'\0')[0] == b'com.apple.iokit.IOAcceleratorFamily2')
syms = symbols(data,base)
for name,label in [('__ZNK11IOAccelTask7releaseEv','parent-release'),
                   ('__ZN11IOAccelTask4initEP22IOGraphicsAccelerator2jPP16IORangeAllocator','parent-init'),
                   ('__ZN11IOAccelTask4freeEv','parent-free')]:
    disasm(data,base,syms,name,4096,label)

p = ROOT/'cross-build/gpu-stack-assembled/Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics'
data = p.read_bytes()
syms = symbols(data)
(OUT/'tgl-task-symbols.json').write_text(json.dumps({k:hex(v) for k,v in syms.items() if 'IGAccelTask' in k},indent=2))
for name in syms:
    if name.startswith(('__ZN11IGAccelTask','__ZNK11IGAccelTask')) and any(s in name for s in ['withOptions','initWithOptions','initAllocators','4init','release','4free','C1E','C2E']):
        disasm(data,0,syms,name,8192,'tgl-'+name)
for name in ['__ZN15IGMemoryManager31newAddressRangeAllocatorForTaskEP11IGAccelTaskj',
             '__ZN21IntelTGLMemoryManager31newUnifiedAddressRangeAllocatorEP11IGAccelTask',
             '__ZN15IGMemoryManager31newUnifiedAddressRangeAllocatorEP11IGAccelTask']:
    disasm(data,0,syms,name,8192,'tgl-'+name)
print(str(OUT))
