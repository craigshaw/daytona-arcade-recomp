# Original panorama milestone

All three courses in Revision A and Deluxe '93, automatic with widescreen in
the playable game. The complete original
sky is decoded from the user's imported ROM; no extracted PNG or replacement
artwork is required. Original aspect remains the default and retains the
original rendering. Capture tools keep their explicit opt-in for comparisons.
This follows the design document's Renderer and Enhancements sections.

## Rendering

- Cache one course's 16-bit palette indices and tile category, plus small
  CPU source-validation records. Beginner is 2048x392 (1.53 MiB), Advanced
  2048x344 (1.34 MiB), and Expert 2048x432 (1.69 MiB), each on CPU and GPU.
  Course IDs are 0 / 2 / 1 respectively. The CPU cache replaces its previous
  course; the GPU allocation grows only as needed and reuses that capacity.
  Software composition also keeps a reusable 496x384 overlay buffer
  (0.73 MiB); the desktop GPU path does not allocate it.
- Upload the indices on the first supported frame and when the rendered
  course or game instance changes, alongside the existing tile pixmaps.
  The shared tile shader uses the current palette, so fades need
  no panorama re-upload and no additional rendering pass.
- Extend only the layer-2 source rows. Preserve live filler rows, layer-3
  vertical splits, window masks, overlay priorities, HUD and foreground.
  Source pixels retain their native size at both 16:9 and 32:9.
- Use the existing latched full camera phase. Before activating, check the
  revision-specific course descriptor, supported scroll state, visible streamed map columns
  and original character data. Loading or stale source states fall back.
- Original view, the unverified fourth source and unsupported states retain the existing
  renderer. The playable game selects the panorama from the aspect ratio,
  with no separate background setting. Diagnostic tools still allow stretching;
  original panoramas take precedence in supported scenes.

The first replay exposed a loading transition at frame 2580: the course
selector was ready before the sky tiles/palette. Checking the selector alone
showed wrongly coloured art. Live map/character matching now prevents this;
there is no frame-number delay or palette-colour heuristic.

## Try and reproduce

Build and launch the playable game, choose a widescreen aspect in
**Game > Enhancements**, then Start or Resume. All four widescreen choices
use panoramas automatically. No extra flag or build option is needed:

```powershell
cmake --build build-daytona --config Release --target daytona --parallel 4
build-daytona/Release/daytona.exe --rom roms/daytona.zip
```

For the existing Deluxe '93 build, use `build-daytona93` instead. Its importer
can use this checkout's complete merged `daytona.zip`, as described below.

Both capture tools accept `--panorama-original`; `--panorama-proof` remains
the separate temporary test image, and the two options cannot be combined.
The capture commands below retain the explicit option to compare with the
legacy renderer. They use this checkout's existing Revision A build and
saved cabinet settings. Run from the repository root:

```powershell
cmake --build build-daytona --config Release --target m2gpushot --parallel 4
New-Item -ItemType Directory -Force traces/original-sky-try | Out-Null
build-daytona/Release/m2gpushot.exe build-daytona/rom_cache/daytona 4200 --nvram traces/widescreen-32x9/nvram --inputs scripts/inputs/race_to_end.txt --aspect 32:9 --panorama-original --dump traces/original-sky-try --dump-from 4200 --every 4200
python scripts/rgb2png.py traces/original-sky-try/run_04200.rgb 1366 traces/original-sky-try/preview.png
Invoke-Item traces/original-sky-try/preview.png
```

The PNG converter uses only the Python standard library. To capture at 16:9,
change the aspect to `16:9` and the converter width to `682`.
For Advanced or Expert, replace the input replay with
`scripts/inputs/widescreen_advanced.txt` or `scripts/inputs/widescreen_expert.txt`.
The same `--panorama-original` option selects the course artwork automatically.

For the automated checks:

