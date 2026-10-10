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

FAT32_EOC = 0x0FFFFFFF
FAT_FILE_SIZE_MAX = 0xFFFFFFFF
FD64_LFN_MAX_UNITS = 52
FD64_NAME_MAX = 160
VFAT_FORBIDDEN = set('"*/:<>?\\|')
# no RTC in the image builder or the kernel: same fixed stamp both sides
FIXED_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1
FIXED_TIME = 0


def validate_layout(
    *,
    sector_size: int = SECTOR_SIZE,
    total_sectors: int = TOTAL_SECTORS,
    reserved_sectors: int = RESERVED_SECTORS,
    fat_count: int = FAT_COUNT,
    sectors_per_fat: int = SECTORS_PER_FAT,
    sectors_per_cluster: int = SECTORS_PER_CLUSTER,
    root_cluster: int = ROOT_CLUSTER,
    fsinfo_lba: int = FSINFO_LBA,
    backup_boot_lba: int = BACKUP_BOOT_LBA,
    backup_fsinfo_lba: int = BACKUP_FSINFO_LBA,
    stage2_lba: int = STAGE2_LBA,
    stage2_sectors: int = STAGE2_SECTORS,
    kernel_lba: int = KERNEL_LBA,
) -> None:
    """Reject a layout that the BPB, boot stages, or FAT cannot represent."""
    if sector_size != 512:
        raise ValueError("the boot stages require 512-byte sectors")
    if total_sectors <= 0 or total_sectors > 0xFFFFFFFF:
        raise ValueError("total sector count does not fit the FAT32 BPB")
    if reserved_sectors <= 0 or reserved_sectors > 0xFFFF:
        raise ValueError("reserved sector count does not fit the FAT32 BPB")
    if fat_count <= 0 or fat_count > 0xFF or sectors_per_fat <= 0:
        raise ValueError("invalid FAT geometry")
    if (sectors_per_cluster <= 0 or
            sectors_per_cluster & (sectors_per_cluster - 1)):
        raise ValueError("sectors per cluster must be a power of two")
    metadata_lbas = {0, fsinfo_lba, backup_boot_lba, backup_fsinfo_lba}
    if len(metadata_lbas) != 4 or min(metadata_lbas) < 0:
        raise ValueError("FAT32 boot metadata sectors overlap")
    if backup_fsinfo_lba != backup_boot_lba + fsinfo_lba:
        raise ValueError("backup FSInfo offset does not match the primary")
    if max(metadata_lbas) >= reserved_sectors:
        raise ValueError("FAT32 boot metadata lies outside the reserved area")
    if stage2_sectors <= 0 or stage2_lba <= max(metadata_lbas):
        raise ValueError("stage 2 overlaps FAT32 boot metadata")
    if stage2_lba + stage2_sectors > kernel_lba:
        raise ValueError("stage 2 overlaps the kernel reserved area")
    if kernel_lba < 0 or kernel_lba >= reserved_sectors:
        raise ValueError("kernel lies outside the reserved area")

    data_lba = reserved_sectors + fat_count * sectors_per_fat
    if data_lba >= total_sectors:
        raise ValueError("FAT copies leave no data area")
    cluster_count = (total_sectors - data_lba) // sectors_per_cluster
    # FAT32 needs at least 65525 clusters or host tools treat it as FAT16.
    if cluster_count < 65525:
        raise ValueError("data area is too small for FAT32")
    if root_cluster < 2 or root_cluster >= cluster_count + 2:
        raise ValueError("root cluster lies outside the data area")
    if (cluster_count + 2) * 4 > sectors_per_fat * sector_size:
        raise ValueError("FAT is too small for the data area")


validate_layout()

DATA_LBA = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT
CLUSTER_COUNT = (TOTAL_SECTORS - DATA_LBA) // SECTORS_PER_CLUSTER
KERNEL_MAX_SECTORS = RESERVED_SECTORS - KERNEL_LBA

MAKE_VARS = {
    "FAT32_64_TOTAL_SECTORS": TOTAL_SECTORS,
    "FAT32_64_RESERVED_SECTORS": RESERVED_SECTORS,
    "FAT32_64_FAT_COUNT": FAT_COUNT,
    "FAT32_64_SECTORS_PER_FAT": SECTORS_PER_FAT,
    "FAT32_64_ROOT_CLUSTER": ROOT_CLUSTER,
    "FAT32_64_FSINFO_LBA": FSINFO_LBA,
    "FAT32_64_BACKUP_BOOT_LBA": BACKUP_BOOT_LBA,
    "FAT32_64_BACKUP_FSINFO_LBA": BACKUP_FSINFO_LBA,
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


def validate_file_name(name: str) -> None:
    """Reject names that the kernel cannot represent or VFAT must alter."""
    encoded = name.encode("utf-8")
    units = [ord(c) if ord(c) <= 0xFFFF else ord("_") for c in name]
    if not encoded or len(encoded) >= FD64_NAME_MAX:
        raise SystemExit(f"이름의 UTF-8 길이가 범위를 벗어납니다: {name}")
    if len(units) > FD64_LFN_MAX_UNITS:
        raise SystemExit(f"이름의 UTF-16 길이가 범위를 벗어납니다: {name}")
    if name in (".", "..") or name[-1] in (" ", "."):
        raise SystemExit(f"VFAT에서 허용되지 않는 이름입니다: {name}")
    if any(ord(c) < 0x20 or c in VFAT_FORBIDDEN for c in name):
        raise SystemExit(f"VFAT에서 허용되지 않는 문자가 있습니다: {name}")


def validate_file_size(size: int) -> None:
    if size < 0 or size > FAT_FILE_SIZE_MAX:
        raise SystemExit("파일 크기가 FAT32의 4 GiB 제한을 넘습니다")


def ascii_fold(name: str) -> str:
    return "".join(c.lower() if "A" <= c <= "Z" else c for c in name)


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
    if count > FD64_LFN_MAX_UNITS // LFN_UNITS_PER_ENTRY:
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
    validate_file_name(name)
    validate_file_size(size)
    name11, lossy = make_shortname(name)
    flags = case_flags(name)
    if not lossy and short_name_text(name11, flags) == name:
        return [dir_entry(name11, 0x20, cluster, size, flags)]
    # build 전에 요청 이름과 이 별칭의 유일성을 함께 검사한다.
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
        validate_file_size(len(data))
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


def validate_files(files: list[tuple[str, bytes]]) -> None:
    seen_names = set()
    seen_aliases = set()
    for name, data in files:
        validate_file_name(name)
        validate_file_size(len(data))
        folded = ascii_fold(name)
        if folded in seen_names:
            raise SystemExit(
                f"중복되거나 대소문자만 다른 이름입니다: {name}"
            )
        seen_names.add(folded)
        entries = name_entries(name, 2, len(data))
        alias = entries[-1][:11]
        if alias in seen_aliases:
            raise SystemExit(f"8.3 별칭이 충돌합니다: {name}")
        seen_aliases.add(alias)


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
    validate_files(files)

    # the root directory is itself a cluster chain, so reserve it first and let
    # the files follow; the kernel grows the chain when it runs out of slots
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
