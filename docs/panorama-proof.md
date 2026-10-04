# Seamless panorama proof

This is an opt-in, one-course experiment on Revision A, following the
design document's Renderer and Enhancements sections. It tests a cached
panorama at 16:9 and 32:9 before producing replacement artwork. The test
sky is original code-generated ellipses and a gradient, not finished game
art or extracted Sega material. No new launcher option is enabled.

## Implementation

- One 2048x512 BGRA image, prepared once when the proof is enabled: 4 MiB
  in CPU memory and 4 MiB on the GPU after its first visible frame.
- The GPU reuses the existing textured-quad shader and background draw
  slot. It uploads the panorama once and updates twelve quad vertices
  (the ordinary layer quad and the panorama quad) each frame. No extra
  draw pass, scenery polygons, shader compiler or platform shader fork.
- Software rendering samples the same image. Horizontal sampling repeats;
  vertical sampling clamps and follows the game's signed scroll offset.
- The image keeps its scale as the aspect ratio widens; narrower outputs
  select the same central portion. Supersampling repeats those pixels.
- Scope: Revision A Beginner, widescreen, a recognised 3D scene and a
  validated normal/split-vertical sky state. Other courses and unsupported
  states fall back to the existing background. Original view is unchanged.

The prototype replaces the complete back-layer composite in eligible
scenes. Front-layer HUD and 3D remain on the existing path. Final artwork,
palette/fade integration and preserving any additional back-layer overlays
must be resolved before turning this into a general launcher feature.
This proof is deliberately not a production replacement for every screen.

## Camera and wraparound

Revision A's sky update at 0x1dfa4..0x1dfb8 shifts the camera-derived value
at RAM 0x5fe11a by five bits, masks it to 511 and writes 0x501308. The tile
register update at 0x1b9c0/0x1ba58 transfers that value to 0x100a004.
Retaining the full 16-bit value yields 2048 positions at the game's
existing horizontal scroll scale. It avoids four repetitions of a
512-pixel image without stretching each cloud.

Reading the live camera at capture time was rejected: in the measured
3000..5900 interval, 1,660 of 2,901 frame-end samples did not match the
displayed tile scroll. The camera can already describe the next frame.
The observer instead latches the full value when 0x501308 is written and
again when it reaches the tile register. The vblank snapshot validates the
low nine bits against the value actually written. It changes no game RAM,
registers, emulated timing or polygon selection. Invalid correspondence falls back.

The new replay also shows vertical split mode (0x2000) on layer 2 during
the Beginner race. Earlier notes describing normal-scroll mode alone do
not describe this Revision A replay. The proof follows the signed vertical
offset and uses a full-height sky with clamped edges; it does not reinterpret
the game's split-layer artwork as an already seamless panorama.

## Reproduce

Build the current Revision A capture tools, then run:

```powershell
cmake --build build-daytona --config Release --target m2run m2gpushot test_panorama --parallel 4
ctest --test-dir build-daytona -C Release -R "^(panorama|scenery|app_config)$" --output-on-failure
python scripts/panorama_proof.py --nvram traces/widescreen-32x9/nvram --output traces/panorama-proof-repeat --bench
```

Supply a working single-cabinet EEPROM and backup RAM. The runner freezes
them, records executable/ROM/input hashes and exact commands, and checks
capture dimensions. Existing output is reusable only with the same inputs
and executable. Images and raw game captures stay under ignored `traces/`.

Both capture tools accept:

- `--panorama-proof`: enable the one-course test sky.
- `--sky-log FILE`: record scroll state and prototype activation each frame.
- `--panorama-sweep`: move only the panorama through a complete cycle in
  128 frames; the car, camera and game logic are unchanged. This is a
  sampling stress test, not evidence about actual camera motion.
- `--panorama-only`: isolate the background to compare software/GPU pixels.

`m2gpushot --bench-from 3000` excludes boot and the first 3,000 warm-up
frames from its reported timings, while still running/rendering them.
Performance runs use no image readback or diagnostic logging, run
sequentially, and reverse mode order to reduce cold-start/order bias.