```powershell
cmake --build build-daytona --config Release --target m2run m2gpushot daytona test_panorama --parallel 4
ctest --test-dir build-daytona -C Release -R "^(panorama|scenery|app_config)$" --output-on-failure
python scripts/original_panorama_validate.py --nvram traces/widescreen-32x9/nvram --output traces/original-sky-repeat --bench
```

The validation runner needs NumPy and a fresh output directory. It freezes
cabinet settings and inputs, records commands/executable/ROM hashes, compares
pixels, then removes raw captures and per-frame logs after each job. Compact
results and selected PNGs remain. Optional `--baseline-tools DIRECTORY`
compares disabled rendering against saved pre-change executables.
Use `--course advanced` or `--course expert` for the other courses, each with
its own output directory. The default is Beginner. Each replay exercises all
four race cameras; consecutive loading and wrap checks follow that course's
observed activation and full-phase wrap rather than Beginner's frame numbers.
Use `--set daytona93` for Deluxe '93 (default build directory `build-daytona93`,
or pass `--build-dir`). The build's configured ROM set must match. The '93
Expert wrap control adds a right turn when the standard replay does not
cross that boundary. Loading checks exclude the earlier attract activation.

`--panorama-only` isolates the complete back-layer composite, including when
the extension is disabled, for exact software/GPU comparisons. It is a
capture diagnostic. `--sky-log FILE` records readiness, scroll and palette
state. Neither option changes guest RAM or gameplay.

## Initial Beginner checks — 4 October 2026

Windows / Direct3D 12, Revision A:

- 25 replay jobs, 1,049 captures, 18 comparison groups: all pass. The
  original central view matches in both aspects, all 100 sampled software/
  GPU backgrounds agree, and 16:9 is an exact crop of 32:9.
- Eighteen consecutive wraparound frames and 46 loading-transition frames
  preserve the original centre. All four race cameras are exercised.
  A separate per-frame log confirms uninterrupted activation/readiness across
  all 3,001 frames from 3000 through 6000, including the +2-texel full wrap.
- The 18,000-frame GPU replay and 6,000-frame software replay preserve the
  central view and inactive/menu output. Enhancement-off output matches the
  saved previous executables. Native view, 2x nearest-pixel sampling, HUD at
  the edges and Advanced/Expert fallbacks pass their controls.
- The ordinary replay exposed no active sky-colour fade. A separate paused
  scene fixture applies four palette levels (full, half, black, restored)
  to derived renderer pens without writing guest RAM. All eight stages over
  two game instances match CPU/GPU and the original centre exactly, retaining
  one panorama upload per instance. This validates palette integration;
  it is explicitly a synthetic fade, not a newly observed game transition.
- Release builds, panorama/scenery/app-config CTests and the five capture
  utility tests pass. The original-art gallery and compact local results
  are under ignored `traces/original-panorama/`.

Run the palette/cache fixture explicitly (it is excluded from ordinary builds):

```powershell
cmake --build build-daytona --config Release --target m2panoramacheck --parallel 4
build-daytona/Release/m2panoramacheck.exe build-daytona/rom_cache/daytona traces/widescreen-32x9/nvram scripts/inputs/race_to_end.txt scripts/inputs/widescreen_advanced.txt scripts/inputs/widescreen_expert.txt scripts/inputs/race_to_end.txt
```

For performance, two reverse-order runs per mode measured 3,000 frames
after 3,000 warm-up frames, without capture/log I/O. Unpinned runs showed
a large whole-system timing shift, so the repeat used the same four logical
CPUs (affinity mask 0xf, only for those child processes). Renderer submission
CPU time was 0.24/0.23 ms without the extension and 0.26/0.25 ms with it;
game time was 1.37/1.34 ms versus 1.37/1.39 ms. GPU wait time varied, so these
are host-side costs, not GPU timestamp measurements or a speedup claim.
Software-renderer performance was not separately benchmarked. Both timing
sets and commands remain in the local results.

