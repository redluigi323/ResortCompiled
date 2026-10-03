# SPDX-License-Identifier: GPL-3.0-only
"""Wii Mii records and database support adapted from Team Wheel Wizard.

See THIRD-PARTY.md. Binary layout/defaults follow MiiSerializer, MiiFactory,
MiiRepositoryService and MiiDbService. Preserve unedited bytes and hidden Miis.
"""
from __future__ import annotations

import binascii
import os
from pathlib import Path
import secrets
import time
import uuid

RECORD_SIZE = 74
DATABASE_SIZE = 779968
CRC_OFFSET = 0x1F1DE
SLOTS = 100

# key: (section, label, byte offset, word size, bit shift, bit width, min, max, default)
FIELDS = {
    'girl': ('Profile', 'Body type', 0, 2, 14, 1, 0, 1, 0),
    'month': ('Profile', 'Birthday month (0 = unset)', 0, 2, 10, 4, 0, 12, 0),
    'day': ('Profile', 'Birthday day (0 = unset)', 0, 2, 5, 5, 0, 31, 0),
    'color': ('Profile', 'Favorite color', 0, 2, 1, 4, 0, 11, 5),
    'favorite': ('Profile', 'Favorite', 0, 2, 0, 1, 0, 1, 1),
    'height': ('Profile', 'Height', 22, 1, 0, 7, 0, 127, 63),
    'weight': ('Profile', 'Build', 23, 1, 0, 7, 0, 127, 63),
    'face': ('Face', 'Face shape', 32, 2, 13, 3, 0, 7, 0),
    'skin': ('Face', 'Skin tone', 32, 2, 10, 3, 0, 5, 0),
    'feature': ('Face', 'Complexion', 32, 2, 6, 4, 0, 11, 0),
    'hair': ('Hair', 'Hairstyle', 34, 2, 9, 7, 0, 71, 33),
    'hair_color': ('Hair', 'Hair color', 34, 2, 6, 3, 0, 7, 1),
    'hair_flip': ('Hair', 'Mirror hair', 34, 2, 5, 1, 0, 1, 0),
    'brow': ('Eyebrows', 'Shape', 36, 4, 27, 5, 0, 23, 6),
    'brow_rotation': ('Eyebrows', 'Rotation', 36, 4, 22, 4, 0, 11, 6),
    'brow_color': ('Eyebrows', 'Color', 36, 4, 13, 3, 0, 7, 1),
    'brow_size': ('Eyebrows', 'Size', 36, 4, 9, 4, 0, 8, 4),
    'brow_y': ('Eyebrows', 'Vertical position', 36, 4, 4, 5, 3, 18, 10),
    'brow_spacing': ('Eyebrows', 'Spacing', 36, 4, 0, 4, 0, 12, 2),
    'eye': ('Eyes', 'Shape', 40, 4, 26, 6, 0, 47, 2),
    'eye_rotation': ('Eyes', 'Rotation', 40, 4, 21, 3, 0, 7, 4),
    'eye_y': ('Eyes', 'Vertical position', 40, 4, 16, 5, 0, 18, 12),
    'eye_color': ('Eyes', 'Color', 40, 4, 13, 3, 0, 5, 0),
    'eye_size': ('Eyes', 'Size', 40, 4, 9, 3, 0, 7, 4),
    'eye_spacing': ('Eyes', 'Spacing', 40, 4, 5, 4, 0, 12, 2),
    'nose': ('Nose', 'Shape', 44, 2, 12, 4, 0, 11, 1),
    'nose_size': ('Nose', 'Size', 44, 2, 8, 4, 0, 8, 4),
    'nose_y': ('Nose', 'Vertical position', 44, 2, 3, 5, 0, 18, 9),
    'mouth': ('Mouth', 'Shape', 46, 2, 11, 5, 0, 23, 23),
    'mouth_color': ('Mouth', 'Color', 46, 2, 9, 2, 0, 2, 0),
    'mouth_size': ('Mouth', 'Size', 46, 2, 5, 4, 0, 8, 4),
    'mouth_y': ('Mouth', 'Vertical position', 46, 2, 0, 5, 0, 18, 13),
    'glasses': ('Glasses', 'Style', 48, 2, 12, 4, 0, 8, 0),
    'glasses_color': ('Glasses', 'Color', 48, 2, 9, 3, 0, 5, 0),
    'glasses_size': ('Glasses', 'Size', 48, 2, 5, 3, 0, 7, 4),
    'glasses_y': ('Glasses', 'Vertical position', 48, 2, 0, 5, 0, 20, 10),
    'mustache': ('Facial hair', 'Mustache', 50, 2, 14, 2, 0, 3, 0),
    'beard': ('Facial hair', 'Beard', 50, 2, 12, 2, 0, 3, 0),
    'beard_color': ('Facial hair', 'Color', 50, 2, 9, 3, 0, 7, 0),
    'beard_size': ('Facial hair', 'Mustache size', 50, 2, 5, 4, 0, 8, 4),
    'beard_y': ('Facial hair', 'Mustache position', 50, 2, 0, 5, 0, 16, 10),
    'mole': ('Mole', 'Show mole', 52, 2, 15, 1, 0, 1, 0),
    'mole_size': ('Mole', 'Size', 52, 2, 11, 4, 0, 8, 4),
    'mole_y': ('Mole', 'Vertical position', 52, 2, 6, 5, 0, 30, 20),
    'mole_x': ('Mole', 'Horizontal position', 52, 2, 1, 5, 0, 16, 2),
}


