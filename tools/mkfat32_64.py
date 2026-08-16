#!/usr/bin/env python3
"""Build the 64-bit FAT32 boot image.

This file is the single source of truth for the on-disk layout: the Makefile
reads it with --make-vars and passes the values into boot64.asm's BPB and into
the loader's kernel LBA, so the image and the boot sector cannot disagree.

Layout (64 MiB, 512-byte sectors, 1 sector per cluster):

    LBA 0            boot sector (FAT32 BPB)
    LBA 1            FSInfo
    LBA 6            backup boot sector
    LBA 7            backup FSInfo
    LBA 8..23        stage 2 loader
    LBA 32..1023     kernel image
    LBA 1024         FAT #1
    LBA 2025         FAT #2
    LBA 3026         data area, cluster 2 = root directory

Stage 2 and the kernel sit in the reserved region, so growing the kernel never
shifts cluster numbering.
"""
import math
import struct
import sys

SECTOR_SIZE = 512
TOTAL_SECTORS = 131072
RESERVED_SECTORS = 1024
FAT_COUNT = 2
SECTORS_PER_FAT = 1001
SECTORS_PER_CLUSTER = 1
ROOT_CLUSTER = 2
FSINFO_LBA = 1
BACKUP_BOOT_LBA = 6
BACKUP_FSINFO_LBA = 7
STAGE2_LBA = 8
STAGE2_SECTORS = 16
KERNEL_LBA = 32
VOLUME_ID = 0x646B776D
VOLUME_LABEL = b"MOWKOW64   "

DATA_LBA = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT
CLUSTER_COUNT = (TOTAL_SECTORS - DATA_LBA) // SECTORS_PER_CLUSTER
KERNEL_MAX_SECTORS = RESERVED_SECTORS - KERNEL_LBA
FAT32_EOC = 0x0FFFFFFF
# no RTC in the image builder or the kernel: same fixed stamp both sides
FIXED_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1
FIXED_TIME = 0

# FAT32 needs at least 65525 clusters or host tools read the volume as FAT16.
assert CLUSTER_COUNT >= 65525, CLUSTER_COUNT
assert (CLUSTER_COUNT + 2) * 4 <= SECTORS_PER_FAT * SECTOR_SIZE

MAKE_VARS = {
    "FAT32_64_TOTAL_SECTORS": TOTAL_SECTORS,
    "FAT32_64_RESERVED_SECTORS": RESERVED_SECTORS,
    "FAT32_64_FAT_COUNT": FAT_COUNT,
    "FAT32_64_SECTORS_PER_FAT": SECTORS_PER_FAT,
    "FAT32_64_ROOT_CLUSTER": ROOT_CLUSTER,
    "FAT32_64_FSINFO_LBA": FSINFO_LBA,
    "FAT32_64_BACKUP_BOOT_LBA": BACKUP_BOOT_LBA,
    "FAT32_64_VOLUME_ID": VOLUME_ID,
    "STAGE2_64_LBA": STAGE2_LBA,
    "STAGE2_64_SECTORS": STAGE2_SECTORS,
    "KERNEL64_LBA": KERNEL_LBA,
}


SHORT_OK = set(b"$%\'-_@~`!(){}^#&")
LFN_ATTR = 0x0F
LFN_LAST = 0x40
LFN_UNITS_PER_ENTRY = 13
NT_LOWER_BASE = 0x08
NT_LOWER_EXT = 0x10


def short_char(c):
    """8.3에 넣을 수 있는 바이트로. 표현할 수 없으면 None (kernel의
    shortname_char와 같은 규칙)."""
    if 0x61 <= c <= 0x7A:
        return c - 32
    if 0x41 <= c <= 0x5A or 0x30 <= c <= 0x39 or c in SHORT_OK:
        return c
    return None


def make_shortname(name):
    """(11바이트 8.3 이름, 원래 이름을 그대로 되살리지 못하면 True)"""
    base, dot, ext = name.rpartition(".")
    if not dot:
        base, ext = name, ""
    lossy = False
    out = []
    for part, limit in ((base, 8), (ext, 3)):
        chars = []
        for c in part.encode("utf-8"):
            m = short_char(c)
            if m is None:
                m = ord("_")
                lossy = True
            if len(chars) < limit:
                chars.append(m)
            else:
                lossy = True
        out.append(bytes(chars).ljust(limit))
    if not out[0].strip():
        out[0] = b"_".ljust(8)
        lossy = True
    return out[0] + out[1], lossy


