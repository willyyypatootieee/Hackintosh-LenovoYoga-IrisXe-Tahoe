"""Exercise Apple's downloaded parser in userspace, not the running kernel.

The parser body is kept unmodified. Only kernel headers/entry points are
replaced by C standard headers and a mock PE_boot_args() input.
"""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
source = (ROOT / 'xnu-bootargs.c').read_text()
declarations_and_parser = source[source.index('typedef boolean_t'):source.index('#if defined(CONFIG_XNUPOST)\nkern_return_t\nparse_boot_arg_test')]
helpers = source[source.index('static boolean_t\nisargsep(char c)'):source.index('boolean_t\nPE_imgsrc_mount_supported')]
args = json.loads((ROOT / 'active-research-snapshot.json').read_text())['bootargs']
tests = [(token.split('=', 1)[0], args.index(token)) for token in args.split()]
prelude = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int boolean_t;
#define TRUE 1
#define FALSE 0
static char *input;
static char *PE_boot_args(void) { return input; }
'''
main = 'int main(void) {\ninput = ' + json.dumps(args) + ';\n'
for name, offset in tests:
    main += 'printf("%s %d %d\\n", ' + json.dumps(name) + ', ' + str(offset) + ', PE_parse_boot_argn(' + json.dumps(name) + ', NULL, 0));\n'
main += 'return 0;\n}\n'
with tempfile.TemporaryDirectory(prefix='bootarg-parser-', dir='/tmp/opencode') as tmp:
    cfile = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    cfile.write_text(prelude + declarations_and_parser + helpers + main)
    subprocess.run(['cc', '-O2', str(cfile), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
records = []
for line in result.stdout.splitlines():
    name, offset, found = line.split()
    records.append({'argument': name, 'byte_offset': int(offset), 'found': bool(int(found))})
assert all(r['found'] for r in records)
report = {
    'scope': 'Published Apple parser body compiled in userspace; not a live macOS test',
    'configured_length_bytes': len(args.encode()),
    'all_arguments_found': True,
    'arguments': records,
}
(ROOT / 'bootarg-parser-test.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
