# Widescreen validation

This records the initial capture experiment and the implemented scenery
and budget milestone. The initial experiment measured the existing renderer
before changing projection, scenery selection or launcher. Work follows the design document's
**Renderer** and **Enhancements (all off by default)** sections. The native
baseline has every enhancement off; widescreen captures are comparisons with
that baseline, not MAME parity claims.

The 4 October 2026 disk cleanup removed bulk local raw captures, frame logs
and redundant PNGs. Compact results, commands, hashes, cabinet snapshots
and representative images remain. Recorded counts below describe historical
runs. Use fresh output directories when repeating validation; `--resume`
and `--report-only` require complete captures and cannot restore these
pruned archives. The old pre-change probe executable was also removed;
its native-regression results remain recorded, but rerunning that historical
comparison requires rebuilding the matching earlier version.

## Run the comparison

Regenerate the game from the updated hook seeds, then build the capture
tools for the ROM set being tested:

```powershell
python scripts/recompile.py --set daytona --build-dir build-daytona
cmake --build build-daytona --config Release --target m2run m2gpushot --parallel 4
python scripts/widescreen_compare.py --nvram "$env:APPDATA/daytona-recomp/daytona" --output traces/widescreen-baseline --every 300
```

Revision A must have a working single-cabinet configuration saved from the
game's test mode. The script copies the supplied EEPROM and backup RAM into
the output directory. It never saves changes back to the live game profile.
Visually confirm the input replay reaches racing; exit code zero alone does
not establish this.

The defaults run `attract_long` (9,000 frames) and `race_to_end` (20,000
frames), with software and hardware rendering at original, 16:10, 16:9, 21:9 and
32:9. All start from the same cabinet snapshot. HUD movement, background
stretching and supersampling are off; draw distance is the game's default.

Open the resulting `index.html` in a browser. It works directly from disk,
without a web service. Choose a sequence and renderer, then use the frame
slider or arrow buttons. Every image has the same scale; dashed lines mark
the original 496-pixel view. The original baseline omits `--aspect` entirely:
passing `--aspect 4:3` would select a 512-pixel width, not native 496x384.

Outputs include PNG and raw frames, tool logs, `manifest.json` with exact
commands and SHA-256 hashes of executables, ROM images, input scripts and
cabinet data, `comparisons.csv`, and `summary.json`. Keep these under ignored
`traces/`; they contain game-derived material. Only source, measurements
and findings belong in the repository.

Use `--resume` with identical arguments to reuse completed captures. Changes
to tools, scripts, source input files, ROM images or starting cabinet data
are rejected. `--report-only --output <directory>` rebuilds the viewer and
measurements without running the game. An optional local `findings.json`
contains objects with `title`, `scenario`, `frame`, optional `end_frame`,
and `text`; these become jump buttons in the viewer.

## Inspect a short sequence

Both tools support `--dump-from FRAME`. They still replay from boot, but
only save selected frames at or after that absolute frame. For example:

```powershell
python scripts/widescreen_compare.py --nvram traces/widescreen-baseline/nvram --output traces/widescreen-detail --scenarios race_to_end --frames 3620 --dump-from 3580 --every 1 --aspects original 32:9
```

This captures 41 consecutive frames per renderer and ratio. The sparse
baseline at every 300th frame is useful for finding scenes to inspect, but
cannot establish that there is no brief pop-in between samples.

## Interpret the measurements

Centre comparisons crop each wide output to its central 496 columns and
count changed RGB pixels against the same renderer's original frame.
Renderer comparisons count changed RGB pixels across the complete output.
Unused alpha is ignored. Counts are descriptive; there is no arbitrary
pass/fail threshold. Clipping and texture interpolation affect comparisons,
and checker transparency is anchored to absolute output pixel coordinates.
The odd margins at 16:9 (93 pixels) and 32:9 (435) reverse checker phase
relative to the original centre; 21:9 has an even margin of 200 pixels.
Inspect framing and object presence separately from those pixel differences.

Capture elapsed times include software drawing or GPU readback and disk
output. They are not comparable gameplay performance benchmarks. The GPU
capture tool only draws sampled frames. Use its separate `--bench` mode for
performance investigations.

## Validation of the tooling

```powershell
python tests/test_widescreen_compare.py
```

Synthetic checks cover crop alignment, alpha handling, PNG channel order,
absolute frame selection, and rejecting missing or truncated captures. Real
capture runs additionally check every expected frame and exact byte size.

