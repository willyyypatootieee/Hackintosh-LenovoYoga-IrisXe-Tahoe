"""Compile native Type-C translation units against the local kernel SDK on Linux.

Produces object files only: no fake plugin entry, loadable kext, or deployment.
"""
from pathlib import Path
import datetime
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parent
DEPS = Path('/path/to/local-workspace/hackintosh-yoga/xe-lab/NootedGreen')


def build():
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    stage = ROOT / 'cross-build' / ('typec-native-objects-' + stamp)
    stage.mkdir(exist_ok=False)
    flags = ['-target','x86_64-apple-macos13','-mkernel','-nostdinc','-nostdinc++',
             '-fno-builtin','-fno-common','-fno-stack-protector','-fno-exceptions',
             '-fno-rtti','-fapple-kext','-std=gnu++17','-DKERNEL','-DKERNEL_PRIVATE',
             '-DDRIVER_PRIVATE','-DAPPLE','-DNeXT','-D__null_terminated=',
             '-D__stateful_pure=','-isystem',str(DEPS/'MacKernelSDK/Headers'),
             '-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
             '-Wno-inconsistent-missing-override','-Wno-unused-parameter','-O2','-g']
    sources = ['NativeIokitMapping.cpp','NativeIokitAux.cpp','NativeIokitDpcd.cpp']
    commands, objects = [], []
    for name in sources:
        source = ROOT / 'yoga-typec' / name
        output = stage / (source.stem + '.o')
        command = ['clang++',*flags,'-c',str(source),'-o',str(output)]
        commands.append(command)
        result = subprocess.run(command,capture_output=True,text=True,timeout=60)
        (stage/(source.stem+'.log')).write_text(result.stdout+result.stderr)
        (stage/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode:
            raise RuntimeError(result.stderr)
        undefined = subprocess.check_output(['llvm-nm','--undefined-only',str(output)],text=True)
        (stage/(source.stem+'.undefined.txt')).write_text(undefined)
        objects.append(dict(source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                            object=str(output),object_sha256=hashlib.sha256(output.read_bytes()).hexdigest()))
    report = dict(objects=objects,loadable_kext=False,native_hooks_registered=False,
                  runtime_validated=False,device_access=False,external_writes=False)
    (stage/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    print(json.dumps(build(),indent=2))
