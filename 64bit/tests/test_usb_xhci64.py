from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class UsbXhci64Test(unittest.TestCase):
    def test_controller_transfer_and_buffer_ownership(self) -> None:
        self.run_native("xhci_transport64_host.c", [])

    def test_capability_ring_and_context_corruption(self) -> None:
        self.run_native("xhci_transport64_host.c", [], ("--faults",))

    def test_port_quarantine_and_slot_retirement(self) -> None:
        self.run_native("xhci_transport64_host.c", [], ("--quarantine",))

    def test_board_device_failures_keep_other_input_and_heartbeat(self) -> None:
        self.run_native(
            "usb_board64_host.c", ["arch/aarch64/usbhid64.c", "lib/fifo64.c"],
        )

    def test_descriptor_hid_policy_and_transport_failures(self) -> None:
        self.run_native(
            "usb_xhci64_host.c",
            ["arch/aarch64/usb_xhci64.c", "arch/aarch64/usbhid64.c"],
        )

    def run_native(self, fixture: str, sources: list[str],
                   arguments: tuple[str, ...] = ()) -> None:
        source = ROOT / "64bit/src64"
        with tempfile.TemporaryDirectory(prefix="usb-xhci64-") as temporary:
            executable = Path(temporary) / "usb_xhci64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", "-Wall",
                 "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections",
                 "-DMOWKOW64_BOOT_TESTS=0", "-idirafter", str(source / "include"),
                 str(ROOT / "64bit/tests" / fixture),
                 *(str(source / path) for path in sources),
                 "-Wl,--gc-sections", "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable), *arguments], check=True, cwd=ROOT, timeout=10)


if __name__ == "__main__":
    unittest.main()