## Initial measured findings (before the scenery change)

The local Revision A baseline and follow-up captures are recorded in
`traces/widescreen-32x9/`. Hardware captures used Direct3D 12 on Windows.
The baseline is 16 runs, 232,000 replayed frames and 768 sampled images:
30 attract and 66 race images for each renderer/ratio combination. A
second comparison records 41 consecutive frames, 3580 through 3620, at
original and 32:9 on both renderers (164 additional images).
Frame 3600 is byte-identical between the sparse and consecutive runs in
all four shared renderer/ratio combinations (`repeatability.json`).

The saved settings reach the beginner race: frame 3000 shows rolling
start, 3600 racing, 7500 time zero, 7800 results, 8400 circuit selection
and 9300 transmission selection. Three scripted coins give three credits
with these settings, so the sequence continues into further races after
the first results screen. It is not an attract-only run or a link-wait
screen. Starting EEPROM SHA-256:
`dbbafa2b17f93b89e6fea273bbec60e259aed5dd7f93d7b4be850dce37c75736`;
backup RAM SHA-256:
`5b7ce35f6edc3bbbc8cf504fe956c2e415d45b1dfbd5f4c3cb5e456f0ece1c00`.

### 1. Missing scenery exposed at 32:9

**Reproduction:** `race_to_end`, frame 3600, 32:9, default draw distance,
with the saved starting state above. To the left of the TRACK HAWKS sign,
part of the building is absent, exposing blue sky through the scenery.
Both software and hardware captures show it.

**Controlled comparison:** software captures with `--draw-distance 1`
(Further) and `--draw-distance 2` (Furthest) restore the missing building
section. The two settings produce byte-identical frame 3600 dumps.
Further differs from default in 20,483 RGB pixels, bounded by x=0..285,
y=61..201 in the 1366x384 output. The central native region has zero
changed pixels; the central 21:9 region has 486 changed pixels. The
diagnostic also captured Further at every frame from 3580 to 3620.

The initial comparison implicated `hook_draw_list`, which changes both
the selected scenery cells and the polygon budget. The controlled
experiment below separates those effects: the missing building is caused
by omitted cells in this scene, and the original budget is sufficient to
restore it.

Exact diagnostic commands and hashes are in the local `diagnostics.json`;
pixel counts are in `draw-distance-difference.json`. For example, with the
output directory created first:

```powershell
build-daytona/Release/m2run.exe build-daytona/rom_cache/daytona 3600 --inputs scripts/inputs/race_to_end.txt --nvram traces/widescreen-32x9/nvram --aspect 32:9 --draw-distance 1 --dump-from 3600 --every 3600 --dump traces/widescreen-32x9/check-further
```

#### Selection versus budget experiment

Six conditions replay the same Revision A inputs and starting settings,
capturing all 41 frames from 3580 through 3620 with the software renderer
at 32:9. At frame 3600 the game's visibility masks select 10 cells;
Further's full 5x5 neighbourhood selects 25.

| Selection | Budget | Result |
| --- | ---: | --- |
| Original | 5,000 | Missing building section |
| Original | 10,000 | Byte-identical to original in all 41 frames |
| Original | 50,000 | Byte-identical to original in all 41 frames |
| Further | 5,000 | Restores the building; byte-identical to Further at 10,000 in all 41 frames |
| Further | 10,000 | Restores the building |
| Original | 1 | Large visible change: 464,076 changed RGB pixels at frame 3600; positive control that the budget override takes effect |

A seventh condition retains the original cells but puts them in Further's
order, at budget 5,000. Logged membership matches the original in all
41 frames and order changes in every frame. The building remains absent:
frame 3600 is byte-identical to original, while the other captures differ
by at most two RGB pixels each (15 frames differ, 26 are identical).
Changing order therefore does not account for the restored building.

**Conclusion:** added cell membership restores this defect. Increasing
the polygon budget alone does nothing here. This does not measure actual
polygon consumption or prove that budget cannot constrain other scenes.
This motivated the implemented milestone below: wider scenery selection
and an aspect-scaled automatic budget with an explicit override. Keeping
5,000 was an experimental control, not the widescreen default. Selection
and budget are independent controls, while both change in normal
widescreen use.

Reproduce the six-condition experiment with an empty, ignored output directory:

