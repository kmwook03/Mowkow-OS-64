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
