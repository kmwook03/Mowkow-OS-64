"""Inspect real SDK ELFs without executing UNIX syscalls on the host."""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = r"""
#include <mowkow/syscall64.h>
_Thread_local int initialized_tls = 37;
_Thread_local char zero_tls[180];
volatile int data = 1;
volatile int bss;
int main(void);
void __mlibc_entry(uintptr_t *stack, int (*entry)(void));
void __mlibc_entry(uintptr_t *stack, int (*entry)(void))
{
    bss = (int) *stack + entry();
    __builtin_trap();
}
int main(void)
{
    return data + initialized_tls + zero_tls[0] +
        (int) unix64_syscall6(0x106, 0, 0, 0, 0, 0, 0);
}
"""


class Unix64SdkTest(unittest.TestCase):
    def test_raw_errno_boundary(self) -> None:
        with tempfile.TemporaryDirectory(prefix="unix64-errno-") as temporary:
            directory = Path(temporary)
            source = directory / "errno.c"
            source.write_text(
                '#include "unix64.h"\n'
                'int main(void) { return unix64_result_errno(-1) != 1 ||\n'
                'unix64_result_errno(-4095) != 4095 ||\n'
                'unix64_result_errno(-4096) != 0 ||\n'
                'unix64_result_errno(INT64_MIN) != 0 ||\n'
                'unix64_result_errno(0) != 0 ||\n'
                'unix64_result_errno(INT64_MAX) != 0; }\n',
                encoding="utf-8",
            )
            executable = directory / "errno"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-std=c11", "-Werror",
                 f"-I{ROOT / '64bit/src64/include/uapi'}", str(source),
                 "-o", str(executable)], check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_sdk_layouts(self) -> None:
        compilers = {
            "x86_64": os.environ.get("X64_CC", "x86_64-elf-gcc"),
            "aarch64": os.environ.get("A64_CC", "aarch64-none-elf-gcc"),
        }
        if any(shutil.which(cc) is None for cc in compilers.values()):
            self.skipTest("SDK ELF checks require both OS cross-compilers")
        with tempfile.TemporaryDirectory(prefix="unix64-sdk-") as temporary:
            directory = Path(temporary)
            result = subprocess.run(
                ["make", "--no-print-directory", "unix64-sdk",
                 f"BUILD64_DIR={directory / 'build'}",
                 f"X64_CC={compilers['x86_64']}", f"A64_CC={compilers['aarch64']}"],
                cwd=ROOT, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            source = directory / "fixture.c"
            source.write_text(FIXTURE, encoding="utf-8")
            for arch, cc in compilers.items():
                with self.subTest(architecture=arch):
                    sdk = directory / "build/unix64" / arch / "sysroot"
                    self.assertEqual(
                        {str(p.relative_to(sdk)) for p in sdk.rglob("*") if p.is_file()},
                        {"usr/include/mowkow/unix64.h", "usr/include/mowkow/syscall64.h",
                         "usr/lib/crt1.o", "usr/lib/syscall64.o", "usr/lib/unix64.ld"},
                    )
                    self.assertEqual(
                        (sdk / "usr/include/mowkow/unix64.h").read_bytes(),
                        (ROOT / "64bit/src64/include/uapi/unix64.h").read_bytes(),
                    )
                    elf = directory / f"{arch}.elf"
                    command = [
                        cc, "-nostdlib", "-static", "-ffreestanding", "-fno-pie",
                        "-fno-pic", "-fno-stack-protector", "-Wl,--build-id=none",
                        f"-I{sdk / 'usr/include'}", str(source),
                        str(sdk / "usr/lib/crt1.o"), str(sdk / "usr/lib/syscall64.o"),
                        f"-Wl,-T,{sdk / 'usr/lib/unix64.ld'}", "-o", str(elf),
                    ]
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.check_elf(elf.read_bytes(), 62 if arch == "x86_64" else 183)
                    # Empty .data must not let TLS share the preceding RX page.
                    source.write_text(FIXTURE.replace("volatile int data = 1;",
                                                     "volatile int data;"), encoding="utf-8")
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.check_elf(elf.read_bytes(), 62 if arch == "x86_64" else 183)
                    source.write_text(FIXTURE, encoding="utf-8")
                    # Losing/duplicating the identifying CRT note must fail linking.
                    for mode in ("missing", "duplicate"):
                        altered = command.copy()
                        if mode == "missing":
                            altered.remove(str(sdk / "usr/lib/crt1.o"))
                        else:
                            note = directory / f"{arch}-duplicate.S"
                            note.write_text(
                                '.section .note.mowkow,"a",%note\n'
                                '.balign 4\n.long 7, 4, 1\n'
                                '.asciz "MOWKOW"\n.balign 4\n.long 1\n',
                                encoding="utf-8",
                            )
                            altered.insert(-2, str(note))
                        result = subprocess.run(altered, capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("exactly one UNIX ABI note required", result.stderr)

    def check_elf(self, data: bytes, machine: int) -> None:
        self.assertEqual(data[:7], b"\x7fELF\x02\x01\x01")
        kind, actual_machine = struct.unpack_from("<HH", data, 16)
        entry, phoff = struct.unpack_from("<QQ", data, 24)
        phsize, phnum = struct.unpack_from("<HH", data, 54)
        self.assertEqual((kind, actual_machine, phsize), (2, machine, 56))
        headers = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize)
                   for i in range(phnum)]
        self.assertFalse(any(p[0] in (2, 3) for p in headers))  # no dynamic/interp
        loads = [p for p in headers if p[0] == 1 and p[6]]
        self.assertEqual({p[1] for p in loads}, {4, 5, 6})
        self.assertTrue(any(p[1] == 5 and p[3] <= entry < p[3] + p[6] for p in loads))
        for _, flags, offset, address, _, filesz, memsz, align in loads:
            self.assertLessEqual(filesz, memsz)
            self.assertLessEqual(offset + filesz, len(data))
            self.assertGreaterEqual(address, 0x400000)
            self.assertLessEqual(address + memsz, 0x4000000)
            self.assertGreaterEqual(align, 4096)
            self.assertEqual(address % align, offset % align)
            for other in loads:
                overlap = (address // 4096 < (other[3] + other[6] + 4095) // 4096
                           and other[3] // 4096 < (address + memsz + 4095) // 4096)
                if overlap:
                    self.assertNotEqual((flags | other[1]) & 3, 3)
        self.assertTrue(any(p[1] == 4 and p[2] == 0 and
                            p[5] >= phoff + phnum * phsize for p in loads))
        notes = [p for p in headers if p[0] == 4]
        self.assertEqual(len(notes), 1)
        note = notes[0]
        self.assertEqual(data[note[2]:note[2] + note[5]],
                         struct.pack("<III8sI", 7, 4, 1, b"MOWKOW\0\0", 1))
        tls = [p for p in headers if p[0] == 7]
        self.assertEqual(len(tls), 1)
        self.assertEqual(tls[0][5], 4)
        self.assertGreaterEqual(tls[0][6], 184)
        self.assertEqual(data[tls[0][2]:tls[0][2] + 4], b"\x25\0\0\0")


if __name__ == "__main__":
    unittest.main()
