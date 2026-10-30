from __future__ import annotations

import contextlib
import errno
import io
import json
import subprocess
import sys
import tempfile
import unittest
from collections import deque
from pathlib import Path
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "64bit/tools"))
import mowkow_parity as PARITY
sys.path.pop(0)


class ScriptedSocket:
    """Byte-level QMP peer; expected replies do not use client codec helpers."""

    def __init__(self, owner):
        self.owner = owner
        self.chunks = deque([b'{"Q', b'MP":{"version":{}}}\r\n'])
        self.closed = False
        self.requests = []
        self.endpoint = None
        self.enters = 0

    def settimeout(self, timeout: float) -> None:
        self.owner.test.assertGreater(timeout, 0)

    def connect(self, endpoint: str) -> None:
        self.endpoint = Path(endpoint)
        if self.owner.phase == "connect":
            raise OSError(errno.ENOENT, "not ready")
        if self.owner.phase == "permission":
            raise PermissionError(errno.EPERM, "socket blocked")

    def sendall(self, data: bytes) -> None:
        message = json.loads(data)
        self.requests.append(message)
        reply = {"return": {}, "id": message["id"]}
        if self.owner.phase == "error":
            reply = {"error": {"class": "GenericError", "desc": "rejected"},
                     "id": message["id"]}
        if self.owner.phase == "wrong-id":
            reply["id"] = -1
        if self.owner.phase == "send":
            raise TimeoutError("send blocked")
        # A notification and reply share a recv; the greeting was fragmented.
        self.chunks.append(b'{"event":"STOP"}\n' + json.dumps(reply).encode() + b'\n')
        if message["execute"] == "input-send-event":
            events = message["arguments"]["events"]
            if any(e["data"]["down"] and e["data"]["key"]["data"] == "ret"
                   for e in events):
                self.enters += 1
                if self.owner.phase != "input" and self.enters > self.owner.dropped_enters:
                    with self.owner.logs[-1].open("ab") as log:
                        log.write(b"\n> ")

    def recv(self, count: int) -> bytes:
        self.owner.now += 0.01
        phase = self.owner.phase
        if phase == "interrupt":
            raise KeyboardInterrupt()
        if phase == "greeting-timeout":
            raise TimeoutError("no greeting")
        if phase == "eof":
            return b""
        if phase == "json":
            return b"{bad}\n"
        if phase == "greeting":
            return b'{"return":{}}\n'
        if phase == "non-object":
            return b'[]\n'
        if phase == "oversize":
            return b"x" * 80
        if phase == "events" and self.requests:
            return b'{"event":"STOP"}\n'
        if phase == "partial" and self.requests:
            if self.chunks:
                self.chunks.clear()
                return b'{"return":'
            raise TimeoutError("partial reply")
        if self.chunks:
            return self.chunks.popleft()
        raise TimeoutError("no reply")

    def close(self) -> None:
        self.closed = True


class Harness:
    def __init__(self, test, phase: str = "", dropped_enters: int = 0):
        self.test = test
        self.phase = phase
        self.dropped_enters = dropped_enters
        self.now = 0.0
        self.sockets = []
        self.processes = []
        self.logs = []
        self.stderr_files = []
        self.endpoints = []
        self.errors = io.StringIO()

    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.directory = Path(self.stack.enter_context(tempfile.TemporaryDirectory()))
        mkstemp = tempfile.mkstemp
        for target, attribute, value in (
                (PARITY.subprocess, "Popen", self.launch),
                (PARITY.socket, "socket", self.socket),
                (PARITY.time, "monotonic", lambda: self.now),
                (PARITY.time, "sleep", self.advance)):
            self.stack.enter_context(patch.object(target, attribute, side_effect=value))
        self.stack.enter_context(patch.object(
            PARITY.tempfile, "mkstemp", side_effect=lambda **kwargs: mkstemp(
                **kwargs, dir=self.directory
            )
        ))
        for attribute, value in (("CONNECT_TIMEOUT", 0.5), ("QMP_TIMEOUT", 0.5),
                                 ("BOOT_TIMEOUT", 3), ("CMD_TIMEOUT", 0.5),
                                 ("QMP_MESSAGE_MAX", 64)):
            self.stack.enter_context(patch.object(PARITY, attribute, value))
        self.stack.enter_context(contextlib.redirect_stderr(self.errors))
        return self

    def __exit__(self, *args):
        return self.stack.__exit__(*args)

    def advance(self, seconds: float) -> None:
        self.now += seconds

    def socket(self, *args):
        connection = ScriptedSocket(self)
        self.sockets.append(connection)
        return connection

    def launch(self, command, **kwargs):
        endpoint = command[command.index("-qmp") + 1]
        self.test.assertTrue(endpoint.startswith("unix:"))
        self.endpoints.append(Path(endpoint[5:].split(",")[0]))
        log = Path(command[command.index("-serial") + 1][5:])
        self.logs.append(log)
        self.stderr_files.append(kwargs["stderr"])
        log.write_bytes(b"" if self.phase == "boot" else b"boot\n> ")
        if self.phase == "launch":
            raise FileNotFoundError("missing QEMU")
        process = Mock()
        process.poll.return_value = 1 if self.phase == "exit" else None
        process.wait.return_value = 0
        self.processes.append(process)
        return process

    def assert_released(self) -> None:
        for connection in self.sockets:
            self.test.assertTrue(connection.closed)
        for stream in self.stderr_files:
            self.test.assertTrue(stream.closed)
        for endpoint in self.endpoints:
            self.test.assertFalse(endpoint.parent.exists())
        for process in self.processes:
            if self.phase == "exit":
                process.terminate.assert_not_called()
            else:
                process.terminate.assert_called_once()
            process.wait.assert_called_once_with(timeout=5)