def atomic_bytes(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with temporary.open('xb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def read_field(data: bytes, key: str) -> int:
    _, _, offset, size, shift, bits, *_ = FIELDS[key]
    return (int.from_bytes(data[offset:offset+size], 'big') >> shift) & ((1 << bits) - 1)


def write_field(data: bytearray, key: str, value: int):
    _, label, offset, size, shift, bits, low, high, _ = FIELDS[key]
    if not low <= value <= high:
        raise ValueError(f'{label} is outside the Wii range.')
    word = int.from_bytes(data[offset:offset+size], 'big')
    mask = ((1 << bits) - 1) << shift
    data[offset:offset+size] = ((word & ~mask) | (value << shift)).to_bytes(size, 'big')


def name_of(data: bytes, offset=2) -> str:
    return data[offset:offset+20].decode('utf-16-be', errors='replace').split('\0')[0]


def set_name(data: bytearray, text: str, offset=2):
    if '\0' in text or any(ord(c) < 32 or ord(c) > 0xffff for c in text):
        raise ValueError('Use Wii-compatible characters without emoji or control characters.')
    encoded = text.encode('utf-16-be')
    if len(encoded) > 20 or (offset == 2 and not text.strip()):
        raise ValueError('A Mii name must contain 1–10 characters; creator names allow up to 10.')
    data[offset:offset+20] = encoded.ljust(20, b'\0')


def validate_record(data: bytes):
    # RFLiCharData's first bit is padding0, not an invalid/deleted flag.
    # RFLiConvertRaw2Info ignores it; preserve it when importing real NAND data.
    # See koopthekoopa/RFL include/internal/RFLi_Types.h and RFL_Database.c.
    if len(data) != RECORD_SIZE or not any(data[24:28]):
        raise ValueError('This is not a valid Wii Mii record.')
    copy = bytearray(data)
    set_name(copy, name_of(data))
    set_name(copy, name_of(data, 54), 54)
    for key in FIELDS:
        write_field(copy, key, read_field(data, key))
    month, day = read_field(data, 'month'), read_field(data, 'day')
    if (month == 0) != (day == 0):
        raise ValueError('Set both birthday fields, or leave both at zero.')
    if month and day > (31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)[month-1]:
        raise ValueError('The birthday is not a valid calendar date.')


def new_record(existing: list[bytes], girl=False) -> bytearray:
    result = bytearray(RECORD_SIZE)
    for key, field in FIELDS.items():
        write_field(result, key, field[-1])
    if girl:
        for key, value in [('girl', 1), ('hair', 12), ('brow', 0), ('eye', 4), ('eye_rotation', 3)]:
            write_field(result, key, value)
    set_name(result, 'New Mii')
    ids = {int.from_bytes(record[24:28], 'big') for record in existing}
    counter = int((time.time() - 1136073600) // 4) & 0x1fffffff
    while (0x80000000 | counter) in ids:
        counter = (counter + 1) & 0x1fffffff
    result[24:28] = (0x80000000 | counter).to_bytes(4, 'big')
    result[28:32] = next((record[28:32] for record in existing if any(record[28:32])), secrets.token_bytes(4))
    return result


def checksum(data: bytearray):
    data[CRC_OFFSET:CRC_OFFSET+2] = binascii.crc_hqx(data[:CRC_OFFSET], 0).to_bytes(2, 'big')


def fresh_database() -> bytearray:
    data = bytearray(DATABASE_SIZE)
    data[:4] = b'RNOD'
    data[0x1cec] = 0x80
    data[0x1d00:0x1d08] = b'RNHD\xff\xff\xff\xff'
    checksum(data)
    return data


def validate_database(data: bytes):
    if len(data) != DATABASE_SIZE or data[:4] != b'RNOD' or data[0x1d00:0x1d04] != b'RNHD':
        raise ValueError('Unsupported or incomplete RFL_DB.dat. The original file was left untouched.')
    if binascii.crc_hqx(data[:CRC_OFFSET], 0) != int.from_bytes(data[CRC_OFFSET:CRC_OFFSET+2], 'big'):
        raise ValueError('The Mii database checksum is invalid. Restore a backup before editing it.')


def database_path(root: Path) -> Path:
    return root / 'Runtime/UserData/NAND/shared2/menu/FaceLib/RFL_DB.dat'


class Database:
    def __init__(self, path: Path):
        self.path = path
        self.original = path.read_bytes() if path.exists() else None
        self.data = bytearray(self.original) if self.original is not None else fresh_database()
        validate_database(self.data)

    def records(self) -> list[tuple[int, bytes]]:
        return [(i, bytes(self.data[4+i*74:4+(i+1)*74])) for i in range(SLOTS)
                if any(self.data[4+i*74:4+(i+1)*74])]

    def put(self, record: bytes, slot: int | None = None):
        validate_record(record)
        used = {i for i, _ in self.records()}
        if slot is None:
            slot = next((i for i in range(SLOTS) if i not in used), None)
        if slot is None or not 0 <= slot < SLOTS:
            raise ValueError('All 100 Mii slots are occupied.')
        if any(i != slot and r[24:32] == record[24:32] for i, r in self.records()):
            raise ValueError('That Mii is already in your collection.')
        self.data[4+slot*74:4+(slot+1)*74] = record

    def save(self):
        # Also protects against another process changing the file while editing.
        current = self.path.read_bytes() if self.path.exists() else None
        if current != self.original:
            raise ValueError('The Mii database changed. Reopen the editor before saving.')
        checksum(self.data)
        if current is not None:
            backup = self.path.parent / 'Backups' / f'RFL_DB-{time.time_ns()}.dat'
            atomic_bytes(backup, current)
        atomic_bytes(self.path, self.data)
        self.original = bytes(self.data)


def ensure_database(root: Path):
    path = database_path(root)
    if not path.exists():
        Database(path).save()
