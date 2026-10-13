#!/usr/bin/env python3
"""Prepare local engine-start artwork; never commit the input or output.

The input is the complete approved 32:9 composition, including the lettering.
--crop removes letterboxing. --pens is a local 4096-entry BGRA palette dump
from the fully lit engine-start screen (m2gpushot --dump-tiles).
Requires Pillow. Output is loaded from artwork/engine-start.d2bg beside daytona.
"""
import argparse
import struct
from pathlib import Path
from PIL import Image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--pens', required=True, type=Path)
    p.add_argument('--crop', type=int, nargs=4, metavar=('LEFT', 'TOP', 'RIGHT', 'BOTTOM'))
    args = p.parse_args()
    pens = struct.unpack('<4096I', args.pens.read_bytes())
    palette = [tuple((pens[0x600+i] >> shift) & 255 for shift in (16, 8, 0)) for i in range(16)]
    if len(set(palette[2:15])) != 13:
        p.error('palette must be captured after the engine-start colour fade has finished')
    image = Image.open(args.image).convert('RGB')
    if args.crop:
        l, t, r, b = args.crop
        if not (0 <= l < r <= image.width and 0 <= t < b <= image.height):
            p.error('crop lies outside the image')
        image = image.crop((l, t, r, b))
    # Match the game's native pixel size once, not on each frame. The renderer
    # takes a centred crop for every narrower aspect, retaining the text size.
    image = image.resize((1366, 384), Image.Resampling.LANCZOS)
    cache = {}
    def encode(rgb):
        # Retain intermediate tones as a blend of two live game colours.
        # Unlike RGB artwork this follows all palette fades in either ROM.
        key = tuple(v >> 3 for v in rgb)
        if key not in cache:
            colour = tuple(255 if v == 31 else v * 8 + 4 for v in key)
            best = (float('inf'), 0)
            for a in range(1, 16):
                for b in range(a,16):
                    low, high = palette[a], palette[b]
                    delta = [high[c] - low[c] for c in range(3)]
                    norm = sum(v*v for v in delta)
                    t = max(0, min(255, round(255 * sum((colour[c]-low[c])*delta[c] for c in range(3)) / norm))) if norm else 0
                    distance = sum((colour[c] - (low[c]*(255-t)+high[c]*t)/255)**2 for c in range(3))
                    best = min(best, (distance, a | (b << 4) | (t << 8)))
            cache[key] = best[1]
        return cache[key]
    samples = [0] * (1366*384)
    for y in range(384):
        for x in range(1366):
            samples[y*1366+x] = encode(image.getpixel((x,y)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b'D2BGv002' + struct.pack('<II',1366,384) + struct.pack('<%dH'%len(samples),*samples))
    print(f'{args.output}: 1366x384 complete palette-linked artwork')


if __name__ == '__main__':
    main()