```powershell
cmake --build build-daytona --config Release --target m2run --parallel 4
python scripts/scenery_experiment.py --nvram traces/widescreen-32x9/nvram --output traces/scenery-experiment
```

The headless `m2run` controls are:

- `--draw-budget N` overrides the budget independently of draw distance;
  zero retains the normal policy.
- `--draw-order-only` requires positive draw distance and keeps only the
  original cells in the reordered list. Pair with `--draw-budget 5000`
  to isolate ordering.
- `--scenery-log FILE` now records every frame at/after `--dump-from`,
  independently of image sampling. It includes the bounded cell list,
  peak game cost, budget checks and rejected lists. The original experiment
  used the earlier, captured-frames-only format without cost observers.
- `--original-selection` disables the new widescreen selection while
  retaining the chosen budget. The experiment runner now uses this flag
  and an explicit 5,000 original budget to preserve the six-case experiment.

The local results are in `traces/widescreen-32x9/selection-budget/captures/`:
`index.html`, commands and hashes in `manifest.json`, per-frame measurements,
and JSONL scenery logs. The sibling `order-controls.json` records exact
commands, executable hashes and comparisons for the ordering control and
a default rerun after adding that control; `order-differences.json` contains
its per-frame measurements. The original six-condition executable is
preserved as `selection-budget/m2run-budget-probe.exe`.

Validation: the original-condition captures from the six-case experiment and the final default
rerun match the previous 32:9 baseline in all 41 frames. A native-width
frame 3600 control also matches the previous baseline byte-for-byte.
Both diagnostic builds succeeded; malformed and negative budget arguments
were rejected. At that stage, rendering and launcher settings were unchanged.

### 2. Background artwork still ends at the original view

Attract frame 7200 clearly shows clouds ending at the native view boundary
with plain sky outside it. That is the existing policy with stretching
disabled, separate from missing 3D objects. It becomes conspicuous at
32:9 and needs a presentation decision, not a scenery-culling fix.

### 3. Pixel equality is not a widescreen correctness test

The centre keeps its framing in the reviewed captures, but does not stay
pixel-identical. Checker phase and clipping/interpolation must be accounted
for when interpreting the CSV. Attract frame 8100 also has substantial
software/GPU differences at original width (19.019% changed pixels) as
well as 32:9 (21.641%). This is an existing renderer-comparison case,
not evidence by itself of a defect introduced at 32:9.

### Limits of the initial capture milestone

This established repeatable capture and concrete defects, not release-ready
32:9. It did not validate every frame, every driving camera, all playable
courses, Deluxe '93, GPU backends other than Direct3D 12, supersampling,
the optional HUD/background modes, or live aspect switching. The launcher
then stopped at 21:9, with rendering and scenery selection unchanged.
The following milestone expands that coverage and implements the fix.

## Approved milestone: widescreen scenery and configurable budget

Approved 4 Oct 2026. This follows the
design document's **Renderer** and **Enhancements (all off by default)**
sections. The preceding capture and diagnosis work is complete. The
controls and selection change are implemented. Revision A validation is
complete for the coverage recorded below. The user explicitly deferred
Deluxe '93 validation until its ROM set is available.

### Intended result

Selecting a wider aspect ratio also selects the scenery required by that
view and gives it a proportionately larger default polygon allowance.
Users can choose an explicit budget independently of aspect ratio and
draw distance. Original view with enhancements off retains the game's
original selection and 5,000 budget.

### Scope and policy

1. **Scenery selection follows the wider view.** Extend the existing
   scenery hook, using the reproduced missing building as the first
   regression case. Investigate the two visibility masks and widen the
   relevant selection conservatively, retaining distance controls and
   respecting the 63-cell list capacity. The final rule must account for
   the active camera, not just repair one car position. Validate 16:10,
   16:9 and 21:9 alongside 32:9.
2. **Automatic budget scales with aspect ratio.** At Default draw distance,
   use `ceil(5000 * max(1, W / 496))`, where W is the widened viewport width
   at the native 384-pixel height, before supersampling or display scaling.
   This gives approximately 6,200 at 16:10, 6,900 at 16:9, 9,000 at 21:9,
   and 13,800 at 32:9. Increasing output resolution at the same aspect
   ratio does not increase this allowance. These are initial defaults to
   validate, not a claim that geometry cost grows linearly.
