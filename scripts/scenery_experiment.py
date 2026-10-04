#!/usr/bin/env python3
"""Separate scenery selection from polygon budget at the recorded 32:9 defect.

Uses the software capture tool and only Python's standard library. Output is
local game-derived data; it must remain in a git-ignored directory.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import subprocess
import time

import widescreen_compare as capture

CASES = {
    "original": (0, 5000, 5000),
    "budget_only": (0, 10000, 10000),
    "selection_only": (1, 5000, 5000),
    "both": (1, 10000, 10000),
    "high_budget_only": (0, 50000, 50000),
    "low_budget_control": (0, 1, 1),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=capture.ROOT / "build-daytona")
    parser.add_argument("--nvram", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output, build = args.output.resolve(), args.build_dir.resolve()
    if not output.is_relative_to(capture.ROOT) or subprocess.run(
            ["git", "check-ignore", "-q", str(output / "index.html")], cwd=capture.ROOT).returncode:
        parser.error("output must be git-ignored inside the checkout")
    if output.exists() and any(output.iterdir()):
        parser.error("choose an empty output directory")
    tool = capture.executable(build, "m2run")
    if "M2_ROMSET:STRING=daytona\n" not in (build / "CMakeCache.txt").read_text():
        parser.error("this recorded case uses a Revision A (daytona) build")
    rom = build / "rom_cache" / "daytona"
    nvram = args.nvram.resolve()
    for name, size in (("ioboard_eeprom.bin", 128), ("backup_ram.bin", 16384)):
        if (nvram / name).stat().st_size != size:
            parser.error(f"wrong size for {name}")
    output.mkdir(parents=True, exist_ok=True)
    (output / "nvram").mkdir()
    for name in ("ioboard_eeprom.bin", "backup_ram.bin"):
        shutil.copyfile(nvram / name, output / "nvram" / name)
    inputs = output / "race_to_end.txt"
    shutil.copyfile(capture.ROOT / "scripts/inputs/race_to_end.txt", inputs)
    manifest = {"git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=capture.ROOT, text=True).strip(),
                "tool_sha256": capture.digest(tool), "script_sha256": capture.digest(__file__),
                "rom_sha256": {p.name: capture.digest(p) for p in sorted(rom.glob("*.bin"))},
                "nvram_sha256": {p.name: capture.digest(p) for p in (output / "nvram").glob("*.bin")},
                "input_sha256": capture.digest(inputs), "frames": list(range(3580, 3621)), "runs": []}
    capture.write_json(output / "manifest.json", manifest)

    def run_case(item):
        name, (selection, budget_override, expected_budget) = item
        folder = output / name
        folder.mkdir()
        cmd = [str(tool), str(rom), "3620", "--inputs", str(inputs), "--nvram", str(output / "nvram"),
               "--aspect", "32:9", "--dump", str(folder), "--dump-from", "3580", "--every", "1",
               "--original-selection", "--draw-distance", str(selection), "--draw-budget", str(budget_override),
               "--scenery-log", str(folder / "scenery.jsonl")]
        started = time.monotonic()
        print(f"Capturing {name}: selection={selection}, budget={expected_budget}", flush=True)
        with (folder / "run.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(cmd, cwd=capture.ROOT, stdout=log, stderr=subprocess.STDOUT)
        run = {"name": name, "command": cmd, "exit_code": result.returncode,
               "elapsed_seconds": round(time.monotonic() - started, 3), "status": "failed"}
        capture.write_json(folder / "run.json", run)
        if result.returncode:
            raise ValueError(f"{name} failed: see {folder / 'run.log'}")
        capture.validate_capture(folder, 1366, manifest["frames"])
        samples = [json.loads(line) for line in (folder / "scenery.jsonl").read_text().splitlines()]
        if [s["frame"] for s in samples] != manifest["frames"]:
            raise ValueError(f"{name}: scenery log frames do not match captures")
        if any(s["budget"] != expected_budget or s["cell_count"] != len(s["cells"]) for s in samples):
            raise ValueError(f"{name}: observed budget or cell count differs from the intended experiment")
        for frame in manifest["frames"]:
            path = folder / f"run_{frame:05d}.rgb"
            path.with_suffix(".png").write_bytes(capture.png(capture.read_frame(path, 1366), 1366))
        run["status"] = "complete"
        run["observed_budgets"] = sorted({s["budget"] for s in samples})
        run["observed_cell_counts"] = sorted({s["cell_count"] for s in samples})
        capture.write_json(folder / "run.json", run)
        print(f"  {name} complete in {run['elapsed_seconds']} s", flush=True)
        return run

    # Independent processes, each with its own fixed starting state and output.
    # These elapsed times include concurrent capture and are not benchmarks.
    with ThreadPoolExecutor(max_workers=2) as pool:
        for run in pool.map(run_case, CASES.items()):
            manifest["runs"].append(run)
            capture.write_json(output / "manifest.json", manifest)
    samples = {name: [json.loads(line) for line in (output / name / "scenery.jsonl").read_text().splitlines()]
               for name in CASES}
    comparisons = []
    for frame in manifest["frames"]:
        data = {name: capture.read_frame(output / name / f"run_{frame:05d}.rgb", 1366) for name in CASES}
        for name in CASES:
            comparisons.append({"frame": frame, "case": name,
                                "changed_vs_original": capture.difference(data[name], data["original"])["changed_pixels"],
                                "changed_vs_both": capture.difference(data[name], data["both"])["changed_pixels"]})
    capture.write_json(output / "comparisons.json", comparisons)
    summary = []
    for name, (_, _, budget) in CASES.items():
        rows = [r for r in comparisons if r["case"] == name]
        sample = next(s for s in samples[name] if s["frame"] == 3600)
        summary.append({"case": name, "budget": budget, "cell_count_at_3600": sample["cell_count"],
                        "all_41_equal_to_original": all(r["changed_vs_original"] == 0 for r in rows),
                        "all_41_equal_to_both": all(r["changed_vs_both"] == 0 for r in rows),
                        "cell_lists_equal_to_original": all(a["cells"] == b["cells"] for a, b in zip(samples[name], samples["original"])),
                        "cell_lists_equal_to_both": all(a["cells"] == b["cells"] for a, b in zip(samples[name], samples["both"])),
                        **next(r for r in rows if r["frame"] == 3600)})
    capture.write_json(output / "summary.json", summary)
    table = "".join(f"<tr><td>{s['case']}</td><td>{s['budget']:,}</td><td>{s['cell_count_at_3600']}</td>"
                    f"<td>{s['changed_vs_original']:,}</td><td>{s['changed_vs_both']:,}</td></tr>" for s in summary)
    figures = "".join(f'<figure><figcaption>{name}</figcaption><img src="{name}/run_03600.png" alt="{name}, frame 3600"></figure>' for name in CASES)
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Scenery selection and budget experiment</title><style>:root{color-scheme:dark;font-family:system-ui;background:#111821;color:#edf2fa}body{max-width:1400px;margin:auto;padding:28px}p{color:#becbdb;line-height:1.6}a{color:#86e4d1}table{border-collapse:collapse;width:100%;margin:24px 0}th,td{padding:12px;text-align:left;border:1px solid #425368}figure{margin:24px 0;background:#1c2a39;padding:12px;border-radius:8px}img{width:100%;display:block;margin-top:10px}code{color:#f3d186}</style>
<h1>Scenery selection × polygon budget</h1><p>Revision A · software renderer · 32:9 · identical replay and cabinet snapshot. Six conditions, 41 consecutive frames each (3580–3620). The logged cell list is the list left in game RAM after the frame; it is not a polygon count.</p>
<p><a href="manifest.json">Exact commands and hashes</a> · <a href="summary.json">41-frame summary</a> · <a href="comparisons.json">Per-frame pixel measurements</a></p>
<table><tr><th>Condition</th><th>Budget</th><th>Cells at frame 3600</th><th>Changed pixels vs original</th><th>Changed pixels vs both</th></tr>__TABLE__</table>
<p>Pixel counts in the table are for frame 3600. <code>selection_only</code> uses Further's cell selection with the original 5,000 budget. <code>high_budget_only</code> retains original selection with ten times the budget. <code>low_budget_control</code> verifies the budget override can affect rendering.</p>__FIGURES__</html>'''
    (output / "index.html").write_text(page.replace("__TABLE__", table).replace("__FIGURES__", figures), encoding="utf-8")
    print(json.dumps(summary, indent=2), flush=True)
    print(f"Report: {output / 'index.html'}", flush=True)


if __name__ == "__main__":
    main()
