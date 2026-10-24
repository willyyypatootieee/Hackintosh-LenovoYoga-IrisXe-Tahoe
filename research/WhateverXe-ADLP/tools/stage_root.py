#!/usr/bin/env python3
"""Stage the renamed provider against an already qualified, manifested root set."""
import argparse,hashlib,json,plistlib,shutil
from pathlib import Path
OLD='org.codex.TGLCompat';NEW='org.b00t0x.AppleIntelTGLCompat'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 baseline=json.loads((a.baseline/'manifest.json').read_text())
 for path,h in baseline['files'].items():assert sha(a.baseline/path)==h,path
 build=json.loads((a.build/'build-manifest.json').read_text())
 for n in ['WhateverXe','AppleIntelTGLCompat']:assert sha(a.build/(n+'.kext')/'Contents/MacOS'/n)==build[n]['sha256']
 assert not a.output.exists();a.output.mkdir(parents=True)
 for bundle in a.baseline.iterdir():
  if bundle.is_dir() and bundle.name not in ['TGLCompat.kext','AppleIntelTGLCompat.kext']:shutil.copytree(bundle,a.output/bundle.name,symlinks=True)
 provider=a.build/'AppleIntelTGLCompat.kext';info=plistlib.loads((provider/'Contents/Info.plist').read_bytes());assert info['CFBundleIdentifier']==NEW
 shutil.copytree(provider,a.output/provider.name,symlinks=True)
 modified=[]
 for f in a.output.glob('*/Contents/Info.plist'):
  d=plistlib.loads(f.read_bytes());libs=d.get('OSBundleLibraries',{})
  if OLD in libs or NEW in libs:
   libs.pop(OLD,None);libs[NEW]=info['CFBundleVersion'];f.write_bytes(plistlib.dumps(d));modified.append(str(f.relative_to(a.output)))
 files={str(p.relative_to(a.output)):sha(p) for p in sorted(a.output.rglob('*')) if p.is_file()}
 unchanged=[k for k in baseline['files'] if k in files and baseline['files'][k]==files[k]]
 for k in baseline['files']:
  if k.endswith('/Contents/MacOS/AppleIntelTGLGraphics') or k.endswith('/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer'):assert k in unchanged
 manifest={'scope':'WhateverXe extraction: provider update; unchanged Apple executables/userland','target_build':'20G1443','files':files,'provider':build['AppleIntelTGLCompat'],'plugin':build['WhateverXe'],'dependency_plists':modified,'unchanged_baseline_files':len(unchanged),'baseline_files':baseline['files'],'qualification':'candidate; live results recorded separately'}
 (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'files':len(files),'unchanged':len(unchanged),'dependency_plists':modified}))
if __name__=='__main__':main()