def case_flags(name):
    base, dot, ext = name.rpartition(".")
    if not dot:
        base, ext = name, ""
    flags = 0
    if not any("A" <= c <= "Z" for c in base):
        flags |= NT_LOWER_BASE
    if not any("A" <= c <= "Z" for c in ext):
        flags |= NT_LOWER_EXT
    return flags


def short_name_text(name11, flags):
    base = name11[:8].rstrip().decode("latin-1")
    ext = name11[8:].rstrip().decode("latin-1")
    if flags & NT_LOWER_BASE:
        base = base.lower()
    if flags & NT_LOWER_EXT:
        ext = ext.lower()
    return f"{base}.{ext}" if ext else base


def short_checksum(name11):
    sum_ = 0
    for c in name11:
        sum_ = (((sum_ & 1) << 7) + (sum_ >> 1) + c) & 0xFF
    return sum_


def lfn_entries(name, name11):
    """긴 이름 엔트리들. 8.3 엔트리 앞에, 역순으로 놓인다."""
    units = [ord(c) if ord(c) <= 0xFFFF else ord("_") for c in name]
    total = len(units)
    count = (total + LFN_UNITS_PER_ENTRY - 1) // LFN_UNITS_PER_ENTRY
    if count > 20:
        raise SystemExit(f"이름이 너무 깁니다: {name}")
    checksum = short_checksum(name11)
    out = []
    for ord_ in range(count, 0, -1):
        e = bytearray(32)
        e[0] = ord_ | (LFN_LAST if ord_ == count else 0)
        e[11] = LFN_ATTR
        e[13] = checksum
        for i in range(LFN_UNITS_PER_ENTRY):
            index = (ord_ - 1) * LFN_UNITS_PER_ENTRY + i
            if index < total:
                unit = units[index]
            elif index == total:
                unit = 0x0000
            else:
                unit = 0xFFFF
            off = 1 + i * 2 if i < 5 else (14 + (i - 5) * 2 if i < 11 else 28 + (i - 11) * 2)
            struct.pack_into("<H", e, off, unit)
        out.append(bytes(e))
    return out


def name_entries(name, cluster, size):
    """이 파일의 디렉터리 엔트리 전부 (필요하면 긴 이름 + 8.3)."""
    name11, lossy = make_shortname(name)
    flags = case_flags(name)
    if not lossy and short_name_text(name11, flags) == name:
        return [dir_entry(name11, 0x20, cluster, size, flags)]
    # ~1: 이미지 안에서 이름은 유일하므로 번호는 1로 충분하다
    base = name11[:8].rstrip()[:6].ljust(6, b" ")
    name11 = (base[:6] + b"~1")[:8] + name11[8:]
    return lfn_entries(name, name11) + [dir_entry(name11, 0x20, cluster, size, 0)]


def dir_entry(name11, attr, cluster, size, nt_flags=0):
    e = bytearray(32)
    e[0:11] = name11
    e[11] = attr
    e[12] = nt_flags
    struct.pack_into("<H", e, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", e, 22, FIXED_TIME)
    struct.pack_into("<H", e, 24, FIXED_DATE)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)


def fsinfo_sector():
    s = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", s, 0, 0x41615252)
    struct.pack_into("<I", s, 484, 0x61417272)
    # free count / next free: "unknown", which the spec allows. The kernel
    # never updates FSInfo, so a real count here would go stale on first write.
    struct.pack_into("<I", s, 488, 0xFFFFFFFF)
    struct.pack_into("<I", s, 492, 0xFFFFFFFF)
    struct.pack_into("<I", s, 508, 0xAA550000)
    return bytes(s)


