#!/usr/bin/env python3
"""Capture the opt-in Revision A Beginner panorama proof; output stays in traces/."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

import widescreen_compare as capture

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, default=ROOT / 'build-daytona')
    ap.add_argument('--nvram', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--bench', action='store_true', help='also run sequential warmed performance comparisons')
    args = ap.parse_args()
    out, build = args.output.resolve(), args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tools = {r: capture.executable(build, tool) for r, tool in [('software', 'm2run'), ('hardware', 'm2gpushot')]}
    source = ROOT / 'scripts/inputs/race_to_end.txt'
    inputs = source.read_text(encoding='utf-8') + '\n3200-3209 vr1=1\n3800-3809 vr2=1\n4400-4409 vr3=1\n5000-5009 vr4=1\n'
    signature = {'tools': {k: digest(v) for k, v in tools.items()},
                 'inputs': hashlib.sha256(inputs.encode()).hexdigest(),
                 'nvram': {n: digest(args.nvram / n) for n in ('ioboard_eeprom.bin', 'backup_ram.bin')},
                 'rom': {p.name: digest(p) for p in sorted((build / 'rom_cache/daytona').glob('*.bin'))},
                 'runner': digest(Path(__file__))}
    manifest = out / 'manifest.json'
    if manifest.exists():
        if json.loads(manifest.read_text())['signature'] != signature:
            raise SystemExit('Inputs or executable changed: use a new output directory')
    else:
        (out / 'nvram').mkdir(exist_ok=True)
        for name in signature['nvram']:
            shutil.copyfile(args.nvram / name, out / 'nvram' / name)
        (out / 'inputs.txt').write_text(inputs, encoding='utf-8')
        manifest.write_text(json.dumps({'signature': signature, 'git_head': subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()}, indent=2))
    jobs = []

    def add(name, renderer, aspect, frames, first, every, flags):
        jobs.append((name, renderer, aspect, frames, first, every, flags))

    for aspect in ('16:9', '32:9'):
        slug = aspect.replace(':', 'x')
        for renderer in tools:
            for mode in ('plain', 'panorama'):
                add(f'{slug}/{renderer}/{mode}', renderer, aspect, 9000, 0, 300,
                    [] if mode == 'plain' else ['--panorama-proof'])
            add(f'{slug}/{renderer}/sweep', renderer, aspect, 3712, 3584, 1,
                ['--panorama-proof', '--panorama-sweep', '--panorama-only'])
            for mode in ('plain', 'panorama'):
                add(f'native/{renderer}/{mode}', renderer, None, 3600, 3600, 3600,
                    [] if mode == 'plain' else ['--panorama-proof'])
        for start, end in ((3688,3705), (3788,3820), (4388,4420), (4988,5020), (5468,5480)):
            add(f'{slug}/hardware/detail-{start}', 'hardware', aspect, end, start, 1, ['--panorama-proof'])
    # Avoid duplicate native jobs from the two aspects.
    jobs = list({j[0]: j for j in jobs}.values())
    add('32x9/hardware/scale2', 'hardware', '32:9', 3600, 3600, 3600,
        ['--panorama-proof', '--panorama-only', '--scale', '2'])
    add('32x9/software/isolated', 'software', '32:9', 3600, 3600, 3600,
        ['--panorama-proof', '--panorama-only'])

    def run(job):
        name, renderer, aspect, frames, first, every, flags = job
        folder = out / name
        folder.mkdir(parents=True, exist_ok=True)
        result_file = folder / 'result.json'
        cmd = [str(tools[renderer]), str(build / 'rom_cache/daytona'), str(frames),
               '--nvram', str(out / 'nvram'), '--inputs', str(out / 'inputs.txt'),
               '--dump', str(folder), '--dump-from', str(first), '--every', str(every),
               '--sky-log', str(folder / 'sky.jsonl')] + (['--aspect', aspect] if aspect else []) + flags
        if not result_file.exists():
            start = time.perf_counter()
            result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
            (folder / 'log.txt').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'{name} failed: {result.stderr}')
            rows = [json.loads(line) for line in (folder / 'sky.jsonl').read_text().splitlines()]
            expected = list(range(max(every, ((first + every - 1) // every) * every), frames + 1, every))
            width = 496 if not aspect else capture.ASPECTS[aspect]
            scale = 2 if '--scale' in flags else 1
            for frame in expected:
                image = folder / f'run_{frame:05d}.rgb'
                if image.stat().st_size != width * 384 * 4 * scale * scale:
                    raise RuntimeError(f'Invalid image size: {image}')
            # Keep the isolated sweep compact; PNGs at quarter turns suffice for review.
            for image in folder.glob('*.rgb'):
                frame = int(image.stem.split('_')[1])
                if 'sweep' not in name or frame % 32 == 0:
                    image.with_suffix('.png').write_bytes(capture.png(image.read_bytes(), width * scale, 384 * scale))
            result_file.write_text(json.dumps({'command': cmd, 'seconds': time.perf_counter()-start,
                'images': len(expected), 'logged_frames': len(rows),
                'active_frames': sum(r['panorama'] for r in rows)}, indent=2))
        print('Completed', name, flush=True)
        return name

    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, jobs))
    checks = []
    for renderer in tools:
        a, b = [out / f'native/{renderer}/{mode}/run_03600.rgb' for mode in ('plain', 'panorama')]
        if a.read_bytes() != b.read_bytes():
            raise RuntimeError(f'Native view changed with prototype enabled: {renderer}')
        checks.append({'renderer': renderer, 'native_identical': True})
    for aspect in ('16x9', '32x9'):
        software, hardware = out / aspect / 'software/sweep', out / aspect / 'hardware/sweep'
        for image in sorted(software.glob('*.rgb')):
            if image.read_bytes() != (hardware / image.name).read_bytes():
                raise RuntimeError(f'Isolated panorama CPU/GPU mismatch: {aspect}/{image.name}')
        rows = [json.loads(x) for x in (hardware / 'sky.jsonl').read_text().splitlines()]
        if not all(r['panorama'] for r in rows):
            raise RuntimeError(f'Inactive panorama during sweep: {aspect}')
        checks.append({'aspect': aspect, 'isolated_identical_pairs': len(rows)})
    # Same asset, same central view at both ratios, including the wrap itself.
    import array
    small = array.array('I', (out / '32x9/software/isolated/run_03600.rgb').read_bytes())
    large = array.array('I', (out / '32x9/hardware/scale2/run_03600.rgb').read_bytes())
    if any(large[y * 2732 + x] != small[(y // 2) * 1366 + x // 2] for y in range(768) for x in range(2732)):
        raise RuntimeError('Supersampling changed the background scale/position')
    checks.append({'scale2_exact_nearest_replication': True})
    for frame in range(3584, 3713):
        wide = array.array('I', (out / f'32x9/hardware/sweep/run_{frame:05d}.rgb').read_bytes())
        narrow = array.array('I', (out / f'16x9/hardware/sweep/run_{frame:05d}.rgb').read_bytes())
        for y in range(384):
            if wide[y*1366+342:y*1366+1024] != narrow[y*682:(y+1)*682]:
                raise RuntimeError(f'Aspect changed sky scale/position at frame {frame}')
    (out / 'checks.json').write_text(json.dumps(checks + [{'aspect_crop_identical_pairs': 129}], indent=2))
    if args.bench:
        results = []
        for aspect, order in [('16:9', ['plain','panorama','panorama','plain']),
                              ('32:9', ['plain','panorama','stretch','stretch','panorama','plain'])]:
            for mode in order:
                cmd = [str(tools['hardware']), str(build / 'rom_cache/daytona'), '6000', '--bench',
                       '--bench-from','3000','--nvram',str(out/'nvram'),'--inputs',str(out/'inputs.txt'),
                       '--aspect',aspect] + ({'plain': [], 'panorama':['--panorama-proof'], 'stretch':['--stretch-backdrop']}[mode])
                r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)
                results.append({'aspect':aspect, 'mode':mode, 'command':cmd, 'output':r.stdout})
                (out/'benchmarks.json').write_text(json.dumps(results,indent=2))
                print('Benchmarked', aspect, mode, flush=True)
    print('Panorama proof comparisons passed:', out, flush=True)


if __name__ == '__main__':
    main()