3. **Preserve existing draw-distance allowances.**
   Automatic uses the larger of the aspect-scaled default and the existing
   draw-distance allowance (5,000 / 10,000 / 15,000 for Default / Further /
   Furthest). This preserves the existing higher allowances without
   multiplying the two policies together. Shorter choices retain their
   selection restriction. Draw distance and polygon budget remain distinct
   controls.
4. **Provide Automatic and Custom budget settings.** Add a persisted
   launcher/config setting and matching headless-tool support. Display the
   effective automatic allowance; a valid custom positive value replaces
   the entire automatic policy, including the draw-distance allowance.
   It can be above or below Automatic. Switching back to Automatic restores
   calculation from the current aspect and distance settings. Validate
   inputs against the game's counter representation and supported bounds;
   avoid unchecked overflow or silent reinterpretation. The existing
   diagnostic 5,000 comparison remains available.
5. **Measure selection and budget separately.** Extend diagnostic capture
   with the game's consumed budget/cost and budget-limit events or rejected
   scenery, as well as selected cells. Establish what those counters mean
   before calling them actual rendered polygons. Compare Automatic against
   a generous budget with identical selection to detect budget-related
   omissions. Record normal rendering cost separately from capture I/O.
6. **Expose 32:9 after validation.** Add it to the existing aspect choices
   with the new scenery and budget policy. This milestone concerns 3D
   scenery; background artwork, HUD redesign and the separate road window
   remain subsequent work. It is not a claim of complete 32:9 polish.

### Acceptance checks

- The known building remains present across frames 3580..3620 at 32:9,
  using Automatic and an explicit 5,000 control with identical selection.
- Fixed replays cover the three courses and race cameras at original,
  16:10, 16:9, 21:9 and 32:9. Review consecutive frames around omissions
  or pop-in; sparse screenshots alone do not pass this check. Use both
  software and Direct3D 12 rendering on Revision A; include original-view
  regression and widescreen smoke checks on Deluxe '93 for the shared hook.
- Automatic versus a generous budget produces no unexplained missing
  scenery in those cases. Record budget-limit events and performance so
  any adjustment to the proportional defaults has measured justification.
- With enhancements off, original-view replay captures match the existing
  baseline. Changing resolution or supersampling alone leaves the budget
  unchanged. Explicit budget, Automatic reset, save/reload, aspect changes
  and draw-distance changes obey the precedence above in both app and tools.
- Capture commands, starting-state hashes, coverage and remaining limits
  are recorded. Game-derived output stays under ignored `traces/`.

The implemented selection is deliberately conservative: retain the original
cell order and course/grid exclusions, append all other cells within the
selected range, and let the geometrizer clip to the active camera. It
widens directional coverage without increasing the default distance radius.
Further/Furthest retain their existing full-neighbourhood selection.

Custom values are supported from 1 to 1,000,000; zero means Automatic.
This is a setting bound with headroom for the game's unsigned counter,
not a guarantee that arbitrary values fit renderer limits or run quickly.
The launcher saves `draw_budget` in `launcher.ini`; old settings without
that key use Automatic. Both `m2run` and `m2gpushot` accept `--draw-budget`,
`--draw-distance`, `--scenery-log` and `--original-selection`.

Diagnostic costs come from the game's object-list metadata accumulated at
0x5010e8. They are measured before clipping and are not screen polygon
counts. A rejection is a strict unsigned `consumed > allowance` check;
the game checks between lists, so a list can take cost past the allowance
before a later list is rejected. Measurements cover each headless frame
step. The new instruction observers are currently mapped for Revision A.
Zero observed checks on a set without these hooks does not establish zero
polygon consumption or absence of budget pressure.

### Implementation results

In the launcher's **Game > Enhancements**, choose **Widescreen: 32:9**.
Leave **Polygon budget: Automatic**, or select **Custom** for an independent
override. The launcher displays the calculated allowance and saves both
settings. Exact Automatic allowances at Default distance are:

| Aspect | Viewport at native height | Budget |
| --- | --- | ---: |
| Original | 496x384 | 5,000 |
| 16:10 | 614x384 | 6,190 |
| 16:9 | 682x384 | 6,875 |
| 21:9 | 896x384 | 9,033 |
| 32:9 | 1366x384 | 13,771 |

The original viewport is 496x384 rather than an exact mathematical 4:3.
Integer margins also account for the small differences from rounded
aspect-ratio estimates.

