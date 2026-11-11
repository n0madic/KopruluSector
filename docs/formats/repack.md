# The repack (`koprulu*.mpq`)

[ADR 0018](../adr/0018-original-data-at-run-time.md), stage D8. `python3 -m converter repack` (`converter/repack.py`) writes the files the game reads from an install, in their original formats, into MPQ archives; `kp_res` (`native/kp_app/kp_res.c`) reads them before an install in `--data`.

## The archives

MPQ version 1, written by StormLib, with a `(listfile)` and no `(attributes)` (StormLib reads and writes its 64-bit file times at unaligned addresses). Each file is compressed with zlib on its own, except an Ogg Vorbis and a Smacker movie, which are stored as they are.

| File | Holds |
|---|---|
| `koprulu.mpq` | everything but the cinematics (`--single`: the computers' bundles) |
| `koprulu-core.mpq` | what the program reads when it starts: the DAT, the scripts, the interface, the fonts, the palettes, the tilesets, every GRP (grpmeta reads their frames), every portrait, the sounds |
| `koprulu-<campaign>-<mission>.mpq` | a mission directory's files, `campaign\<campaign>\<mission>\` (its CHK and speech): 68 |
| `koprulu-music.mpq` | `music\` |
| `koprulu-cinematics.mpq` | `smk\`: the 1.17 install's 28 movies (Remastered's WebM are not repacked); apart from `koprulu.mpq` with `--single` too |

A group with no file is not written. `--out` defaults to `generated/repack`, with `--single` to `generated/repack-single`: the folders a bundle stages (`tools/product.sh`, under its data's `generated/`) and the app looks in when no `--data` names a source. A bundle carries no cinematics' group (the cinematics packs show the movies). The files are the input manifest's (`defs/scr/inputs-<profile>.json`) but its absent and unreadable names, in its sorted order, then the 1.17 cinematics, then the names a `--res-log` of a run read unlisted; a file the source lacks is left out and reported.

## The listing `(koprulu)`

Each archive holds one, in UTF-8, a line each:

```
koprulu repack 2
profile <profile id>
source <1.17|remastered>
group <file> <prefix>
<sha256> <size> <status> <name>
<sha256> <size> <status> ogg <wav sha256> <wav size> <wav header> <name>
```

- `group` lines are the core's only: a group's file and the normalized prefix of its names (lower case, `/`). `kp_res` adds a group lying beside the core when it opens it; `kp_res_group_wanted` names the group of a name while it is not open (the web fetches it, then `kp_res_add_repack`).
- `<sha256>` and `<size>` are the stored bytes'. `<status>` is the source's: `exact`, `derived`, `differs`, `equivalent`, `colour-equal`, `approximation` (Remastered's statuses, `converter/casc/source.py`), `unlisted` (a name the manifest does not hold). `<name>` is the source's spelling, `\` separated.
- `ogg`: a WAV stored as Ogg Vorbis (oggenc at the packs' quality, the CRC-32 of the normalized name as the stream's serial), done only when the Ogg is smaller and the WAV's header, up to its data chunk, fits in 4,096 bytes. The WAV's digest, size and header (hexadecimal) follow: `kp_res_head` answers them for the WAV (the game computes the sounds' durations from the header), `kp_res_read` gives the Ogg (SDL3_mixer tells the formats apart by their bytes).

## Android: the groups read in place

An Android bundle carries its groups in the APK's assets, `generated/repack/` (`tools/product.sh`), stored uncompressed (`tools/native/android_app.sh`'s `noCompress`). The app reads them in place (`kp_app_files.c` `apk_repack_file`, `KpResConfig.repack_file`): `AAsset_openFileDescriptor64` gives a group as the APK's descriptor, the group's offset and its length, and StormLib opens the archive by the name `fd:N:OFFSET:LENGTH` (`kp_mpq_slice_name`; its POSIX file streams patched for that, `tools/toolchain/patches/StormLib-9.40/`), through a descriptor of its own (`dup`) read with `pread`. The core is asked for first, then each group its listing names; a group the APK does not hold (the cinematics') is not open. A group stored compressed is refused with its name: the core then leaves no repack. Until 2026-11-11 the app copied the groups out of the APK into its preference path (`repack.txt`, its SHA-256 checked on the copy): 140 MB more on the phone.

## Checks

`kp_res` refuses another magic, another profile or no profile line (the install is read instead), before any line of the listing applies: a listing refused adds neither its files nor the groups it names. A blank line is skipped. A file listed `exact` or `derived` whose digest (an Ogg's: its WAV's) and size are not the manifest's is dropped from the table and logged, and the install's copy is read; a file's size is checked against its listing when read, a copy of another size read from the install. Its bytes are not hashed when read: the converter wrote the repack from files it checked, Android's groups are read from its signed APK (above), the web fetches a group whole. A line that is not a file's is counted and logged.
