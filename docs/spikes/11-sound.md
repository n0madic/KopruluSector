# Spike 11: sound

Plan: phase 0 item 11, §2 (Sound), §2.2, §4.2 (`sounds.kpd`), §5.1.

## Method

`python3 -m converter.emit.spikes --data <app> sound` (`converter/emit/sound.py`, RIFF reader `converter/formats/wav.py` following the native walker without odd-chunk padding) writes the sound data and a manifest. It reads the race sounds from `sfxdata.dat`/`sfxdata.tbl` and converts each WAV to OGG with `oggenc -q 3`. For comparison it also keeps the original WAV bytes. Total conversion time: 6.8 s.

| Pack | Sounds | Source | Size | Total duration |
|---|---|---|---|---|
| `terran_ogg` | all 375 `sound\terran\…` | WAV → OGG | 5.37 MB | 737.8 s |
| `terran_wav` | same 375 | original WAV | 32.7 MB (PCM 22 kHz 16-bit) | 737.8 s |
| `mission_ogg` | 8 WAVs of `campaign\terran\terran01` (from [campaign-media](../research/evidence/campaign-media.json)) | WAV → OGG | 456 KB | — |
| `music_ogg` | `music\terran1.wav` (8.9 min) | WAV → OGG | 3.82 MB | 533 s |

`sounds.json` carries the native duration fields (data length, `nAvgBytesPerSec`).

The spike (`spikes/11-sound`) loads the packs as follows:

- **Race packs:** collection factories with "Load Dynamically" ([ADR 0004](../adr/0004-resource-packs.md)).
- **Mission speech and music:** collection proxies.

It records process memory before and after each load. It plays up to 12 sounds of ≤ 5 s one after another and compares the time from `sound.play` to the completion callback with the WAV duration. It also starts 8 sounds in one frame and times the calls.

The spike first ran twice: with `sound.stream_enabled = 1` and an 8 MB `stream_cache_size`, and with streaming off. It now inherits the `[sound]` settings of `game.project`, where streaming is off. The streaming results below are historical.

## Results

Raw logs are in `build/spikes/11-sound/`:
- `{macos,web,android-emulator}.log` and `build/spikes/11-sound-nostream/macos.log`;
- for the Galaxy Z Fold7: `android-device{,2}.log` (streaming), `android-device-nostream.log` and `android-device-product.log` (the `game.project` settings).

**Memory**: RSS increase on load, KB.

| | `terran_ogg` | `terran_wav` | mission speech | music load |
|---|---|---|---|---|
| macOS, no streaming | +6,864 | +37,616 | +32 | +7,536 |
| macOS, streaming | +6,448 | +4,608 | +208 | +464 |
| Android emulator, streaming | +15,872 | +27,836 | +640 | ~0 |
| Galaxy Z Fold7, no streaming (product settings) | +18,304 | +65,424 | +384 | +7,576 |

- Without streaming, a pack holds its encoded files (OGG ≈ 6.9 MB, WAV ≈ 37 MB for one race).
- With streaming, the music costs 0.46 MB instead of 7.5 MB. Effects keep roughly a preload chunk per sound: 375 sounds × 16 KB ≈ 6 MB.
- RSS does not fall after unload on any platform (allocators keep freed memory). Sound resource release is therefore not shown by these numbers.

**Load and unload times** (ms):

| | `terran_ogg` CF load | `terran_wav` CF load | mission proxy load / unload | music proxy load / unload |
|---|---|---|---|---|
| macOS | 60.5–63.9 | 64.3–64.7 | 16.9–18.0 / 7.4–9.1 | 16.6–17.9 / 7.8–9.0 |
| Web (WebGL 2, headless Chrome) | 19 | 18 | 20 / 16 | 22 / 17 |
| Android emulator | 100.2 | 35.5 | 33.7 / 16.7 | 34.0 / 17.3 |
| Galaxy Z Fold7, no streaming | 21.7–25.7 | 55.8–56.0 | 17.7–17.8 / 14.3–18.6 | 23.0–24.0 / 7.5–9.6 |

**Play latency.**
- `sound.play` costs 0.01–0.03 ms per call (0.017–0.040 ms on the phone); 8 sounds in one frame take 0.008–0.077 ms in total.
- Wall time from play to the completion callback, minus the WAV duration:
  - macOS: −21…+20 ms (effects), −2…+52 ms (speech);
  - web: −50…−1 ms;
  - Android emulator: −118…−4 ms;
  - Galaxy Z Fold7: −15…+26 ms.

  The spread is frame quantization of the callback plus mixer buffering. It is not output latency: measuring that needs a device and a loopback.

**Streaming fails on a real device.** On a Galaxy Z Fold7 (Android 16) every run with `stream_enabled = 1` fails to load the 375-sound OGG pack.
- The same **25 resources** fail every run: "Failed to detect sound type". Only the first one reported differs between runs, because loading is asynchronous.
- Spawning the pack then aborts the engine in `CollectionSpawnFromDescInternal`.

Root cause, from the APK's `game.dmanifest` and the Defold 1.13.2 source:
- The 25 failing sounds are exactly the entries bob stored **LZ4-compressed** in the archive (flags 9, `compressed_size` set). All 350 sounds that load are stored uncompressed.
- With streaming, `LoadResourceToBufferWithOffset` (`engine/resource/src/resource.cpp`) reads the preload chunk with `ReadResourcePartial`. Its own comment says: "no decryption or decompressing is done on these resources".
- `ResSoundDataCreate` therefore receives compressed bytes, finds no `OggS`/`RIFF` header and rejects the resource.

This is a Defold defect: streaming plus a compressed archive entry. Why the emulator's archive loaded was not established.

Fix: `sound.stream_enabled = 0` in `game.project`. With those product settings every pack loads and plays on the phone.

**Engine limits found:**
- With the default `sound.max_sound_data = 128` the 375-sound pack fails to load ("Out of sound data slots"), and the next `sound.play` crashed the engine.
- With streaming and the default stream cache, playback fails with "Cannot fit sound data chunk of size 16384". One WAV failed to decode.
- `game.project` sets `max_sound_data = 2048`, `max_sound_instances = 1024` and `max_component_count = 2048`. The streaming runs also used an 8 MB stream cache.

## Conclusion

- **Format.** Ship effects and speech as OGG: 6× smaller archive and memory than WAV, same load time. Keep the source data length and `nAvgBytesPerSec` in `sounds.kpd` for native durations, as the manifest does here.
- **Streaming: off** (`sound.stream_enabled = 0` in `game.project`). Streamed resources skip archive decompression, so streaming breaks every compressed sound (above), and the setting is global.
  - The cost is memory: one music track stays resident (≈ 7.5 MB for 9 minutes) instead of 0.46 MB.
  - Revisit if Defold fixes the partial read, or if bob gains a way to store sounds uncompressed.
- **Packs.** Race sounds belong in the race pack (collection factory). Mission speech goes in the mission assets proxy: loads in ≤ 34 ms on the emulator. `max_sound_data` must cover all simultaneously loaded sound resources (ADR 0004).
- **Channels.** The native 8-channel manager (plan §2) is not affected: starting 8 sounds per frame is negligible.

## Not measured

- Device audio output latency (needs real devices and a loopback).
- iOS.
- A mid-range Android device.
- Web memory: the wasm heap is fixed at 256 MB.
- Sound resource release after unload: RSS is unsuitable, and the Lua API has no sound resource lookup.
- Mixing load with many concurrent voices.
