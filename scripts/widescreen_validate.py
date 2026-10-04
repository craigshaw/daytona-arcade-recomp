#!/usr/bin/env python3
"""Course/camera replay matrix for widescreen selection and polygon budgets.

Uses the supplied Revision A cabinet snapshot. Captures stay git-ignored.
Run again with the same output to resume; executable/input changes are rejected.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import subprocess
import time

import widescreen_compare as capture

ASPECTS = {"original": 496, "16x10": 614, "16x9": 682, "21x9": 896, "32x9": 1366}
COURSES = {"beginner": "race_to_end", "advanced": "widescreen_advanced", "expert": "widescreen_expert"}
COURSE_IDS = {"beginner": 0, "advanced": 2, "expert": 1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--nvram", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=capture.ROOT / "build-daytona")
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    out, build = args.output.resolve(), args.build_dir.resolve()
    if not out.is_relative_to(capture.ROOT) or subprocess.run(
            ["git", "check-ignore", "-q", str(out / "index.html")], cwd=capture.ROOT).returncode:
        parser.error("output must be git-ignored inside the checkout")
    tools = {name: capture.executable(build, exe) for name, exe in capture.TOOLS.items()}
    signature = {"tools": {name: capture.digest(path) for name, path in tools.items()},
                 "nvram": {p.name: capture.digest(p) for p in sorted(args.nvram.glob("*.bin"))},
                 "inputs": {name: capture.digest(capture.ROOT / f"scripts/inputs/{source}.txt")
                            for name, source in COURSES.items()}, "frames": 9000, "every": 300,
                 "camera_switches": {3200: "vr1", 3800: "vr2", 4400: "vr3", 5000: "vr4"}}
    # JSON round trip normalizes the integer keys for resume comparison.
    signature = json.loads(json.dumps(signature))
    if (out / "manifest.json").exists():
        manifest = json.loads((out / "manifest.json").read_text())
        if not args.report_only and signature != manifest["signature"]:
            parser.error("tools, input scripts or cabinet snapshot changed; choose a new output")
    else:
        if args.report_only:
            parser.error("manifest not found")
        out.mkdir(parents=True, exist_ok=True)
        shutil.copytree(args.nvram, out / "nvram")
        (out / "inputs").mkdir()
        for name, source in COURSES.items():
            script = (capture.ROOT / f"scripts/inputs/{source}.txt").read_text()
            script += "\n# Exercise all four race cameras after the start.\n"
            script += "\n".join(f"{start}-{int(start)+9} {button}=1"
                                for start, button in signature["camera_switches"].items()) + "\n"
            (out / "inputs" / f"{name}.txt").write_text(script)
        manifest = {"signature": signature, "git_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=capture.ROOT, text=True).strip(),
                    "rom_hashes": {p.name: capture.digest(p) for p in sorted((build / "rom_cache/daytona").glob("*.bin"))}}
        capture.write_json(out / "manifest.json", manifest)

    jobs = [(course, aspect, renderer, budget)
            for course in COURSES for aspect in ASPECTS for renderer in tools
            for budget in ([0, 50000] if renderer == "hardware" or aspect == "32x9" else [0])]
    if args.prepare_only:
        return

    def run(job):
        course, aspect, renderer, budget = job
        folder = out / course / aspect / renderer / ("automatic" if budget == 0 else "generous")
        record = folder / "run.json"
        expected = list(range(300, 9001, 300))
        if record.exists() and json.loads(record.read_text()).get("exit_code") == 0:
            capture.validate_capture(folder, ASPECTS[aspect], expected)
            return
        folder.mkdir(parents=True, exist_ok=True)
        command = [str(tools[renderer]), str(build / "rom_cache/daytona"), "9000",
                   "--inputs", str(out / "inputs" / f"{course}.txt"), "--nvram", str(out / "nvram"),
                   "--dump", str(folder), "--every", "300", "--draw-budget", str(budget),
                   "--scenery-log", str(folder / "scenery.jsonl")]
        if aspect != "original":
            command += ["--aspect", aspect.replace("x", ":")]
        start = time.monotonic()
        with (folder / "run.log").open("w") as log:
            result = subprocess.run(command, cwd=capture.ROOT, stdout=log, stderr=subprocess.STDOUT)
        info = {"command": command, "exit_code": result.returncode, "seconds": round(time.monotonic()-start, 3)}
        capture.write_json(record, info)
        if result.returncode:
            raise RuntimeError(f"{folder}: capture failed")
        capture.validate_capture(folder, ASPECTS[aspect], expected)
        samples = [json.loads(line) for line in (folder / "scenery.jsonl").read_text().splitlines()]
        if [s["frame"] for s in samples] != list(range(1, 9001)):
            raise ValueError(f"{folder}: expected telemetry for every frame")
        sample = samples[3599]
        if sample["course"] != COURSE_IDS[course] or sample["list_updates"] == 0:
            raise ValueError(f"{folder}: intended course not reached at frame 3600")
        print(f"Completed {course} {aspect} {renderer} {budget or 'Automatic'}", flush=True)

    if not args.report_only:
        with ThreadPoolExecutor(max_workers=max(1, min(args.workers, 4))) as pool:
            list(pool.map(run, jobs))
    rows = []
    for course, aspect, renderer, budget in jobs:
        folder = out / course / aspect / renderer / ("automatic" if budget == 0 else "generous")
        samples = [json.loads(line) for line in (folder / "scenery.jsonl").read_text().splitlines()]
        row = {"course": course, "aspect": aspect, "renderer": renderer, "budget": budget,
               "logged_frames": len(samples), "max_cost": max(s["cost_peak"] for s in samples),
               "rejections": sum(s["budget_rejections"] for s in samples),
               "observed_courses": sorted({s["course"] for s in samples if s["list_updates"]}),
               "path": folder.relative_to(out).as_posix()}
        comparisons = []
        for rgb in sorted(folder.glob("run_*.rgb")):
            data = capture.read_frame(rgb, ASPECTS[aspect])
            if not rgb.with_suffix(".png").exists():
                rgb.with_suffix(".png").write_bytes(capture.png(data, ASPECTS[aspect]))
            generous = folder.parent / "generous" / rgb.name
            if budget == 0 and generous.exists():
                comparisons.append({"frame": int(rgb.stem[4:]), **capture.difference(data, generous.read_bytes())})
        row["budget_comparison"] = comparisons
        rows.append(row)
    capture.write_json(out / "summary.json", rows)
    cards = []
    for row in rows:
        if row["budget"] != 0:
            continue
        changed = sum(r["changed_pixels"] != 0 for r in row["budget_comparison"])
        cards.append(f'<section><h2>{row["course"]} · {row["aspect"]} · {row["renderer"]}</h2>'
                     f'<p>Peak recorded cost: {row["max_cost"]:,}; budget rejections: {row["rejections"]:,}; '
                     f'{changed}/{len(row["budget_comparison"])} sampled frames differ from generous budget.</p>'
                     + ''.join(f'<figure><figcaption>Frame {frame}</figcaption><img loading="lazy" '
                               f'src="{row["path"]}/run_{frame:05d}.png"></figure>'
                               for frame in [3600, 4200, 4800, 5400, 7800]) + '</section>')
    page = '''<!doctype html><meta charset="utf-8"><title>Widescreen scenery validation</title>
<style>:root{color-scheme:dark;font-family:system-ui;background:#121923;color:#edf2fa}body{max-width:1400px;margin:auto;padding:24px}p{line-height:1.6}a{color:#86e4d1}section{border:1px solid #456;padding:16px;margin:24px 0}figure{margin:16px 0}img{display:block;max-width:100%;margin-top:8px}</style>
<h1>Widescreen scenery validation</h1><p>Revision A · three course-selection scripts · all four race cameras requested · five aspect ratios · software and Direct3D 12. Verify the pictured course/camera, not just the input command. Costs are game object metadata before clipping; capture times are not performance benchmarks.</p>
<p><a href="manifest.json">Starting-state and executable hashes</a> · <a href="summary.json">Measurements and budget comparisons</a></p>'''
    (out / "index.html").write_text(page + ''.join(cards), encoding="utf-8")
    print(f"Report: {out / 'index.html'}", flush=True)


if __name__ == "__main__":
    main()
