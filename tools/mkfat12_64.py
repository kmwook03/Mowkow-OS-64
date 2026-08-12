#!/usr/bin/env python3
import math
import struct
import sys

SECTOR_SIZE = 512
# 2 MiB. 플로피 크기(2880섹터)를 벗어나지만 매체는 IDE 드라이브라 상관없다.
# 클러스터당 1섹터를 유지하면서 FAT12로 남을 수 있는 상한 근처다: 데이터
# 영역이 4041클러스터라 FAT12/FAT16 경계인 4085보다 아래다.
# 이 값들은 src64/boot/boot64.asm의 BPB, src64/kernel/fd64.c의
# FD64_IMAGE_SECTORS, Makefile의 FAT12_64_* 상수와 반드시 함께 움직인다.
TOTAL_SECTORS = 4096
STAGE2_SECTORS = 16
RESERVED_SECTORS = 1 + STAGE2_SECTORS
FAT_COUNT = 2
# 4043개 엔트리 x 1.5바이트 = 6065바이트 -> 12섹터
SECTORS_PER_FAT = 12
ROOT_ENTRIES = 224
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR_SIZE
ROOT_LBA = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT
DATA_LBA = ROOT_LBA + ROOT_SECTORS


def upper_ascii(text):
    """ASCII 소문자만 대문자로. UTF-8 바이트는 그대로 둔다 (커널의
    make_name83과 같은 규칙이라 한글 이름도 양쪽에서 같은 바이트가 된다)."""
    return bytes(c - 32 if 0x61 <= c <= 0x7a else c for c in text.encode("utf-8"))


def dir_name(name):
    base, _, ext = name.partition(".")
    b = upper_ascii(base)
    e = upper_ascii(ext)
    if len(b) > 8 or len(e) > 3:
        raise SystemExit(f"8.3에 들어가지 않는 이름: {name}")
    return b.ljust(8) + e.ljust(3)


def set_fat12(fat, cluster, value):
    offset = cluster + cluster // 2
    value &= 0x0fff
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0f) | ((value << 4) & 0xf0)
        fat[offset + 1] = (value >> 4) & 0xff
    else:
        fat[offset] = value & 0xff
        fat[offset + 1] = (fat[offset + 1] & 0xf0) | ((value >> 8) & 0x0f)


def write_file(image, fat, root, index, name, data, cluster):
    sectors = max(1, math.ceil(len(data) / SECTOR_SIZE))
    for i in range(sectors):
        next_cluster = 0x0fff if i == sectors - 1 else cluster + i + 1
        set_fat12(fat, cluster + i, next_cluster)
        offset = (DATA_LBA + cluster + i - 2) * SECTOR_SIZE
        chunk = data[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE]
        image[offset:offset + len(chunk)] = chunk

    entry = index * 32
    root[entry:entry + 11] = dir_name(name)
    root[entry + 11] = 0x20
    struct.pack_into("<H", root, entry + 26, cluster)
    struct.pack_into("<I", root, entry + 28, len(data))
    return cluster + sectors


def main():
    if len(sys.argv) < 6:
        print("usage: mkfat12_64.py image boot.bin loader.bin kernel.bin h04.fnt [NAME=path ...]", file=sys.stderr)
        return 2

    image_path, boot_path, loader_path, kernel_path, h04_path = sys.argv[1:6]
    app_specs = sys.argv[6:]
    with open(boot_path, "rb") as f:
        boot = f.read()
    with open(loader_path, "rb") as f:
        loader = f.read()
    with open(kernel_path, "rb") as f:
        kernel = f.read()
    with open(h04_path, "rb") as f:
        h04 = f.read()

    if len(boot) != SECTOR_SIZE:
        raise SystemExit("boot sector must be 512 bytes")
    if len(loader) > STAGE2_SECTORS * SECTOR_SIZE:
        raise SystemExit("loader does not fit in reserved sectors")

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
    image[0:SECTOR_SIZE] = boot
    image[SECTOR_SIZE:SECTOR_SIZE + len(loader)] = loader

    fat = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    fat[0:3] = b"\xf0\xff\xff"
    root = bytearray(ROOT_SECTORS * SECTOR_SIZE)

    cluster = 2
    cluster = write_file(image, fat, root, 0, "KERNEL64.BIN", kernel, cluster)
    cluster = write_file(image, fat, root, 1, "H04.FNT", h04, cluster)
    readme = (
        "Mowkow OS x86_64 FAT12 image\r\n"
        "한글 콘솔에서 읽는 UTF-8 파일입니다.\r\n"
    ).encode("utf-8")
    cluster = write_file(image, fat, root, 2, "README.TXT", readme, cluster)

    root_index = 3
    for spec in app_specs:
        name, sep, path = spec.partition("=")
        if not sep:
            raise SystemExit(f"bad app spec: {spec}")
        with open(path, "rb") as f:
            data = f.read()
        cluster = write_file(image, fat, root, root_index, name.upper(), data, cluster)
        root_index += 1

    for copy in range(FAT_COUNT):
        start = (RESERVED_SECTORS + copy * SECTORS_PER_FAT) * SECTOR_SIZE
        image[start:start + len(fat)] = fat

    root_start = ROOT_LBA * SECTOR_SIZE
    image[root_start:root_start + len(root)] = root

    with open(image_path, "wb") as f:
        f.write(image)


if __name__ == "__main__":
    raise SystemExit(main())
