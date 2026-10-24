"""Expose update ZIP inside source SharedSupport without copying 18 GB."""
from pathlib import Path
import errno
import os
import plistlib
import stat
import struct
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'cross-build/python-deps'))
from fuse import FUSE, Operations, FuseOSError
import inspect_source_hfs as hfs


class CopyDMG:
    def __init__(self, path):
        self.file = path.open('rb')
        self.file.seek(-512, 2)
        trailer = self.file.read()
        assert trailer[:4] == b'koly'
        self.data_offset = struct.unpack_from('>Q', trailer, 24)[0]
        xml_offset, xml_size = struct.unpack_from('>QQ', trailer, 216)
        self.file.seek(xml_offset)
        plist = plistlib.loads(self.file.read(xml_size))
        table = next(x['Data'] for x in plist['resource-fork']['blkx'] if 'Apple_HFS' in x['Name'])
        self.runs = []
        for i in range(struct.unpack_from('>I', table, 200)[0]):
            kind, _, sector, count, offset, size = struct.unpack_from('>IIQQQQ', table, 204 + i * 40)
            if kind == 0xffffffff:
                break
            assert kind in (0, 1, 2), 'Compressed run unsupported'
            self.runs.append((sector * 512, count * 512, kind, offset, size))
        self.position = 0

    def seek(self, offset):
        self.position = offset

    def read(self, length):
        result = bytearray()
        while len(result) < length:
            pos = self.position
            run = next(x for x in self.runs if x[0] <= pos < x[0] + x[1])
            start, size, kind, offset, packed = run
            take = min(length - len(result), start + size - pos)
            if kind == 1:
                self.file.seek(self.data_offset + offset + pos - start)
                data = self.file.read(take)
                assert len(data) == take
                result.extend(data)
            else:
                result.extend(bytes(take))
            self.position += take
        return bytes(result)


class NestedVolume(hfs.Volume):
    def __init__(self):
        self.file = CopyDMG(ROOT / 'cross-build/source-sharedsupport-ro/SharedSupport.dmg')
        self.file.seek(1024)
        header = self.file.read(512)
        assert header[:2] in (b'H+', b'HX')
        self.block = struct.unpack_from('>I', header, 40)[0]
        self.catalog = self.fork(header[272:352])


class UpdateFork(Operations):
    def __init__(self):
        hfs.OFFSET = 0
        self.volume = NestedVolume()
        self.entry = next(e for e in self.volume.entries() if e['name'] == 'af34f3ce7e69c63e0635ee357f5405cefd45834a.zip')
        (ROOT / 'cross-build/update-zip-source.json').write_text(__import__('json').dumps(self.entry, indent=2))

    def getattr(self, path, fh=None):
        if path == '/':
            return dict(st_mode=stat.S_IFDIR | 0o555, st_nlink=2)
        if path == '/update.zip':
            return dict(st_mode=stat.S_IFREG | 0o444, st_nlink=1,
                        st_size=self.entry['fork'][0], st_uid=os.getuid(), st_gid=os.getgid())
        raise FuseOSError(errno.ENOENT)

    def readdir(self, path, fh):
        return ['.', '..', 'update.zip']

    def open(self, path, flags):
        if path != '/update.zip':
            raise FuseOSError(errno.ENOENT)
        if flags & (os.O_WRONLY | os.O_RDWR):
            raise FuseOSError(errno.EROFS)
        return 0

    def read(self, path, size, offset, fh):
        if offset >= self.entry['fork'][0]:
            return b''
        return self.volume.read(self.entry['fork'], offset, size)


if __name__ == '__main__':
    mount = ROOT / 'cross-build/source-update-ro'
    mount.mkdir(exist_ok=True)
    FUSE(UpdateFork(), str(mount), foreground=True, ro=True, nothreads=True)
