#!/usr/bin/env python3
"""Extract local System 24 backdrop evidence from three course and attract replays."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import widescreen_compare as capture

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-daytona')
    parser.add_argument('--nvram', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out, build = args.output.resolve(), args.build_dir.resolve()
    if subprocess.run(['git', 'check-ignore', '-q', str(out)], cwd=ROOT).returncode:
        raise SystemExit('Output must be git-ignored: extracted game artwork is local only')
    out.mkdir(parents=True, exist_ok=True)
    exe = capture.executable(build, 'm2gpushot')
    source = ROOT / 'scripts/inputs'
    cases = [('beginner', 'race_to_end.txt', 9000, 0),
             ('advanced', 'widescreen_advanced.txt', 9000, 2),
             ('expert', 'widescreen_expert.txt', 9000, 1),
             ('attract', 'attract_long.txt', 18000, None)]
    signature = {'executable': capture.digest(exe), 'runner': capture.digest(Path(__file__)),
                 'rom': {p.name: capture.digest(p) for p in sorted((build / 'rom_cache/daytona').glob('*.bin'))},
                 'nvram': {n: capture.digest(args.nvram / n) for n in ('ioboard_eeprom.bin', 'backup_ram.bin')},
                 'inputs': {name: capture.digest(source / name) for _, name, _, _ in cases}}
    manifest = out / 'manifest.json'
    if manifest.exists():
        if json.loads(manifest.read_text())['signature'] != signature:
            raise SystemExit('Input/tool signature changed; use a fresh output directory')
    else:
        (out / 'nvram').mkdir(exist_ok=True)
        for name in signature['nvram']:
            shutil.copyfile(args.nvram / name, out / 'nvram' / name)
        manifest.write_text(json.dumps({'signature': signature, 'git_head': subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()}, indent=2))

    def run(case):
        course, inputs, frames, expected_course = case
        folder = out / course
        folder.mkdir(exist_ok=True)
        (folder / 'frames').mkdir(exist_ok=True)
        result_file = folder / 'result.json'
        if result_file.exists():
            return
        shutil.copyfile(source / inputs, folder / 'inputs.txt')
        cmd = [str(exe), str(build / 'rom_cache/daytona'), str(frames), '--nvram', str(out / 'nvram'),
               '--inputs', str(folder / 'inputs.txt'), '--dump', str(folder / 'frames'), '--every', '150',
               '--dump-tiles', str(folder / 'tiles'), '--sky-log', str(folder / 'sky.jsonl')]
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        (folder / 'log.txt').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{course} failed: {result.stderr}')
        rows = [json.loads(line) for line in (folder / 'sky.jsonl').read_text().splitlines()]
        if expected_course is not None and rows[3599]['course'] != expected_course:
            raise RuntimeError(f'{course}: wrong course selected')
        for frame in range(150, frames+1, 150):
            prefix = folder / 'tiles' / f'frame_{frame:05d}'
            for layer in range(4):
                assert Path(str(prefix) + f'_layer{layer}_indices.bin').stat().st_size == 512*512*2
                assert Path(str(prefix) + f'_layer{layer}_flags.bin').stat().st_size == 512*512
            image = folder / 'frames' / f'run_{frame:05d}.rgb'
            image.with_suffix('.png').write_bytes(capture.png(image.read_bytes(), 496))
        result_file.write_text(json.dumps({'command': cmd, 'frames': frames, 'snapshots': frames//150,
                                          'verified_course_id': expected_course}, indent=2))
        print('Captured', course, flush=True)

    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, cases))


if __name__ == '__main__':
    main()
