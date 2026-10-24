"""Expose source RAW's fragmented SharedSupport fork as a read-only FUSE file.

Avoids extracting an 18 GB copy. No physical disk access, no write methods.
"""
from pathlib import Path
import errno
import os
import stat
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'cross-build/python-deps'))
from fuse import FUSE, Operations, FuseOSError
from inspect_source_hfs import Volume


class SourceFork(Operations):
    def __init__(self):
        self.volume = Volume()
        self.entry = next(e for e in self.volume.entries() if e['name'] == 'SharedSupport.dmg')

    def getattr(self, path, fh=None):
        if path == '/':
            return dict(st_mode=stat.S_IFDIR | 0o555, st_nlink=2)
        if path == '/SharedSupport.dmg':
            return dict(st_mode=stat.S_IFREG | 0o444, st_nlink=1,
                        st_size=self.entry['fork'][0], st_uid=os.getuid(), st_gid=os.getgid())
        raise FuseOSError(errno.ENOENT)

    def readdir(self, path, fh):
        return ['.', '..', 'SharedSupport.dmg']

    def open(self, path, flags):
        if path != '/SharedSupport.dmg':
            raise FuseOSError(errno.ENOENT)
        if flags & (os.O_WRONLY | os.O_RDWR):
            raise FuseOSError(errno.EROFS)
        return 0

    def read(self, path, size, offset, fh):
        if offset >= self.entry['fork'][0]:
            return b''
        return self.volume.read(self.entry['fork'], offset, size)


if __name__ == '__main__':
    mount = ROOT / 'cross-build/source-sharedsupport-ro'
    mount.mkdir(exist_ok=True)
    FUSE(SourceFork(), str(mount), foreground=True, ro=True, nothreads=True)
