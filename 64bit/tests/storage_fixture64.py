"""Independent on-disk fixtures; never import the image builder or kernel tables.

The sparse volume is just above FAT32's minimum cluster count. Root and file
data occupy different cache blocks so write ordering can be observed on media.
All callers supply paths inside a TemporaryDirectory.
"""

import struct
from pathlib import Path


SECTOR_SIZE = 512
TOTAL_SECTORS = 67584
RESERVED_SECTORS = 32
FAT_SECTORS = 528
DATA_LBA = 1088
FILE_CLUSTER = 10
FILE_LBA = 1096
LONG_NAME = "fixture long.txt"
SHORT_NAME = "FIXTUR~1.TXT"
PAYLOAD = bytes(range(256)) * 32


def elf_fixture(corruption: str = "", machine: int = 62) -> bytes:
    """Two adjacent ELF64 PT_LOAD segments with independently packed headers."""
    image = bytearray(0x2010)
    identity = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIQQQIHHHHHH", image, 0, identity, 2, machine, 1,
                     0x400000, 64, 0, 0, 64, 56, 2, 0, 0, 0)
    for index, flags in enumerate((5, 6)):
        struct.pack_into("<IIQQQQQQ", image, 64 + 56 * index, 1, flags,
                         0x1000 * (index + 1), 0x400000 + 0x1000 * index,
                         0, 16, 0x1000, 0x1000)
    patches = {
        "magic": (0, "B", 0),
        "machine": (18, "H", 183 if machine == 62 else 62),
        "phoff-overflow": (32, "Q", 0xfffffffffffffff0),
        "offset-overflow": (72, "Q", 0xfffffffffffffff0),
        "address-overflow": (80, "Q", 0xfffffffffffffff0),
        "filesz": (96, "Q", 0x1001),
        "overlap": (136, "Q", 0x400000),
        "alignment": (112, "Q", 0x1800),
        "flags": (68, "I", 0x80000005),
        "entry": (24, "Q", 0x401000),
        "no-load": (56, "H", 0),
    }
    if corruption == "truncated-header":
        return bytes(image[:63])
    if corruption == "truncated-table":
        return bytes(image[:175])
    if corruption:
        offset, fmt, value = patches[corruption]
        struct.pack_into("<" + fmt, image, offset, value)
    return bytes(image)


def volume_fixture(path: Path, *, bpb: str = "", fat: str = "",
                   vfat: str = "", payload: bytes = PAYLOAD) -> None:
    """Generate one sparse superfloppy, optionally corrupting one input family."""
    boot = bytearray(512)
    struct.pack_into("<HBHB", boot, 11, 512, 1, 32, 2)
    struct.pack_into("<II", boot, 32, TOTAL_SECTORS, FAT_SECTORS)
    struct.pack_into("<I", boot, 44, 2)
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xaa"
    bpb_patches = {
        "signature": (510, "H", 0),
        "sector-size": (11, "H", 1024),
        "cluster-size": (13, "B", 3),
        "reserved": (14, "H", 0),
        "fat-capacity": (36, "I", 1),
        "geometry-overflow": (36, "I", 0xffffffff),
        "device-size": (32, "I", TOTAL_SECTORS + 1),
        "root-range": (44, "I", TOTAL_SECTORS),
    }
    if bpb:
        offset, fmt, value = bpb_patches[bpb]
        struct.pack_into("<" + fmt, boot, offset, value)

    table = bytearray(FAT_SECTORS * SECTOR_SIZE)
    struct.pack_into("<III", table, 0, 0x0ffffff8, 0xffffffff, 0x0fffffff)
    # Mark the root's remaining cache block unavailable. File growth then uses
    # cluster 26 on a separate block, exposing data/FAT/directory write order.
    for cluster in range(3, FILE_CLUSTER):
        struct.pack_into("<I", table, cluster * 4, 0x0ffffff7)
    clusters = max(1, (len(payload) + 511) // 512)
    for index in range(clusters):
        cluster = FILE_CLUSTER + index
        struct.pack_into("<I", table, cluster * 4,
                         cluster + 1 if index + 1 < clusters else 0x0fffffff)
    bad_values = {"free": 0, "reserved": 0x0ffffff0, "bad": 0x0ffffff7,
                  "range": TOTAL_SECTORS, "cycle": 2, "short": 0x0fffffff}
    if fat:
        owner, kind = fat.split("-", 1)
        cluster = 2 if owner == "root" else FILE_CLUSTER
        value = cluster if kind == "cycle" else bad_values[kind]
        struct.pack_into("<I", table, cluster * 4, value)

    alias = b"FIXTUR~1TXT"
    checksum = 0
    for value in alias:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + value) & 255
    units = list(struct.unpack("<16H", LONG_NAME.encode("utf-16-le")))
    units += [0] + [0xffff] * 9
    root = bytearray(512)
    offsets = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
    for entry, ordinal in enumerate((2, 1)):
        base = entry * 32
        root[base] = ordinal | (0x40 if entry == 0 else 0)
        root[base + 11] = 0x0f
        root[base + 13] = checksum
        for index, offset in enumerate(offsets):
            struct.pack_into("<H", root, base + offset, units[(ordinal - 1) * 13 + index])
    root[64:75] = alias
    root[75] = 0x20
    struct.pack_into("<HI", root, 90, FILE_CLUSTER, len(payload))
    vfat_patches = {
        "checksum": (13, "B", checksum ^ 1),
        "ordinal": (32, "B", 2),
        "missing-last": (0, "B", 2),
        "oversized": (0, "B", 0x45),
        "zero-ordinal": (0, "B", 0x40),
        "surrogate": (33, "H", 0xd800),
        "deleted": (32, "B", 0xe5),
    }
    if vfat:
        offset, fmt, value = vfat_patches[vfat]
        struct.pack_into("<" + fmt, root, offset, value)
    with path.open("wb") as stream:
        stream.truncate(TOTAL_SECTORS * SECTOR_SIZE)
        stream.write(boot)
        for lba in (RESERVED_SECTORS, RESERVED_SECTORS + FAT_SECTORS):
            stream.seek(lba * SECTOR_SIZE)
            stream.write(table)
        stream.seek(DATA_LBA * SECTOR_SIZE)
        stream.write(root)
        stream.seek(FILE_LBA * SECTOR_SIZE)
        stream.write(payload)
