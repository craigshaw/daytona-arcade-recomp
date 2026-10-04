#!/usr/bin/env python3
"""Inspect EVERY budget-rejection frame found by widescreen_validate.py.

Two-frame margins include transitions. Shared boot/attract intervals are
checked once per aspect, not once per course. Uses hardware readback.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess

import widescreen_compare as capture
from widescreen_validate import ASPECTS, COURSES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=capture.ROOT / "build-daytona")
    args = parser.parse_args()
    matrix, out, build = args.matrix.resolve(), args.output.resolve(), args.build_dir.resolve()
    if not out.is_relative_to(capture.ROOT) or subprocess.run(
            ["git", "check-ignore", "-q", str(out / "summary.json")], cwd=capture.ROOT).returncode:
        parser.error("output must be git-ignored inside the checkout")
    tool = capture.executable(build, "m2gpushot")
    manifest = json.loads((matrix / "manifest.json").read_text())
    if capture.digest(tool) != manifest["signature"]["tools"]["hardware"]:
        parser.error("capture executable changed since the matrix")
    intervals = []
    for course in COURSES:
        for aspect in ASPECTS:
            if aspect == "original":
                continue
            source = matrix / course / aspect / "hardware/automatic"
            if not (source / "run.json").exists():
                parser.error(f"matrix incomplete: {source}")
            samples = [json.loads(line) for line in (source / "scenery.jsonl").read_text().splitlines()]
            frames = [s["frame"] for s in samples if s["budget_rejections"] and
                      (course == "beginner" or s["frame"] >= 1400)]
            ranges = []
            for frame in frames:
                start, end = max(1, frame - 2), min(9000, frame + 2)
                if ranges and start <= ranges[-1][1] + 1:
                    ranges[-1][1] = end
                else:
                    ranges.append([start, end])
            intervals += [(course, aspect, start, end) for start, end in ranges]
    out.mkdir(parents=True, exist_ok=True)
    capture.write_json(out / "manifest.json", {"matrix": str(matrix), "tool_sha256": capture.digest(tool),
                                              "intervals": intervals})

    def run(item):
        course, aspect, start, end = item
        folder = out / f"{course}-{aspect}-{start}-{end}"
        if (folder / "result.json").exists():
            return json.loads((folder / "result.json").read_text())
        commands = []
        for budget, name in [(0, "automatic"), (50000, "generous")]:
            target = folder / name
            target.mkdir(parents=True, exist_ok=True)
            command = [str(tool), str(build / "rom_cache/daytona"), str(end),
                       "--inputs", str(matrix / "inputs" / f"{course}.txt"), "--nvram", str(matrix / "nvram"),
                       "--aspect", aspect.replace("x", ":"), "--draw-budget", str(budget),
                       "--dump", str(target), "--dump-from", str(start), "--every", "1"]
            commands.append(command)
            with (target / "run.log").open("w") as log:
                subprocess.run(command, cwd=capture.ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            capture.validate_capture(target, ASPECTS[aspect], list(range(start, end + 1)))
        changed = []
        for frame in range(start, end + 1):
            a = folder / "automatic" / f"run_{frame:05d}.rgb"
            b = folder / "generous" / a.name
            first, second = a.read_bytes(), b.read_bytes()
            if first == second:
                continue
            diff = capture.difference(first, second)
            if diff["changed_pixels"]:
                changed.append({"frame": frame, **diff})
                a.with_suffix(".png").write_bytes(capture.png(first, ASPECTS[aspect]))
                b.with_suffix(".png").write_bytes(capture.png(second, ASPECTS[aspect]))
        result = {"course": course, "aspect": aspect, "start": start, "end": end,
                  "commands": commands, "changed": changed}
        capture.write_json(folder / "result.json", result)
        print(f"{folder.name}: {len(changed)}/{end-start+1} frames differ", flush=True)
        return result

    with ThreadPoolExecutor(max_workers=2) as pool:
        rows = list(pool.map(run, intervals))
    capture.write_json(out / "summary.json", rows)
    print(f"Checked {sum(r['end']-r['start']+1 for r in rows)} frame pairs; "
          f"{sum(len(r['changed']) for r in rows)} differ", flush=True)


if __name__ == "__main__":
    main()
