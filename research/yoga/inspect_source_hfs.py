"""Read selected files from the source RAW, never any physical disk.

Outputs go to /tmp/opencode. The original image is opened read-only.
"""
from pathlib import Path
import struct
import json
import argparse

IMAGE = Path('/home/willy/Downloads/Olarila Tahoe 26/Olarila Tahoe 26.7.1.raw')
OFFSET = 209735680


class Volume:
    def __init__(self):
        self.file = IMAGE.open('rb')
        self.file.seek(OFFSET + 1024)
        header = self.file.read(512)
        assert header[:2] in (b'H+', b'HX')
        self.block = struct.unpack_from('>I', header, 40)[0]
        self.catalog = self.fork(header[272:352])

    @staticmethod
    def fork(data):
        return (struct.unpack_from('>Q', data)[0],
                [struct.unpack_from('>II', data, 16 + i * 8) for i in range(8)])

    def read(self, fork, offset, length):
        size, extents = fork
        length = min(length, size - offset)
        result = bytearray()
        for block, count in extents:
            span = count * self.block
            if offset >= span:
                offset -= span
                continue
            take = min(length - len(result), span - offset)
            self.file.seek(OFFSET + block * self.block + offset)
            result.extend(self.file.read(take))
            offset = 0
            if len(result) == length:
                break
        assert len(result) == length, 'Overflow extents not implemented'
        return bytes(result)

    def entries(self):
        head = self.read(self.catalog, 0, 64)
        node = struct.unpack_from('>I', head, 24)[0]
        node_size = struct.unpack_from('>H', head, 32)[0]
        entries = []
        visited = set()
        while node:
            assert node not in visited
            visited.add(node)
            data = self.read(self.catalog, node * node_size, node_size)
            node = struct.unpack_from('>I', data)[0]
            assert data[8] == 255
            count = struct.unpack_from('>H', data, 10)[0]
            for i in range(count):
                start = struct.unpack_from('>H', data, node_size - 2 * (i + 1))[0]
                keylen, parent, nchars = struct.unpack_from('>HIH', data, start)
                name = data[start + 8:start + 8 + nchars * 2].decode('utf-16-be')
                rec = start + 2 + keylen
                kind = struct.unpack_from('>H', data, rec)[0]
                if kind not in (1, 2):
                    continue
                ident = struct.unpack_from('>I', data, rec + 8)[0]
                e = dict(id=ident, parent=parent, name=name, folder=kind == 1)
                if kind == 2:
                    e['fork'] = self.fork(data[rec + 88:rec + 168])
                entries.append(e)
        lookup = {e['id']: e for e in entries}
        for e in entries:
            parts = [e['name']]
            parent = e['parent']
            while parent in lookup and parent != 1:
                p = lookup[parent]
                parts.append(p['name'])
                parent = p['parent']
            e['path'] = '/'.join(reversed(parts))
        return entries


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--extract', action='append', default=[])
    args = parser.parse_args()
    v = Volume()
    entries = v.entries()
    root = Path('/tmp/opencode/tahoe-installer-audit')
    root.mkdir(parents=True, exist_ok=True)
    (root / 'catalog.json').write_text(json.dumps(entries, indent=2))
    for e in entries:
        if e['name'] in args.extract and not e['folder']:
            out = root / e['name']
            assert not out.exists(), 'Do not overwrite previous extraction'
            with out.open('xb') as f:
                for offset in range(0, e['fork'][0], 8 * 1024 * 1024):
                    f.write(v.read(e['fork'], offset, 8 * 1024 * 1024))
            print(e['path'], '->', out, out.stat().st_size)
