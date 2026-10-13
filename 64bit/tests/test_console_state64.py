from __future__ import annotations

import unittest

from test_gui_lifecycle64 import run_fixture64


class ConsoleState64Test(unittest.TestCase):
    def test_mode_input_resize_focus_close_transitions(self) -> None:
        run_fixture64("console_state64_host.c", (
            "kernel/console_editor64.c", "lib/keymap64.c", "drivers/keyboard64.c",
        ))


if __name__ == "__main__":
    unittest.main()
