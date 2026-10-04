# Original panorama milestone

All three Revision A courses, opt-in capture tools. The complete original
sky is decoded from the user's imported ROM; no extracted PNG or replacement
artwork is required. The normal launcher and default rendering are unchanged.
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
  course selector/descriptor, supported scroll state, visible streamed map columns
  and original character data. Loading or stale source states fall back.
- Original view, the unverified fourth source and other ROM sets retain the existing
  renderer. In a supported scene this mode takes precedence over stretching;
  elsewhere the selected legacy background mode remains in force.

The first replay exposed a loading transition at frame 2580: the course
selector was ready before the sky tiles/palette. Checking the selector alone
showed wrongly coloured art. Live map/character matching now prevents this;
there is no frame-number delay or palette-colour heuristic.

## Try and reproduce

Both capture tools accept `--panorama-original`; `--panorama-proof` remains
the separate temporary test image, and the two options cannot be combined.
There is no build-time switch. This option is not yet exposed by `daytona.exe`
or the launcher, so the commands below produce a screenshot, not a playable
window. They use this checkout's existing Revision A build and saved cabinet
settings. Run from the repository root:

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

Remaining scope: Deluxe '93, live launcher switching and runtime testing on
other GPU backends. The fourth ROM source remains unverified. Shared
SPIR-V, DXIL and MSL shaders are regenerated from the same HLSL source.