class Volume:
    def __init__(self):
        self.image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
        self.fat = [0] * (CLUSTER_COUNT + 2)
        self.fat[0] = 0x0FFFFFF8
        self.fat[1] = FAT32_EOC
        self.next_free = ROOT_CLUSTER

    def alloc_chain(self, count):
        first = self.next_free
        self.next_free += count
        if self.next_free > CLUSTER_COUNT + 2:
            raise SystemExit("image full")
        for i in range(count):
            c = first + i
            self.fat[c] = FAT32_EOC if i == count - 1 else c + 1
        return first

    def write_cluster_data(self, first_cluster, data):
        for i in range(0, len(data), SECTOR_SIZE):
            lba = DATA_LBA + (first_cluster + i // SECTOR_SIZE - 2) * SECTORS_PER_CLUSTER
            chunk = data[i:i + SECTOR_SIZE]
            self.image[lba * SECTOR_SIZE:lba * SECTOR_SIZE + len(chunk)] = chunk

    def add_file(self, name, data):
        clusters = max(1, math.ceil(len(data) / SECTOR_SIZE))
        first = self.alloc_chain(clusters)
        self.write_cluster_data(first, data)
        return b"".join(name_entries(name, first, len(data)))

    def put_sectors(self, lba, data):
        self.image[lba * SECTOR_SIZE:lba * SECTOR_SIZE + len(data)] = data

    def finish(self, root_first, root_bytes):
        self.write_cluster_data(root_first, root_bytes)
        packed = b"".join(struct.pack("<I", v) for v in self.fat)
        packed = packed.ljust(SECTORS_PER_FAT * SECTOR_SIZE, b"\0")
        for copy in range(FAT_COUNT):
            self.put_sectors(RESERVED_SECTORS + copy * SECTORS_PER_FAT, packed)


def build(image_path, boot, loader, kernel, h04, app_specs):
    if len(boot) != SECTOR_SIZE:
        raise SystemExit("boot sector must be 512 bytes")
    if len(loader) > STAGE2_SECTORS * SECTOR_SIZE:
        raise SystemExit("loader does not fit in its reserved sectors")
    if len(kernel) > KERNEL_MAX_SECTORS * SECTOR_SIZE:
        raise SystemExit(f"kernel exceeds the reserved region ({KERNEL_MAX_SECTORS} sectors)")

    vol = Volume()
    vol.put_sectors(0, boot)
    vol.put_sectors(BACKUP_BOOT_LBA, boot)
    vol.put_sectors(FSINFO_LBA, fsinfo_sector())
    vol.put_sectors(BACKUP_FSINFO_LBA, fsinfo_sector())
    vol.put_sectors(STAGE2_LBA, loader)
    vol.put_sectors(KERNEL_LBA, kernel)

    files = [("H04.FNT", h04)]
    readme = (
        "Mowkow OS x86_64 FAT32 image\r\n"
        "한글 콘솔에서 읽는 UTF-8 파일입니다.\r\n"
    ).encode("utf-8")
    files.append(("README.TXT", readme))
    for spec in app_specs:
        name, sep, path = spec.partition("=")
        if not sep:
            raise SystemExit(f"bad app spec: {spec}")
        with open(path, "rb") as f:
            files.append((name, f.read()))

    # the root directory is itself a cluster chain, so reserve it first and let
    # the files follow; the kernel grows the chain when it runs out of slots
    entries_per_cluster = SECTOR_SIZE * SECTORS_PER_CLUSTER // 32
    # a long name costs extra entries, so size the root by bytes, not by files
    root_bytes = 32 + sum(len(b"".join(name_entries(n, 2, 0))) for n, _ in files)
    root_clusters = max(1, math.ceil(root_bytes / (SECTOR_SIZE * SECTORS_PER_CLUSTER)))
    root_first = vol.alloc_chain(root_clusters)
    if root_first != ROOT_CLUSTER:
        raise SystemExit("root directory must start at cluster 2")

    root = bytearray()
    root += dir_entry(VOLUME_LABEL, 0x08, 0, 0)
    for name, data in files:
        root += vol.add_file(name, data)
    root = root.ljust(root_clusters * SECTOR_SIZE * SECTORS_PER_CLUSTER, b"\0")
    vol.finish(root_first, root)

    with open(image_path, "wb") as f:
        f.write(vol.image)


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--make-vars":
        for k, v in MAKE_VARS.items():
            print(f"{k}={v}")
        return 0
    if len(sys.argv) < 6:
        print("usage: mkfat32_64.py image boot.bin loader.bin kernel.bin h04.fnt [NAME=path ...]",
              file=sys.stderr)
        return 2
    image_path, boot_path, loader_path, kernel_path, h04_path = sys.argv[1:6]
    blobs = []
    for path in (boot_path, loader_path, kernel_path, h04_path):
        with open(path, "rb") as f:
            blobs.append(f.read())
    build(image_path, *blobs, sys.argv[6:])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