## Acceptance and limits

The proof checks real turns, all four race cameras, the original 512-pixel
scroll boundaries and full-panorama wraparound; it also isolates a full
synthetic rotation for exact software/GPU and aspect-crop comparisons.
Native-view controls, supersampling, menu fallback and texture upload counts
are checked separately. The texture cache does not establish preservation
of the original artwork's palette fades; that remains explicit follow-up.
Deluxe '93 remains deferred, and no new MAME-parity claim is made.

## Recorded results — 4 October 2026

Revision A Beginner, Windows / Direct3D 12 and software rendering:

- 28 capture jobs produced 1,022 raw frames. All 258 isolated software/GPU
  pairs match exactly across the two aspects and a complete panorama cycle.
  All 129 narrower-aspect crops match the centre of the 32:9 image exactly.
- The 2x GPU sky is an exact nearest-pixel replication of the software sky.
  Both native-view controls are identical with the proof enabled/disabled.
- All 120 default wide captures match the previous widescreen milestone's
  saved baseline. The 28 sampled inactive/menu captures match with the
  panorama enabled/disabled.
- Consecutive captures cover the actual phase wrap at frame 3694, each of
  the VR2/VR3/VR4 transitions and the repeated wrap at 5473..5475. VR1 is
  selected at 3200. The full-phase wrap moves by +2 texels at 3694 and by
  +3, -5, +4 at 5473..5475; it does not reset to a 512-pixel cloud repeat.
- All 3,001 frames in the 3000..6000 race interval use the panorama with
  valid phase correspondence, at both aspects and on both renderers.
  Hardware reports exactly one panorama upload per 9,000-frame run.
- Panorama, scenery and app-config CTests pass. Both capture tools and the
  normal application build in Release; the launcher receives no new setting.

Each performance run measures 3,000 frames after 3,000 rendered warm-up
frames. Two runs per mode, reverse order, no capture/log I/O:

| Aspect / mode | Total time, seconds | Renderer CPU, ms/frame | GPU wait, ms/frame |
| --- | --- | --- | --- |
| 16:9 default | 5.31 / 4.89 | 0.21 / 0.19 | 0.04 / 0.02 |
| 16:9 panorama | 4.88 / 4.83 | 0.19 / 0.20 | 0.02 / 0.02 |
| 32:9 default | 5.23 / 5.67 | 0.22 / 0.21 | 0.03 / 0.34 |
| 32:9 panorama | 5.13 / 5.14 | 0.21 / 0.21 | 0.02 / 0.02 |
| 32:9 stretch | 5.31 / 5.29 | 0.22 / 0.21 | 0.03 / 0.03 |

There is no material rendering overhead in these runs. These are whole
application timings and host-side submission/wait measurements, not GPU
timestamp queries. The second 32:9 default run has a wait-time outlier;
the results do not establish a speedup or performance on other devices.
The proof still decodes original tile layers, so their small existing cost
is included. Software performance was not separately benchmarked.

Local evidence: `traces/panorama-proof/validated/manifest.json`, per-job
commands, `checks.json`, `baseline-checks.json` and `benchmarks.json`.
The local `traces/panorama-proof/index.html` provides before/after images
and consecutive-frame playback. The 4 October disk cleanup retained those
viewer PNGs and compact results, but removed raw frames, frame logs and
other captures. Full validation must use a fresh output directory; the
pruned archive cannot be resumed or used as a complete raw baseline.
Captures remain untracked.

Follow-up extraction found complete original 2048-pixel panoramas: three
verified race sets plus a fourth ROM entry whose use is unconfirmed. See
[backdrop inventory](backdrop-inventory.md). The 512-pixel tilemap is a
streaming window, not the entire original asset. New artwork is therefore
not yet justified. Next: try the complete originals in the cache, integrate
palette fades and additional back-layer overlays, and validate alignment,
wraps and transitions before exposing a setting. The sampling/cache approach
is proven for the test fixture; original-art integration remains to be done.
