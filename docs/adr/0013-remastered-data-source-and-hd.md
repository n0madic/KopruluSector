# ADR 0013: StarCraft: Remastered as the only data source, and its HD art

Status: accepted (2026-10-26). The user approved the plan (inline design document and plan file, 2026-10-26): import every resource the remake needs from a StarCraft: Remastered install alone, the original graphics as well as the HD art, so a player needs one distribution, not two; the HD art is drawn by an HD renderer; HD2 (×2) and HD (×4) are both imported and chosen by scale; enUS only for now; no Carbot skin. Evidence: [Remastered as a data source](../research/remastered.md). Amends `IMPLEMENTATION_PLAN.md` "Scope V1" (CASC/Remastered was outside V1).

## Context

The converter reads a verified Brood War 1.17.0.1 install (profile `bw-1.17.0.1`: StarCraft.exe and five MPQs) and every oracle checks the palette-indexed data it produces. Remastered's CASC storage keeps most of those files byte for byte, some under `SD/`, the enUS locale or another name, grows the tilesets by appending, and holds the rest only in its own formats (RGBA DDS, DXT `*.dds.grp`, WebM, XML and JSON). Its executable's code is encrypted. Measured on 1.23.10.13515 over the 4,376 files the conversion reads: see the research note's table.

## Decision

1. **Sources.** The conversion reads a `Source` (`converter/source.py`): the MPQ `Resolver` (`--data`) or `ScrSource` (`--scr`, `converter/casc/source.py`), which presents a Remastered install as profile `bw-1.17.0.1`'s files under their MPQ paths. No conversion step reads the executable: its requirement tables and the digest of its VCOD resource are in `defs/` (`defs/dat/requirements-*.json` `words`, `defs/chk/*.json` `vcod_resource.sha256`), checked against the executable by `test_dat` and `test_chk`.
2. **The manifest.** `defs/scr/inputs-<profile>.json` lists every file the conversion reads from the 1.17 install with its size and SHA-256, the names it looks for and does not find (`absent`) and the files it cannot read (`unreadable`): hashes and names only. `python3 -m converter scr-manifest --data DIR` runs the whole conversion over a recording source and writes it.
3. **Lookup and status.** `ScrSource` tries, for an MPQ path, the moved-files table, then the prefixes `SD/`, the root, `locales/enUS/Assets/SD/`, `locales/enUS/Assets/`, and rebuilds what is derivable (a grown tileset's prefix of the manifest's size, with the CV5 groups Remastered filled in emptied again; `.vx4` from `.vx4ex`). Each file gets a status, written to `reports/scr-import.json`: `exact` (a copy has the manifest's hash), `derived` (rebuilt to it), `differs` (no candidate reaches it; the first copy is used), `unlisted`, `unreadable`, `missing`. A name absent or unreadable in 1.17 stays so, so the conversion sees 1.17's data set.
4. **What 1.17 alone has.** Where Remastered has no exact copy the SC:R build uses Remastered's data as an `approximation` (plan §8): portraits and glue movies (lossy WebM), strings Remastered edited, frames kept only in DXT, the music and mission speech it re-recorded. Data that is not art, text or audio (the dialogs' numeric structure, string id order) may go into `defs/` like the interface tables. The 1.17 install stays the reference of every oracle and golden.
5. **HD art.** Imported from the user's install only, never shipped: `.anim` sprites (diffuse, team colour mask) and HD terrain (one tile per megatile) for HD2 and HD, drawn by an RGBA world pass at the window's resolution under the palette-indexed interface (`--graphics original|hd|auto`). The simulation and the index oracles do not change; HD is an `approximation` (ADR 0011 point 5).

## Stages

- **R0** (2026-10-26): exe-free conversion, `Source`, `ScrSource` with the exact, moved and tileset files, the manifest, `reports/scr-import.json`, `compare_outputs` (containers of two sources section by section). On 1.23.10.13515: 2,823 exact and 29 derived of 4,376; `missions --scr` gives the 64 mission containers with the same sections as from 1.17.
- **R1**: reconstructions (glue and console PCX from RGBA DDS, TBL by id, dialogs, DXT GRPs, SMK frames from WebM, audio), every command from `--scr` alone.
- **R2**: HD import. **R3**: the HD renderer (terrain, images). **R4**: HD interface, water masks, web HD2.

## Consequences

- One install is enough; with both, the 1.17 one stays the reference and the manifest shows exactly where the SC:R build departs from it.
- A new Remastered build is checked by the manifest, not trusted: a changed file shows as `differs`.
- The converter needs ffmpeg for Remastered's WebM, as for SMK; the runtime gains no dependency.