The local results are under `traces/widescreen-implementation/`, with a
summary viewer at `index.html`. The verified matrix is `verified-matrix/`:
48 planned runs of 9,000 frames, 432,000 replayed frames and 1,440 sampled
images. Each run logs every frame. All three courses reach racing, verified
by course ID at frame 3600; VR1, VR2, VR3 and VR4 are selected at frames
3200, 3800, 4400 and 5000. Captures at 3600, 4200, 4800 and 5400 were
visually reviewed at 32:9 on all three courses. Both software and Direct3D
12 cover original, 16:10, 16:9, 21:9 and 32:9.

Automatic and 50,000-budget images match in all 660 sampled comparisons.
This includes four matching Beginner software references reused from an
earlier run after executable, cabinet data and inputs were checked; the
manifest records reuse. Maximum measured wide-view cost was 7,517.
Neither 21:9 nor 32:9 recorded a budget rejection. The lower 16:10/16:9
allowances rejected some lists, so `widescreen_budget_check.py` captured
every frame around those events, with two frames either side. All 1,438
pairs match the 50,000-budget reference exactly. Shared boot/attract
intervals were tested once per aspect. The rejections produced no visible
omissions in these replays, so the agreed proportional defaults remain.
Sparse samples and these targeted intervals do not cover every possible
driving path or prove that the budget never limits visible scenery.

The known building case now selects 25 cells at frame 3600 and measures
cost 4,974. Automatic (13,771) and explicit 5,000 are identical throughout
3580..3620. A budget of 1 visibly removes scenery and records rejections,
verifying the diagnostic. A 2x GPU capture is 2732x768 with the same
13,771 allowance and 4,974 cost. The preserved pre-change software
executable replayed all three courses: all 90 native-view samples over
three 9,000-frame runs are byte-identical to the current executable.

The initial matrix exposed a replay problem: the generic Advanced/Expert
inputs selected Beginner with this Revision A cabinet snapshot. The menu
uses absolute steering position; selection must be held through confirm,
and Expert needs time to settle before acceleration. The new
`widescreen_advanced.txt` and `widescreen_expert.txt` inputs fix that and
the runner asserts course IDs. The abandoned `matrix/` directory is not
three-course evidence; use `verified-matrix/`. The generic scripts remain
unchanged for other existing uses.

To repeat with the same saved cabinet state, use new output directories:

```powershell
python scripts/widescreen_validate.py --nvram traces/widescreen-32x9/nvram --output traces/widescreen-validation-repeat
python scripts/widescreen_budget_check.py --matrix traces/widescreen-validation-repeat --output traces/widescreen-budget-repeat
ctest --test-dir build-daytona -C Release -R "^(scenery|app_config)$" --output-on-failure
python tests/test_widescreen_compare.py
```

The two CTest tests and five capture-tool tests pass. Selection tests
sweep all 256 grid cells and five distance settings, exercise mask/order
preservation, per-board isolation, budget transitions and observer
non-interference. Config tests cover save/reload, legacy settings, invalid
values and Automatic reset. Both CLI tools reject negative, malformed,
overflowing, out-of-range and missing budget arguments. Revision A Release
builds of the launcher and both capture tools pass.

Separate `m2gpushot --bench` runs replayed 9,000 Beginner frames without
capture I/O or diagnostic logging:

| Aspect | Total | Throughput | Game/frame | Renderer CPU/frame | GPU wait/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 22.58 s | 399 fps | 1.96 ms | 0.39 ms | 0.16 ms |
| 32:9 | 15.24 s | 591 fps | 1.36 ms | 0.21 ms | 0.13 ms |

These are single sequential runs on this PC, including boot and menus;
warming, scheduling and background activity can affect them. They provide
indicative throughput, not evidence that 32:9 is faster or a release
performance comparison. Exact commands, executable hashes and output are
in `benchmarks.json`.

### Remaining coverage and work

Deluxe '93 original-view regression and widescreen smoke checks are
explicitly deferred until the user has that ROM set. New cost-observer
addresses must also be established for that revision before using its
diagnostic values. Other GPU backends, extended play through every course
section, live launcher switching and optional HUD/background combinations
need further coverage. This is replay validation, not a new MAME parity
claim. Sky/background boundaries remain visible, especially at 32:9;
background presentation, HUD redesign and the separate road window remain
outside this milestone.
