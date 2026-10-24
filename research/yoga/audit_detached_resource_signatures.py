"""Record the specific detached resource seal mismatch in package v1."""
from pathlib import Path
import hashlib
import json
import struct

ROOT=Path(__file__).resolve().parent
STACK=ROOT/'cross-build/gpu-stack-assembled'
results=[]
for rel in ['Library/Extensions/AppleIntelGraphicsShared.bundle',
            'Library/GPUBundles/AppleIntelGraphicsShared.bundle']:
    contents=STACK/rel/'Contents'
    directory=(contents/'_CodeSignature/CodeDirectory').read_bytes()
    magic,length,version,flags,hash_offset,ident,special,slots,limit=struct.unpack_from('>9I',directory)
    hash_size,hash_type,_,_=struct.unpack_from('>4B',directory,36)
    assert magic==0xfade0c02 and special>=3
    algorithm={1:'sha1',2:'sha256'}[hash_type]
    expected=directory[hash_offset-3*hash_size:hash_offset-2*hash_size]
    actual=hashlib.new(algorithm,(contents/'_CodeSignature/CodeResources').read_bytes()).digest()[:hash_size]
    results.append(dict(bundle=rel,algorithm=algorithm,recorded_resource_hash=expected.hex(),
                        actual_resource_hash=actual.hex(),matches=expected==actual,
                        has_main_executable=False))
assert all(not x['matches'] for x in results)
report=dict(finding='Detached CodeDirectory resource special slot does not match current CodeResources',
            user_error='invalid resource directory (directory or signature have been modified)',
            installed_files_replaced=False,results=results,
            remedy='Native ad-hoc re-sign and strict verification of temporary copies before installation',
            native_remedy_runtime_tested=False)
(ROOT/'cross-build/detached-resource-signature-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