class QemuConsoleTest(unittest.TestCase):
    def test_private_sessions_protocol_and_startup_handshake(self) -> None:
        with Harness(self, dropped_enters=1) as harness:
            with PARITY.Console("image-one", False) as first, \
                    PARITY.Console("image-two", True) as second:
                self.assertNotEqual(harness.endpoints[0], harness.endpoints[1])
                self.assertNotEqual(first.log_path, second.log_path)
                self.assertEqual([s.enters for s in harness.sockets], [2, 2])
                for connection in harness.sockets:
                    self.assertEqual([m["id"] for m in connection.requests], [1, 2, 3])
                first.type("a가A")
                strokes = harness.sockets[0].requests[3:]
                pressed = [[e["data"]["key"]["data"] for e in m["arguments"]["events"]
                            if e["data"]["down"]] for m in strokes]
                self.assertEqual(pressed, [["shift", "spc"], ["a"], ["shift", "spc"],
                                           ["r"], ["k"], ["shift", "spc"], ["shift", "a"]])
                events = strokes[0]["arguments"]["events"]
                self.assertEqual([(e["data"]["key"]["data"], e["data"]["down"])
                                  for e in events],
                                 [("shift", True), ("spc", True), ("spc", False), ("shift", False)])
            first.close()
            second.close()
            harness.assert_released()
            # Diagnostics persist, but all parent descriptors are closed.
            self.assertTrue(all(path.exists() for path in harness.logs))

    def test_constructor_failures_release_all_acquired_resources(self) -> None:
        phases = {
            "launch": FileNotFoundError, "exit": RuntimeError, "connect": TimeoutError,
            "permission": PermissionError, "greeting-timeout": TimeoutError,
            "eof": RuntimeError, "json": RuntimeError, "greeting": RuntimeError,
            "non-object": RuntimeError, "oversize": RuntimeError,
            "send": TimeoutError, "error": RuntimeError, "wrong-id": RuntimeError,
            "events": TimeoutError, "partial": TimeoutError, "boot": TimeoutError,
            "input": TimeoutError, "interrupt": KeyboardInterrupt,
        }
        for phase, error in phases.items():
            with self.subTest(phase=phase), Harness(self, phase) as harness:
                with self.assertRaises(error):
                    PARITY.Console("image", False)
                harness.assert_released()
                self.assertIn("QEMU logs:", harness.errors.getvalue())

    def test_stale_prompt_and_dead_process_do_not_complete_commands(self) -> None:
        with Harness(self) as harness:
            with PARITY.Console("image", False) as console:
                with patch.object(console, "type"), patch.object(console, "_send"):
                    with self.assertRaises(TimeoutError):
                        console.run("help")
                harness.processes[0].poll.return_value = 1
                with self.assertRaisesRegex(RuntimeError, "QEMU exited"):
                    console._wait(PARITY.PROMPT, 1, "dead process")
            harness.processes[0].terminate.assert_not_called()
            harness.processes[0].wait.assert_called_once_with(timeout=5)
            self.assertTrue(harness.sockets[0].closed)

    def test_os_wait_does_not_accept_interpreter_prompts(self) -> None:
        with Harness(self) as harness:
            with PARITY.Console("image", False) as console:
                for prompt in (">> ", ">>> ", ">  ", ".. "):
                    with self.subTest(prompt=prompt):
                        harness.logs[0].write_text("command\n" + prompt)
                        with self.assertRaises(TimeoutError):
                            console._wait_after(0, "> ", 0.5, "OS prompt")
            harness.assert_released()

    def test_interrupt_waits_for_double_greater_mowkow_prompt(self) -> None:
        with Harness(self) as harness:
            with PARITY.Console("image", False) as console:
                def send(keys):
                    with harness.logs[0].open("ab") as log:
                        log.write("머꼬\n>> ".encode() if keys == ["ret"] else b"\n> ")

                with patch.object(console, "type"), \
                        patch.object(console, "_send", side_effect=send) as strokes:
                    output = console.interrupt("머꼬", ["ctrl", "c"])
                self.assertTrue(output.endswith("\n>> \n> "))
                self.assertEqual([call.args[0] for call in strokes.call_args_list],
                                 [["ret"], ["ctrl", "c"]])
            harness.assert_released()

    def test_cleanup_kills_after_timeout_and_is_idempotent(self) -> None:
        with Harness(self) as harness:
            console = PARITY.Console("image", False)
            process = harness.processes[0]
            process.wait.side_effect = [subprocess.TimeoutExpired("qemu", 5), 0]
            console.close()
            console.close()
            process.terminate.assert_called_once()
            process.kill.assert_called_once()
            self.assertEqual(process.wait.call_count, 2)
            self.assertTrue(harness.sockets[0].closed)
            self.assertTrue(harness.stderr_files[0].closed)
            self.assertFalse(harness.endpoints[0].parent.exists())

    def test_cleanup_failure_does_not_mask_body_exception(self) -> None:
        with Harness(self) as harness:
            console = PARITY.Console("image", False)
            process = harness.processes[0]
            process.wait.side_effect = subprocess.TimeoutExpired("qemu", 5)
            with self.assertRaisesRegex(ValueError, "original failure"):
                with console:
                    raise ValueError("original failure")
            process.kill.assert_called_once()
            self.assertTrue(harness.sockets[0].closed)
            self.assertTrue(harness.stderr_files[0].closed)
            self.assertFalse(harness.endpoints[0].parent.exists())
            self.assertIn("QEMU cleanup failed", harness.errors.getvalue())
            process.wait.side_effect = None
            console.close()

    def test_interrupt_during_reaping_still_releases_resources(self) -> None:
        with Harness(self) as harness:
            console = PARITY.Console("image", False)
            process = harness.processes[0]
            process.wait.side_effect = [KeyboardInterrupt(), 0]
            with self.assertRaises(KeyboardInterrupt):
                console.close()
            process.kill.assert_called_once()
            self.assertTrue(harness.sockets[0].closed)
            self.assertTrue(harness.stderr_files[0].closed)
            self.assertFalse(harness.endpoints[0].parent.exists())
            console.close()

    def test_resource_close_failure_still_closes_other_resources(self) -> None:
        with Harness(self) as harness:
            console = PARITY.Console("image", False)
            with patch.object(harness.sockets[0], "close", side_effect=OSError("close")):
                with self.assertRaisesRegex(RuntimeError, "QEMU cleanup failed"):
                    console.close()
            self.assertTrue(harness.stderr_files[0].closed)
            self.assertFalse(harness.endpoints[0].parent.exists())
            console.close()
            harness.assert_released()