## Advanced and Expert extension — 4 October 2026

The same opt-in path now supports all three race courses. Advanced and Expert
each pass 19 replay jobs / 889 captures / 14 comparison groups, covering both
16:9 and 32:9, exact CPU/GPU backgrounds, native centre preservation, four
cameras, loading, natural wraparound, HUD placement and 2x supersampling.
There are 621 Advanced and 624 Expert pixel comparisons, with no mismatches.
Both remain active throughout all 3,001 frames from 3000 through 6000.
The Beginner regression repeats 19 jobs / 889 captures / 14 groups with 621
comparisons passing; 795 captures at matching frames also agree byte-for-byte
with the previous milestone. Across the three courses this is 57 jobs,
2,667 captures and 1,866 comparisons, with no mismatches.

Each 18,000-frame GPU replay goes from the initial Beginner attract scene to
the selected course and back to Beginner, with exactly three uploads and
unchanged central pixels. The 6,000-frame software controls and native-view
controls pass too. The palette fixture reuses one GPU renderer across eight
game instances (Beginner, Advanced, Expert, Beginner, twice each): all 32
palette stages match CPU/GPU and the original centre, with one upload per
instance. This also exercises buffer growth and reuse for smaller skies.

Local evidence and a before/after gallery: `traces/course-panoramas/`.
Raw frames and per-frame logs are deleted after comparison; only compact
records and selected PNGs remain. Validation is against the existing renderer,
not a new MAME comparison; runtime GPU checks still cover Direct3D 12 only.

## Deluxe '93 support — 5 October 2026

The two revisions share all 24 panorama map sections and all character-upload
blocks byte-for-byte, including source addresses and heights. The source
tables remain 0x2074240 / 0x2081180 / 0x2074268 for Beginner / Advanced / Expert.
Only their descriptor locations and the observer's RAM addresses differ:

| Location | Revision A | Deluxe '93 |
| --- | --- | --- |
| Full sky phase (16-bit) | 0x5fe11a | 0x53e11a |
| Selected backdrop descriptor pointer | 0x5fe5e4 | 0x53e5d4 |
| Beginner / Expert / Advanced descriptors | 0x2600020 / 0x2600040 / 0x2600060 | 0x2800020 / 0x2800040 / 0x2800060 |
| Program's descriptor lookup table | 0x4770 | 0x3a48 |

'93 selects the descriptor at 0x39c0..0x39d0. Its sky update at
0x1c674..0x1c688 shifts the full phase by five bits, masks to 511 and writes
0x501308; 0x1a0d4/0x1a16c transfers that value to tile register 0x100a004.
The course ID remains at 0x501460. These were checked in the imported ROM,
not inferred from a uniform RAM relocation. Local address/hash evidence is
under ignored `traces/daytona93-panorama/mapping/`.

The runtime and read-only capture telemetry use `panorama_revision.h` for
these addresses. The decoder, cache and shaders are shared without extra
assets, memory or rendering passes. The temporary `--panorama-proof` artwork
remains Revision A-only; `--panorama-original` supports both revisions.

The supplied `daytona93.zip` is a split set. This run imported the complete
'93 set from the already verified merged `roms/daytona.zip`, using the '93
build's importer, into `build-daytona93/rom_cache/daytona93`. It did not change
either archive or the Revision A build's ROM cache. Both builds use the
existing Visual Studio 2022 ClangCL toolchain. The saved single-cabinet
settings and existing three course replays reach the expected '93 races.

Validation passed 64 replay jobs / 2,908 captures / 48 comparison groups /
2,105 pixel comparisons across the three courses. A separate Beginner race
loading control adds two jobs / 92 captures / 46 comparisons. It activates at
frame 2586; the first run had selected the earlier attract activation, which
the runner now excludes. Each course remains active for all 3,001 frames from
3000 through 6000 in both widescreen aspects. Coverage includes CPU/GPU skies,
unchanged original centres, 16:9 crops of 32:9, all four cameras, native view,
HUD placement, 2x sampling, stretch precedence and loading/wrap transitions.

