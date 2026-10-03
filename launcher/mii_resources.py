"""Recover face artwork from a user-supplied disc; never bundle personal NAND."""
from __future__ import annotations

from pathlib import Path
import struct
import tempfile

from mii_data import atomic_bytes

MAX_RESOURCE = 32 * 1024 * 1024


def validate_resource(data: bytes):
    # RFL resource header: archive count, version, then relative archive offsets.
    if not 80 <= len(data) <= MAX_RESOURCE:
        raise ValueError('The Mii artwork file is incomplete or too large.')
    count = int.from_bytes(data[:2], 'big')
    if count != 18:
        raise ValueError('This is not a supported Wii RFL_Res.dat artwork file.')
    offsets = struct.unpack_from('>18I', data, 4)
    if offsets[0] < 76 or list(offsets) != sorted(offsets) or offsets[-1] >= len(data):
        raise ValueError('The Mii artwork archive table is invalid.')


def resource_from_u8(data: bytes) -> bytes | None:
    """Read only the named resource from U8; archive paths never reach the disk."""
    if data[:4] != b'U\xaa8-':
        return None
    if len(data) < 32:
        raise ValueError('Incomplete artwork archive.')
    root, header_size, data_start = struct.unpack_from('>III', data, 4)
    if root < 32 or root + 12 > len(data):
        raise ValueError('Invalid archive root.')
    count = struct.unpack_from('>I', data, root + 8)[0]
    strings = root + count * 12
    if count < 1 or strings > root + header_size or root + header_size > len(data):
        raise ValueError('Invalid archive file table.')
    for index in range(1, count):
        kind_name, offset, size = struct.unpack_from('>III', data, root + index * 12)
        if kind_name >> 24:
            continue
        name_start = strings + (kind_name & 0xffffff)
        name_end = data.find(b'\0', name_start, root + header_size)
        if name_start < strings or name_end < 0:
            raise ValueError('Invalid archive filename.')
        if data[name_start:name_end] == b'RFL_Res.dat':
            if offset < data_start or offset + size > len(data):
                raise ValueError('Incomplete Mii artwork in archive.')
            result = data[offset:offset+size]
            validate_resource(result)
            return result
    return None


def recover_resource(image: Path, root: Path, dtk: Path, command, checkpoint):
    target = root / 'Runtime/UserData/NAND/shared2/menu/FaceLib/RFL_Res.dat'
    if target.exists():
        validate_resource(target.read_bytes())
        return
    # Full WSR images carry the System Menu in the update partition. dtk handles
    # image compression and WAD decryption. No keys or copyrighted assets shipped.
    with tempfile.TemporaryDirectory(prefix='.mii-setup-', dir=root) as temporary:
        work = Path(temporary)
        update = work / 'Update'
        command([str(dtk), 'disc', 'extract', str(image), str(update), '-p', 'update', '-q'])
        packages = sorted((update / 'files').rglob('*Systemmenu*.wad'))
        if not packages:
            raise ValueError('This dump has no System Menu package. Choose a complete Wii Sports Resort disc dump, '
                             'or import the artwork from an existing FaceLib folder.')
        for index, package in enumerate(packages):
            checkpoint()
            content = work / f'menu-{index}'
            command([str(dtk), 'wad', 'extract', str(package), '-o', str(content), '-q'])
            for app in sorted(content.glob('*.app')):
                checkpoint()
                if app.stat().st_size > MAX_RESOURCE:
                    continue
                resource = resource_from_u8(app.read_bytes())
                if resource is not None:
                    atomic_bytes(target, resource)
                    return
        raise ValueError('The disc’s System Menu did not contain supported Mii artwork. '
                         'Choose another complete dump or import the artwork from FaceLib.')