class MowkowOutputTest(unittest.TestCase):
    def test_exact_prompts_preserve_greater_and_dot_values(self) -> None:
        expected = [">", ">>", ">=", "..", "...", ">value", "..value"]
        host = ">  >\n>  >>\n>  >=\n>  ..\n>  ...\n>  >value\n>  ..value\n>  "
        device = (
            "머꼬\n>> '>\n>\n>> '>>\n>>\n>> '>=\n>=\n>> '..\n..\n"
            ">> '...\n...\n>> '>value\n>value\n>> '..value\n..value\n> "
        )
        self.assertEqual(PARITY.strip_host(host, repl=True), expected)
        self.assertEqual(PARITY.strip_device(
            device, "머꼬", ["'>", "'>>", "'>=", "'..", "'...", "'>value", "'..value"]
        ), expected)
        # File output has no REPL prompts, even if its text resembles one.
        file_output = "> text\n>  text\n>> text\n.. text\n> \n"
        expected_file = ["> text", ">  text", ">> text", ".. text", ">"]
        self.assertEqual(PARITY.strip_device("머꼬 file.mk\n" + file_output + "> ",
                                            "file.mk", []), expected_file)
        self.assertEqual(PARITY.strip_host(file_output), expected_file)

    def test_raw_prompt_check_rejects_ui_changes_even_when_values_match(self) -> None:
        host = ">  10\n>  ..  3\n>  >\n>  goodbye\n"
        device = "머꼬\n>> 0육ㄱ\n10\n>> (+ 1\n..  2)\n3\n>> '>\n>\n>> \ngoodbye\n> "
        self.assertTrue(PARITY.check_repl_prompts(device, host))
        mutations = (
            device.replace(">> ", ">  "),
            device.replace("..  2)", ">> 2)"),
            device.replace(">> (+ 1", "(+ 1"),
            device.replace("\n>> \n", "\n"),
            device.replace("\n>\n", "\n"),
        )
        for changed in mutations:
            with self.subTest(raw=changed):
                self.assertFalse(PARITY.check_repl_prompts(changed, host))
        self.assertFalse(PARITY.check_repl_prompts(device, host.replace(">  ", ">> ")))
        inputs = ["0육ㄱ", "(+ 1", "2)", "'>", ""]
        self.assertEqual(PARITY.strip_device(device, "머꼬", inputs),
                         ["10", "3", ">", "goodbye"])
        self.assertEqual(PARITY.strip_host(host, repl=True), ["10", "3", ">", "goodbye"])
        self.assertEqual(PARITY.strip_device(mutations[2], "머꼬", inputs),
                         ["10", "3", ">", "goodbye"])


if __name__ == "__main__":
    unittest.main()
