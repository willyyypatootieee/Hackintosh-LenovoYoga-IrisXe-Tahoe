"""Pinned XNU cache-resolution evidence; does not inspect live PTEs or devices."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'cross-build/xnu-12377.1.9-mapping-reference-20261024/IOMemoryDescriptor.cpp'
DIGEST = 'c28e43b9e5eb713f0e4edb1d36e6861c39dbc2f1975813bb1a3745fe2076d171'


def audit():
    raw = SOURCE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != DIGEST:
        raise RuntimeError('Changed XNU reference')
    text = raw.decode()
    required = [
        'IOOptionBits mode = cacheModeForPagerFlags(IODefaultCacheBits(nextAddr));',
        '_flags |= (mode << kIOMemoryBufferCacheShift);',
        'else if (kIOMapDefaultCache == (options & kIOMapCacheMask)) {\n'
        '\t\t\t\tmapping->fOptions |= ((_flags & kIOMemoryBufferCacheMask) >> kIOMemoryBufferCacheShift);',
        'IOMemoryMap::getMapOptions()\n{\n\treturn fOptions;\n}',
    ]
    for fragment in required:
        if text.count(fragment) != 1:
            raise RuntimeError('Missing/ambiguous cache-resolution site: ' + fragment)
    # Do not turn constructor options0 into rejection or acceptance. This native
    # implementation gates the final map attributes; unresolved0 stays refused.
    implementation = (ROOT / 'yoga-typec/NativeIokitMapping.cpp').read_text()
    if 'const IOOptionBits options = mapping->getMapOptions();' not in implementation or \
       '(options & kIOMapCacheMask) != kIOMapInhibitCache' not in implementation:
        raise RuntimeError('Native resolved-UC gate changed')
    return dict(source_sha256=DIGEST,checked_sites=len(required),
                final_map_options_checked=True,unresolved_default_accepted=False,
                live_kernel_source_match_proven=False,runtime_validated=False,device_access=False)


if __name__ == '__main__':
    import json
    print(json.dumps(audit(),indent=2))
