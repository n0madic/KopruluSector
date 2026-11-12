# Koprulu container (`.kpd`, `.kpm`)

All Koprulu binary data files share one container. Writer: `tools/research/scdata/kpf/container.py`;
runtime reader: `native/kp_core/src/core/kp_kpf.c`.
Round-trip tests: `tools/research/scdata/tests/test_kpf.py` and `tests/c/test_data.c`, which read the committed
fixtures `tests/fixtures/kpf/` and their `expect.json` (synthetic files the Python converter's `fixtures.py` wrote before
[ADR 0019](../adr/0019-install-or-repack-converter-in-c.md)'s stage C7 removed it; `large.kpd`, 8 MiB, is built by
`test_data.c` itself).

All integers are little-endian. Offsets are from the start of the file.

## Header (128 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `KPRF` |
| 4 | 4 | file kind (FourCC, e.g. `DAT `, `MISN`) |
| 8 | 2 | container version (currently 1) |
| 10 | 2 | header size (128) |
| 12 | 2 | section entry size (48) |
| 14 | 2 | section count |
| 16 | 4 | schema version of the file kind |
| 20 | 4 | converter version |
| 24 | 16 | profile id, ASCII, NUL-padded (e.g. `bw-1.17.0.1`) |
| 40 | 32 | source data hash (SHA-256 of the converter inputs) |
| 72 | 32 | provenance hash |
| 104 | 4 | section table offset |
| 108 | 4 | file size |
| 112 | 16 | reserved, zero |

## Section entry (48 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | FourCC, unique within the file |
| 4 | 1 | class: 0 canonical, 1 derived |
| 5 | 1 | reserved, zero |
| 6 | 2 | flags (meaning defined per section) |
| 8 | 4 | payload offset, multiple of 4 |
| 12 | 4 | payload size |
| 16 | 4 | section schema version |
| 20 | 4 | CRC-32 of the payload (IEEE, as `zlib.crc32`) |
| 24 | 4 | derived: producer algorithm id (FourCC); canonical: zero |
| 28 | 4 | derived: producer algorithm version; canonical: zero |
| 32 | 16 | derived: first 16 bytes of the SHA-256 of the producer inputs; canonical: zero |

Payloads follow the table in entry order, each aligned to 4 bytes with zero padding; the
file size is padded to a multiple of 4. There is no internal compression (the pack pages are PNG inside their
sections).

## Validation at load time

`kpf.parse` rejects a bad magic, an unsupported container version, a file whose size
differs from the header, an out-of-bounds or misaligned section and duplicate FourCCs.
The payload CRC is checked only when requested (`verify_crc`), because hashing a large
mission on every load is costly; tests and tools enable it.

`Container:check(expect)` compares the file with what the running code expects: file
kind, schema version, profile id, the source hash shared by all files of one data set,
per-section schema versions and, for derived sections, the producer algorithm id and
version. A mismatch raises an error that names the file and says the data must be
regenerated, instead of failing later in unrelated code.
