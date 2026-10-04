#!/usr/bin/env python3
"""Capture reproducible widescreen comparisons using m2run and m2gpushot.

Standard library only. Run --help for options. Output must be git-ignored;
captures and saved cabinet data are local game-derived artifacts.
"""
import argparse
from array import array
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
HEIGHT = 384
ASPECTS = {"original": 496, "16:10": 614, "16:9": 682, "21:9": 896, "32:9": 1366}
TOOLS = {"software": "m2run", "hardware": "m2gpushot"}
SCENARIOS = ("attract_long", "race_to_end", "race_basic", "course_advanced", "course_expert")


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def read_frame(path, width):
    data = path.read_bytes()
    expected = width * HEIGHT * 4
    if len(data) != expected:
        raise ValueError(f"{path}: {len(data)} bytes, expected {expected} ({width}x{HEIGHT})")
    return data


def png(data, width, height=HEIGHT):
    """BGRA dump to RGB PNG; slice assignment avoids a Python loop per pixel."""
    if len(data) != width * height * 4:
        raise ValueError("frame size does not match dimensions")
    rgb = bytearray(width * height * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = data[2::4], data[1::4], data[0::4]
    stride = width * 3
    rows = b"".join(b"\0" + rgb[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def crop(data, width, crop_width=496, height=HEIGHT):
    if crop_width > width or (width - crop_width) % 2:
        raise ValueError("crop must fit and have an integer centre offset")
    margin = (width - crop_width) // 2
    return b"".join(data[(y * width + margin) * 4:(y * width + margin + crop_width) * 4]
                    for y in range(height))


def difference(left, right):
    """Count changed RGB pixels, ignoring unused output alpha. No pass threshold."""
    if len(left) != len(right) or len(left) % 4:
        raise ValueError("comparison frame sizes differ or are not BGRA")
    a, b = array("I"), array("I")
    a.frombytes(left)
    b.frombytes(right)
    if sys.byteorder != "little":
        a.byteswap()
        b.byteswap()
    changed = sum(bool((x ^ y) & 0xffffff) for x, y in zip(a, b))
    return {"changed_pixels": changed, "pixels": len(a),
            "changed_percent": round(100 * changed / len(a), 6) if a else 0}


def executable(build, name):
    for path in (build / "Release" / (name + ".exe"), build / (name + ".exe"), build / name):
        if path.is_file():
            return path.resolve()
    raise ValueError(f"Build {name} first: cmake --build {build} --config Release --target {name}")


def expected_frames(total, every, first):
    return list(range(max(1, (first + every - 1) // every) * every, total + 1, every))


def validate_capture(folder, width, frames):
    actual = sorted(int(p.stem.split("_")[1]) for p in folder.glob("run_*.rgb"))
    if actual != frames:
        raise ValueError(f"{folder}: frame set differs from expected {len(frames)} captures; got {len(actual)}")
    for frame in frames:
        read_frame(folder / f"run_{frame:05d}.rgb", width)


def analyse(output, manifest):
    rows, groups = [], {}
    for run in manifest["runs"]:
        if run["status"] != "complete":
            continue
        groups.setdefault(run["scenario"], {})[(run["renderer"], run["aspect"])] = run
    for scenario, runs in groups.items():
        frames = sorted(set.intersection(*(set(r["frames"]) for r in runs.values())))
        for frame in frames:
            raw = {}
            for key, run in runs.items():
                folder = output / run["folder"]
                source = folder / f"run_{frame:05d}.rgb"
                data = read_frame(source, run["width"])
                raw[key] = data
                target = source.with_suffix(".png")
                if not target.exists():
                    target.write_bytes(png(data, run["width"]))
            for (renderer, aspect), data in raw.items():
                baseline = raw.get((renderer, "original"))
                if aspect != "original" and baseline is not None:
                    rows.append({"scenario": scenario, "frame": frame, "kind": "centre_vs_original",
                                 "renderer": renderer, "aspect": aspect,
                                 **difference(baseline, crop(data, ASPECTS[aspect]))})
                other = raw.get(("hardware", aspect))
                if renderer == "software" and other is not None:
                    rows.append({"scenario": scenario, "frame": frame, "kind": "software_vs_hardware",
                                 "renderer": "both", "aspect": aspect, **difference(data, other)})
    fields = ["scenario", "frame", "kind", "renderer", "aspect", "changed_pixels", "pixels", "changed_percent"]
    with (output / "comparisons.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fields)
        writer.writeheader()
        writer.writerows(rows)
    summary = []
    for key in sorted({(r["scenario"], r["kind"], r["renderer"], r["aspect"]) for r in rows}):
        selected = [r for r in rows if (r["scenario"], r["kind"], r["renderer"], r["aspect"]) == key]
        worst = max(selected, key=lambda r: r["changed_pixels"])
        summary.append(dict(zip(["scenario", "kind", "renderer", "aspect"], key),
                            captures=len(selected), exact_captures=sum(r["changed_pixels"] == 0 for r in selected),
                            max_changed_percent=worst["changed_percent"], worst_frame=worst["frame"]))
    write_json(output / "summary.json", summary)
    findings_path = output / "findings.json"
    findings = json.loads(findings_path.read_text(encoding="utf-8")) if findings_path.exists() else []
    data = json.dumps({"manifest": manifest, "metrics": rows, "findings": findings}).replace("<", "\\u003c")
    template = Path(__file__).with_name("widescreen_viewer.html").read_text(encoding="utf-8")
    (output / "index.html").write_text(template.replace("__CAPTURE_DATA__", data), encoding="utf-8")
    print(f"Viewer: {output / 'index.html'}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-daytona")
    parser.add_argument("--rom-dir", type=Path, help="defaults to build-dir/rom_cache/<configured set>")
    parser.add_argument("--nvram", type=Path, help="directory containing saved EEPROM and backup RAM; copied once")
    parser.add_argument("--output", type=Path, required=True, help="new git-ignored output directory")
    parser.add_argument("--scenarios", nargs="+", choices=SCENARIOS, default=["attract_long", "race_to_end"])
    parser.add_argument("--aspects", nargs="+", choices=ASPECTS, default=list(ASPECTS))
    parser.add_argument("--renderers", nargs="+", choices=TOOLS, default=list(TOOLS))
    parser.add_argument("--every", type=int, default=600)
    parser.add_argument("--frames", type=int, help="override each scenario's frame count")
    parser.add_argument("--dump-from", type=int, default=0, help="replay from boot, only save frames at/after this frame")
    parser.add_argument("--resume", action="store_true", help="reuse completed runs only when all fingerprints match")
    parser.add_argument("--report-only", action="store_true", help="rebuild metrics and viewer from the saved manifest")
    args = parser.parse_args()
    output = args.output.resolve()
    manifest_path = output / "manifest.json"
    if not output.is_relative_to(ROOT) or subprocess.run(
            ["git", "check-ignore", "-q", str(output / "index.html")], cwd=ROOT).returncode != 0:
        parser.error("output must be in a git-ignored directory in this checkout, such as traces/widescreen")
    if args.report_only:
        analyse(output, json.loads(manifest_path.read_text(encoding="utf-8")))
        return
    if args.every <= 0 or args.dump_from < 0 or (args.frames is not None and args.frames <= 0):
        parser.error("every and frames must be positive; dump-from must be non-negative")
    if args.nvram is None:
        parser.error("--nvram is required; use a single-cabinet snapshot for Revision A")
    build = args.build_dir.resolve()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    romset = re.search(r"^M2_ROMSET:[^=]+=(\S+)$", cache, re.M).group(1)
    rom_dir = (args.rom_dir or build / "rom_cache" / romset).resolve()
    if not rom_dir.is_dir():
        parser.error(f"ROM image directory does not exist: {rom_dir}")
    snapshot = output / "nvram"
    nvram_source = args.nvram.resolve()
    nvram_hashes = {}
    for name, size in (("ioboard_eeprom.bin", 128), ("backup_ram.bin", 16384)):
        source = nvram_source / name
        if source.stat().st_size != size:
            parser.error(f"{source} must be {size} bytes")
        nvram_hashes[name] = digest(source)
    paths = {r: executable(build, TOOLS[r]) for r in args.renderers}
    scenarios = {}
    for name in args.scenarios:
        path = ROOT / "scripts" / "inputs" / (name + ".txt")
        total = args.frames or int(re.search(r"^frames (\d+)", path.read_text(), re.M).group(1))
        frames = expected_frames(total, args.every, args.dump_from)
        if not frames:
            parser.error(f"{name}: no frames selected")
        scenarios[name] = {"input": str(path), "sha256": digest(path), "total": total, "frames": frames}
    config = {"romset": romset, "rom_dir": str(rom_dir), "build": str(build),
              "rom_sha256": {p.name: digest(p) for p in sorted(rom_dir.glob("*.bin"))},
              "nvram_sha256": nvram_hashes,
              "tools": {r: {"path": str(p), "sha256": digest(p)} for r, p in paths.items()},
              "script_sha256": digest(__file__),
              "scenarios": scenarios, "aspects": args.aspects, "every": args.every, "dump_from": args.dump_from,
              "settings": {"hud_edges": False, "stretch_backdrop": False, "draw_distance": 0, "scale": 1}}
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not args.resume or manifest["config"] != config:
            parser.error("output already has a different run; choose a new directory, or --resume with identical inputs/tools")
        for name, checksum in nvram_hashes.items():
            if digest(snapshot / name) != checksum:
                parser.error("saved cabinet snapshot changed")
        for name, scenario in scenarios.items():
            if digest(output / "inputs" / (name + ".txt")) != scenario["sha256"]:
                parser.error("saved replay input changed")
    else:
        if output.exists() and any(output.iterdir()):
            parser.error("choose an empty output directory")
        output.mkdir(parents=True, exist_ok=True)
        snapshot.mkdir()
        for name in nvram_hashes:
            shutil.copyfile(nvram_source / name, snapshot / name)
        inputs = output / "inputs"
        inputs.mkdir()
        for name, scenario in scenarios.items():
            shutil.copyfile(scenario["input"], inputs / (name + ".txt"))
        manifest = {"config": config, "git_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(), "git_status": subprocess.check_output(
            ["git", "status", "--short"], cwd=ROOT, text=True), "runs": []}
        write_json(manifest_path, manifest)
    for name, scenario in scenarios.items():
        for renderer, tool in paths.items():
            for aspect in args.aspects:
                folder = Path(name) / renderer / aspect.replace(":", "x")
                existing = next((r for r in manifest["runs"] if r["folder"] == folder.as_posix()), None)
                if existing and existing["status"] == "complete":
                    validate_capture(output / folder, ASPECTS[aspect], scenario["frames"])
                    print(f"Reused {folder}", flush=True)
                    continue
                destination = output / folder
                destination.mkdir(parents=True, exist_ok=True)
                # Retry only this tool-owned run. Never mix old images with a new attempt.
                for stale in destination.glob("run_*"):
                    if stale.suffix in (".rgb", ".png") and re.fullmatch(r"run_\d+", stale.stem):
                        stale.unlink()
                cmd = [str(tool), str(rom_dir), str(scenario["total"]), "--inputs",
                       str(output / "inputs" / (name + ".txt")), "--nvram", str(snapshot),
                       "--dump", str(destination), "--every", str(args.every), "--dump-from", str(args.dump_from)]
                if aspect != "original":
                    cmd.extend(["--aspect", aspect])
                run = {"scenario": name, "renderer": renderer, "aspect": aspect, "width": ASPECTS[aspect],
                       "height": HEIGHT, "frames": scenario["frames"], "folder": folder.as_posix(),
                       "command": cmd, "status": "running"}
                if existing:
                    manifest["runs"].remove(existing)
                manifest["runs"].append(run)
                write_json(manifest_path, manifest)
                print(f"Capturing {folder}: {scenario['total']} frames, {len(scenario['frames'])} samples", flush=True)
                started = time.monotonic()
                with (destination / "run.log").open("w", encoding="utf-8") as log:
                    result = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
                run["elapsed_seconds"] = round(time.monotonic() - started, 3)
                run["exit_code"] = result.returncode
                try:
                    if result.returncode:
                        raise ValueError(f"capture exited with code {result.returncode}; see {destination / 'run.log'}")
                    validate_capture(destination, ASPECTS[aspect], scenario["frames"])
                    run["status"] = "complete"
                except ValueError as error:
                    run["status"], run["error"] = "failed", str(error)
                    write_json(manifest_path, manifest)
                    raise
                write_json(manifest_path, manifest)
                print(f"  complete in {run['elapsed_seconds']} s", flush=True)
    analyse(output, manifest)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(str(error))