The standard '93 Expert replay never crosses the full-phase boundary during
the race, so its initial validation stopped for missing coverage. The added
right-steer control crosses at frame 5311; all 18 consecutive wrap frames stay
active and preserve the original centre. Beginner and Advanced wrap at frames
3701 and 3615. The 18,000-frame Advanced/Expert controls traverse Beginner ->
selected course -> Beginner with exactly three uploads. Eight game instances
sharing one GPU renderer pass all 32 synthetic fade/restore stages, preserving
CPU/GPU equality, palette contents and one upload per instance.

Disabled '93 output matches the pre-change executables in all 240 sampled
full-frame GPU/software comparisons. A further 1,320 Revision A captures
across 18 jobs match the previous milestone byte-for-byte. Both Release game
and capture-tool builds pass, alongside the '93 panorama CTest, three Revision
A targeted CTests and five capture utility tests. No shaders changed in this
milestone; runtime GPU validation covers Direct3D 12. Compact hashes, address
evidence and an interactive before/after gallery are retained locally under
ignored `traces/daytona93-panorama/` after deleting raw captures and frame logs.

```powershell
python scripts/original_panorama_validate.py --set daytona93 --course beginner --nvram traces/widescreen-32x9/nvram --output traces/daytona93-repeat/beginner
python scripts/original_panorama_validate.py --set daytona93 --course advanced --nvram traces/widescreen-32x9/nvram --output traces/daytona93-repeat/advanced
python scripts/original_panorama_validate.py --set daytona93 --course expert --nvram traces/widescreen-32x9/nvram --output traces/daytona93-repeat/expert
```

## Automatic playable backgrounds — 6 October 2026

The desktop app applies one video policy before advancing a frame. A wider
viewport enables the original panorama; returning to original aspect disables
it while retaining the cache for reuse. Native startup allocates no panorama
artwork. Scene/source readiness still controls when the panorama is actually
drawn, including during loading and attract transitions. The stretch checkbox
and saved field are retired: either old value is ignored, other preferences
survive, and the obsolete key disappears on the next save. Capture switches
and their defaults are unchanged.

`m2panoramacheck --play` uses the app's actual settings function and an explicit
panorama control game with uninterrupted scroll tracking. Each revision runs
all three course replays for 18,000 frames, then restarts each for 3,600 frames
with the same GPU renderer. Both pass 4,842 pixel comparisons: automatic vs
explicit software backgrounds, CPU vs GPU backgrounds, and unchanged original
centres. The sequence covers all four widescreen ratios, native intervals,
software/hardware changes and frame-skip modes. All 64,800 frames per revision
match the control's readiness; each run performs ten necessary uploads across
courses and game instances, without re-uploading on aspect changes. Five
targeted CTests across the builds pass, including old-settings migration.

Desktop smoke checks in separate profiles confirm the updated launcher,
32:9 startup, original-aspect Resume, 16:9 Reset and legacy-key removal on save
in Revision A, plus saved 32:9 autostart in Deluxe '93. Normal profiles were
not changed. Full-race pixel comparisons come from the fixture above.

```powershell
cmake --build build-daytona --config Release --target daytona m2panoramacheck test_app_config --parallel 4
build-daytona/Release/m2panoramacheck.exe build-daytona/rom_cache/daytona traces/widescreen-32x9/nvram --play scripts/inputs/race_to_end.txt scripts/inputs/widescreen_advanced.txt scripts/inputs/widescreen_expert.txt
```

Use `build-daytona93` and `rom_cache/daytona93` for the other revision. Pixels
stay in memory; compact local logs are under `traces/automatic-panorama/`.
No runtime or shader changes were needed. These are Direct3D 12 integration
checks, not a new MAME-parity claim. Other GPU backends and the fourth ROM
source remain unverified.
