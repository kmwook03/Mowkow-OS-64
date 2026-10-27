# 64-bit host fixtures

Run `make test64-host` from the repository root. Native runners and image files
are created in `TemporaryDirectory` scopes, including cleanup on assertion or
compiler failure. Python bytecode, `build64/`, `img64/` and root `img/` outputs
are ignored by Git; diagnostic QEMU logs intentionally remain under `/tmp`.

`storage_fixture64.py` packs sparse FAT32 volumes and ELF64 files independently
of `mkfat32_64.py` and kernel helpers. `test_storage_fault64.py` supplies damaged
BPB geometry, root/file FAT chains, VFAT sequences and ELF headers/segments to
`storage_fault64_host.c`, which runs the production block, cache, FAT32, UTF and
ELF implementations. Existing focused mount/chain/directory/ELF runners retain
their finer-grained unit and architecture checks.

The reusable `block_fault64.h` backend implements the `BLOCK64_OPS` 0/-1
transfer contract over disposable memory. A fault selects read/write, a sector
contained in the request, the byte prefix transferred before failure, and
one-shot or persistent failure. Its bounded trace records actual transport
calls, transferred bytes and results; overflow fails the integration runner.
It is compiled only by host tests and is never a production transport.

The integration cases check failed BPB/FAT/directory/data reads and retry,
untouched caller bytes after a partial read, data → both FAT copies → directory
writeback order, and failure at each writeback block. Persistent failures must
retain dirty cache bytes, stop later metadata, and reject filesystem mutations.
Clearing the transport fault allows explicit sync without clearing read-only
state. On-media bytes and call order are checked independently of filesystem
read helpers. Bad ELF and short I/O also check allocator and image-owner cleanup
through repeated loader calls. The valid ELF control validates ranges without
writing the host's reserved guest address window.

These fixtures do not emulate controller timing, power loss or journaling.
Use `make test64-qemu` and `make test64-qemu-ahci` for the real x86 transports;
Raspberry Pi 5 results are recorded separately in the project context.

`test_nano_compat64.py` exercises the private `64bit/nano64/` stream and UTF
foundation with ASan/UBSan. It substitutes native syscall gates while keeping
the real CRT wrappers. It checks short I/O, EOF/error separation, element counts,
overflow, stream exhaustion before truncation, close ownership/retry and strict
UTF conversion. Display widths follow the current OS renderer, including its
one-cell fallback for unsupported Unicode glyphs. The separate recipe audit
checks the shared inventory, direct header audit and strict flags for both
architectures. `make nano64-compat` performs the actual cross-compilation and
is included in `check64`. These archives are not upstream nano executables;
the tests do not validate the upstream editing loop, OS TTY or storage hardware.

`test_nano_upstream64.py` now separately compiles the real upstream editor and
the private libc/POSIX/curses port with ASan/UBSan, using pinned sources and the
same shared source inventory as both cross-builds. It uses the actual CRT I/O
wrappers, replacing native syscall gates and bottom-level allocation only.
It checks editing, Hangul/PREEDIT, undo/redo, search/replace, resize, rendering
bounds, allocation faults and termination. Actual upstream open/save now uses
checked metadata/read/sync and a 64KiB preflight. Read faults must not publish
partial editable buffers; write/sync/close failure retains modified/last_saved.
The production close wrapper observes retained ownership; fixture assignments
alone are not counted as checks. `make nano64-apps` builds standalone cross ELF
files and is included in `check64`; the existing image/staging editor is kept.

`make test64-nano-storage` and `make test64-nano-storage-ahci` separately exercise
those ELF files on disposable IDE/AHCI images through QMP keyboard events and
real SYS_TTY. They require QEMU and mtools mcopy. Full bytes, size and EOF are
compared after shutdown and a reboot/reopen/save cycle. New Hangul, existing
file edits, DOS, empty/no-final-newline files and the exact/oversized 64KiB
boundaries are covered. Logs survive temporary image cleanup. See
`docs/feature_p0_nano_storage.md` for contracts and actual results.

`test_feature_mp_audit64.py` checks configured branch exclusion, concatenated
format strings, preprocessor source locations, literal/character boundaries,
unparsed-input rejection and actual nm invocation parsing. The separate
`feature_mp_audit64.py --check docs/feature_p0_mp_catalog.json` command requires
both built OS targets and inventories their configured error text and exception
symbols. `feature_probe64.py --only micropython` additionally runs the real-core
negative boundary/lifetime/overflow/fully-exhausted-heap probes. Expected SIGABRT
on the prototype's 65th record is checked explicitly with child core dumps off.
These manual P0 probes are not OS runtime acceptance or automatic check64 steps;
see `docs/feature_p0_micropython_audit.md` for their limitations.

The same manual probe also builds an independent `mp-lifetime-probe64` for
no-surrogate message rendering, 256 live exceptions, GC release/reuse, actual
heap-lock/free=0 allocation failures and reentrant language capture. It reproduces
failed constructor pointer identity and stale emergency provenance; these are
design failures, not accepted translation behavior. Coverage tests expose all
pending active message IDs and never equate format coverage with constructor
acceptance. See `docs/feature_p0_mp_lifetime.md`; OS/REPL integration remains open.

`test_feature_mp_boundary64.py` compiles and transforms a real C object, then
links/runs a strong wrapper to verify both a function slot and an internal call
observe the public wrapper. It also covers invalid symbols, in-place rejection
and empty `.text` with nonempty function sections. The manual MicroPython probe
additionally rebuilds isolated objexcept/runtime objects for both architectures,
checks all executable sections and relocations, and runs real host-core
constructor/errno/filename/recursion/user/subclass/GC/emergency-generation cases.
The production OS build/link is unchanged; AArch64 has static checks only.
See `docs/feature_p0_mp_boundary.md` for the alias/weak-definition experiment.

`test_feature_mp_origin64.py` exercises the actual compact C store with 65,536
records plus an emergency slot, full-width values, no eviction, invalid spans,
alignment/capacity/overlap checks and failure preservation. The manual probe
also runs a compact strong-wrapper ELF through actual partial/fragmented GC
exhaustion, lost tuples, truncated original messages and typed argument lifetime
cases. Lost uninterned strings are explicit unresolved failures; the compact
candidate does not imply complete translation. See `docs/feature_p0_mp_compact.md`.
