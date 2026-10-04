# Original backdrop inventory

Revision A, 4 October 2026. Extracted from the user's local ROM and read-only
runtime snapshots. All image and ROM-derived output remains under ignored
`traces/backdrop-inventory/`; none is a distributable repository asset.

## Result

The race sky is not merely a 512-pixel-wide source image. The game streams
columns from a **2048-pixel-wide panorama** into a 512-pixel-wide tilemap.
The course selector at 0x46e8/0x46f0 indexes four backdrop entries at 0x4770.
Each entry has eight source maps, each 32 tiles / 256 pixels wide.

| Entry | Original source size | Content | Usage |
| --- | --- | --- | --- |
| Beginner, course ID 0 | 2048 x 392 | Clouds, mountains and grassland | Verified in race |
| Advanced, course ID 2 | 2048 x 344 | Clouds and cloud-covered lower section | Verified in race |
| Expert, course ID 1 | 2048 x 432 | Clouds, horizon and ocean | Verified in race |
| Fourth slot, ID 3 | 2048 x 528 | Blue sky, distant trees and green ground | ROM entry found; in-game use unconfirmed |

That is **four panorama art sets / 32 source sections**, of which three are
confirmed in the playable courses. The fourth was absent from the 45,000
frames checked; this does not establish that it is unused in every mode.
Menus, logos and HUD artwork are separate 2D assets and not included in
this panorama count. Deluxe '93 has not been extracted.

For the three race sets, the source begins at tilemap row 6 (48 pixels).
The gallery includes the exact source image and a 2048 x 512 version using
the original top/bottom fills. The fourth retains its complete 528-pixel
source height; it is not silently cropped to fit the prototype.

The changing 512 x 512 snapshots are not new backgrounds: their upper
columns get replaced as the camera turns. This accounts for the apparent
internal seams in an unscrolled tilemap dump. A single such dump is not
the complete artwork and must not be used as the replacement source.

## Extraction and checks

`m2gpushot --dump-tiles DIR` exports all four decoded 512 x 512 layers'
palette indices and category/opacity flags at the requested capture
interval, alongside the renderer's palette/register snapshots, character
RAM and the Revision A backdrop selector. It changes no guest state or
renderer behaviour. Raw inspection images include inactive tiles and
filler areas, so they are not final screen composites.

Three 9,000-frame course runs and an 18,000-frame attract run produced 300
snapshots (1,200 layer snapshots). The course IDs at frame 3600 are checked.
An initial pass found 135 distinct RGB states across layers 2/3, including
scrolling, menus and fades; that number is not an asset count.

`extract_backdrop_sources.py` follows each ROM descriptor's eight maps,
decodes their 8 x 8, 4-bit character tiles using the original palette,
and joins the original sections in table order. No image synthesis or
seam repair is performed. All 64 captured tile columns in each of the
three courses match the corresponding congruent columns of the extracted
panorama: 192 checks. Every ROM character-upload block also matches its
captured RAM bytes. All three full frame-3600 screenshots remain identical
before/after adding source dumping.

The fourth entry is decoded directly from its ROM character-upload list
and original palette. Its preview uses the shared 32-value colour transfer
recovered from palette words and renderer output across the three races;
all observed channels agree. Its in-game composition has not been verified.

Descriptor slots: 0x2600020 / 0x2600060 / 0x2600040 / 0x2600080 for
Beginner / Advanced / Expert / fourth. Source tables: 0x2074240 /
0x2081180 / 0x2074268 / 0x20811a8. Column streaming is implemented at
0x1dfd0..0x1e258; source selection uses three high phase bits, producing
eight 256-pixel sections, while the hardware scroll retains nine bits.

## Reproduce

Build the Revision A capture tool and use the existing verified cabinet
snapshot. The extractor needs Pillow and NumPy:

```powershell
cmake --build build-daytona --config Release --target m2gpushot --parallel 4
python scripts/backdrop_inventory.py --nvram traces/widescreen-32x9/nvram --output traces/backdrop-inventory-repeat
python scripts/extract_backdrop_sources.py --sources traces/backdrop-inventory-repeat --output traces/backdrop-inventory-repeat/artwork
```

Use a Python environment containing Pillow and NumPy for the last command.
The recorded run used the bundled workspace Python. The gallery, individual
PNGs, all 32 sections, source-address/hash records and a ZIP are written
to the artwork directory. Initial capture manifests and the additional
source captures record their respective executable hashes and commands.

The 4 October disk cleanup retained the artwork gallery and ZIP, compact
results, and the three minimal frame-3600 source snapshots (characters,
palette, layer-2 indices and selector metadata). Bulk captures were removed.
The retained sources can regenerate the artwork without another replay:

```powershell
python scripts/extract_backdrop_sources.py --sources traces/backdrop-inventory/sources --output traces/backdrop-artwork-repeat
```

Use a fresh output directory for any full capture/validation rerun.

## Implication for widescreen

Reusing the complete originals through the panorama cache is now the
first candidate. Replacement artwork is not yet justified. Palette fades,
vertical split/filler composition, angular alignment and wraparound still
need in-game testing with these originals. The earlier proof established
the cache/sampling mechanism using test art; it did not establish that new
artwork was required. No original-art renderer integration is made here.
