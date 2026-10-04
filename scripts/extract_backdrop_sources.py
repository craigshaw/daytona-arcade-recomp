#!/usr/bin/env python3
"""Recover full Revision A course panoramas from ROM tile maps and local captures.

Requires numpy and Pillow. No pixel synthesis: decode the captured character RAM
with the game's palette, using all eight original source-map sections.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom-dir', type=Path, default=ROOT/'build-daytona/rom_cache/daytona')
    parser.add_argument('--sources', type=Path, required=True, help='per-course --dump-tiles snapshots or backdrop_inventory output')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if subprocess.run(['git','check-ignore','-q',str(args.output.resolve())],cwd=ROOT).returncode:
        raise SystemExit('Output must be git-ignored: game-derived artwork is local only')
    args.output.mkdir(parents=True,exist_ok=True)
    program = (args.rom_dir/'program.bin').read_bytes()
    data = (args.rom_dir/'main_data.bin').read_bytes()
    def read(address, length):
        if 0 <= address < 0x200000:
            region, offset = program, address
        elif 0x220000 <= address < 0x240000:
            region, offset = program, address-0x200000
        elif 0x2000000 <= address < 0x4000000:
            region, offset = data, address-0x2000000
        else:
            raise ValueError(f'Unmapped ROM address {address:#x}')
        result = region[offset:offset+length]
        if len(result) != length:
            raise ValueError('Source exceeds ROM extent')
        return result
    def u32(address):
        return struct.unpack('<I',read(address,4))[0]
    result=[]
    transfer={}
    for course, course_id in [('beginner',0),('advanced',2),('expert',1)]:
        folder=args.sources/course
        if (folder/'tiles').is_dir():
            folder=folder/'tiles'
        prefix=folder/'frame_03600'
        meta=json.loads(Path(str(prefix)+'_source.json').read_text())
        assert meta['course']==course_id
        slot=meta['descriptor_slot']
        # The course selector at 0x46e8/0x46f0 indexes this three-entry table.
        assert u32(0x4770+course_id*4)==slot
        table=u32(slot)
        chars=np.fromfile(str(prefix)+'_characters.bin',dtype='u1')
        pens=np.fromfile(str(prefix)+'_pens.bin',dtype='<u4')
        assert len(chars)==0x80000 and len(pens)==4096
        # Character upload lists are (source, destination) pairs. The source
        # starts with its count of 32-byte characters. Verify the complete ROM
        # banks against captured RAM, not just the currently displayed tiles.
        upload=u32(table)
        while u32(upload):
            source,destination=u32(upload),u32(upload+4)
            size=u32(source)*32
            offset=destination-0x1080000
            assert bytes(chars[offset:offset+size])==read(source+4,size)
            upload+=8
        # Recover this race's colour transfer from original palette words and
        # captured pens. All RGB channels agree on the same 32-value mapping;
        # use it to preview the fourth ROM entry without inventing a palette.
        palette=u32(table+4)
        while u32(palette):
            destination,count=u32(palette),u32(palette+4)
            first=(destination-0x1800000)//2
            for i,value in enumerate(np.frombuffer(read(palette+8,count*2),dtype='<u2')):
                for channel in range(3):
                    code=(int(value)>>(channel*5))&31
                    colour=(int(pens[first+i])>>(16-channel*8))&255
                    assert code not in transfer or transfer[code]==colour
                    transfer[code]=colour
            palette+=8+count*2
        blocks=[]
        descriptors=[]
        for section in range(8):
            address=u32(table+8+section*4)
            header,height,width=struct.unpack('<III',read(address,12))
            assert width==32 and 0<height<=58
            tiles=np.frombuffer(read(address+12,width*height*2),dtype='<u2').reshape(height,width)
            code=(tiles&0x3fff).astype(np.int64)
            colour=(tiles>>7)&255
            pixels=np.empty((height*8,width*8),dtype=np.uint16)
            for y in range(8):
                for x in range(8):
                    byte=chars[(code*32+y*4+x//2)^1]
                    nibble=(byte&15) if x&1 else byte>>4
                    pixels[y::8,x::8]=colour*16+nibble
            blocks.append(pixels)
            descriptors.append({'section':section,'address':hex(address),'tiles':[width,height],
                                'source_sha256':hashlib.sha256(read(address,12+width*height*2)).hexdigest()})
        source=np.concatenate(blocks,axis=1)
        def rgb(indices):
            p=pens[indices]
            return np.stack([(p>>16)&255,(p>>8)&255,p&255],axis=2).astype('uint8')
        source_image=Image.fromarray(rgb(source))
        source_image.save(args.output/f'{course}-panorama-source.png')
        for section,block in enumerate(blocks):
            Image.fromarray(rgb(block)).save(args.output/f'{course}-section-{section+1:02d}.png')
        # The original update routine writes source rows from tilemap byte
        # offset 0x300: six rows = 48 pixels. Retain original top/bottom fills.
        original=np.fromfile(str(prefix)+'_layer2_indices.bin',dtype='<u2').reshape(512,512)
        full=np.tile(original[:,0:1],(1,2048))
        full[48:48+source.shape[0]]=source
        Image.fromarray(rgb(full)).save(args.output/f'{course}-panorama-2048x512.png')
        # Every displayed tile column must occur at its congruent world column
        # in one of the four 512-pixel quarters. This detects decode, ordering,
        # palette and placement errors without using an invented visual target.
        matches=[]
        for x in range(0,512,8):
            strip=original[48:48+source.shape[0],x:x+8]
            candidates=[x+q*512 for q in range(4) if np.array_equal(strip,source[:,x+q*512:x+q*512+8])]
            if not candidates:
                raise RuntimeError(f'{course}: live tile column {x} absent from extracted panorama')
            matches.append({'tilemap_x':x,'source_x':candidates})
        info={'course':course,'course_id':course_id,'descriptor_slot':hex(slot),'table':hex(table),
              'source_size':list(source_image.size),'canvas_size':[2048,512],'source_y':48,
              'sections':descriptors,'verified_tile_columns':len(matches),'column_matches':matches,
              'image_sha256':hashlib.sha256((args.output/f'{course}-panorama-source.png').read_bytes()).hexdigest(),
              'input_sha256':{name:hashlib.sha256(Path(str(prefix)+suffix).read_bytes()).hexdigest()
                              for name,suffix in [('characters','_characters.bin'),('palette','_pens.bin'),('live_layer','_layer2_indices.bin')]}}
        result.append(info)
        print(course,source_image.size,'verified columns',len(matches),flush=True)
    # A fourth entry follows the three playable course selectors. Preserve its
    # complete 66-row source, rather than truncating it to our 512-high canvas.
    assert len(transfer)==32
    slot=u32(0x4770+3*4)
    table=u32(slot)
    chars=np.zeros(0x80000,dtype='u1')
    upload=u32(table)
    while u32(upload):
        source,destination=u32(upload),u32(upload+4)
        size=u32(source)*32
        offset=destination-0x1080000
        chars[offset:offset+size]=np.frombuffer(read(source+4,size),dtype='u1')
        upload+=8
    pens=np.zeros(4096,dtype='<u4')
    palette=u32(table+4)
    while u32(palette):
        destination,count=u32(palette),u32(palette+4)
        first=(destination-0x1800000)//2
        for i,v in enumerate(np.frombuffer(read(palette+8,count*2),dtype='<u2')):
            v=int(v)
            pens[first+i]=0xff000000|(transfer[v&31]<<16)|(transfer[(v>>5)&31]<<8)|transfer[(v>>10)&31]
        palette+=8+count*2
    blocks=[]
    descriptors=[]
    for section in range(8):
        address=u32(table+8+section*4)
        _,height,width=struct.unpack('<III',read(address,12))
        assert height==66 and width==32
        tiles=np.frombuffer(read(address+12,width*height*2),dtype='<u2').reshape(height,width)
        code=(tiles&0x3fff).astype(np.int64)
        colour=(tiles>>7)&255
        pixels=np.empty((height*8,width*8),dtype=np.uint16)
        for y in range(8):
            for x in range(8):
                byte=chars[(code*32+y*4+x//2)^1]
                pixels[y::8,x::8]=colour*16+((byte&15) if x&1 else byte>>4)
        block=rgb(pixels)
        blocks.append(block)
        Image.fromarray(block).save(args.output/f'fourth-slot-section-{section+1:02d}.png')
        descriptors.append({'section':section,'address':hex(address),'tiles':[width,height],
                            'source_sha256':hashlib.sha256(read(address,12+width*height*2)).hexdigest()})
    Image.fromarray(np.concatenate(blocks,axis=1)).save(args.output/'fourth-slot-panorama-source.png')
    result.append({'course':'fourth-slot','course_id':3,'usage':'unconfirmed; absent from captured course/attract replays',
                   'descriptor_slot':hex(slot),'table':hex(table),'source_size':[2048,528],
                   'palette':'original ROM palette; shared 32-value colour transfer verified against three live races',
                   'sections':descriptors,'verified_tile_columns':None,
                   'image_sha256':hashlib.sha256((args.output/'fourth-slot-panorama-source.png').read_bytes()).hexdigest()})
    print('fourth slot (2048, 528), ROM extraction; in-game use unverified',flush=True)
    (args.output/'sources.json').write_text(json.dumps(result,indent=2))
    labels={'beginner':('Beginner','Blue sky, varied clouds, mountains and grassland.'),
            'advanced':('Advanced','Deep blue sky and varied cloud formations above a cloud-covered lower section.'),
            'expert':('Expert','Clouds, a distant horizon and ocean.'),
            'fourth-slot':('Fourth ROM slot — usage unconfirmed','Blue sky, distant trees and green ground. Not observed in the captured course/attract replays.')}
    cards=[]
    for number,item in enumerate(result,1):
        name=item['course']
        title,description=labels[name]
        width,height=item['source_size']
        section_links=''.join(f'<a href="{name}-section-{n:02d}.png"><img loading="lazy" src="{name}-section-{n:02d}.png" alt="Section {n}"><span>Section {n} / 8</span></a>' for n in range(1,9))
        padded=f' · <a href="{name}-panorama-2048x512.png">2048 × 512 with original fills</a>' if name!='fourth-slot' else ''
        verification='All 64 captured tile columns and complete character banks verified against the live race.' if name!='fourth-slot' else 'Original ROM tiles and palette; colour transfer matches the three race captures. In-game use remains unverified.'
        cards.append(f'''<article><div class="heading"><span class="number">0{number}</span><div><h2>{title}</h2><p>{description}</p></div>
<span class="size">{width} × {height}</span></div>
<a href="{name}-panorama-source.png"><img class="panorama" src="{name}-panorama-source.png" alt="{title} complete original panorama"></a>
<p class="links"><a href="{name}-panorama-source.png" download>Download original PNG</a>{padded}</p>
<p class="verification">{verification}</p><details><summary>Show the eight original 256-pixel sections</summary><div class="sections">{section_links}</div></details></article>''')
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Original Daytona backdrop inventory</title><style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101923;color:#edf3fc}body{max-width:1600px;margin:auto;padding:36px 28px 64px}
h1{font-size:clamp(30px,4vw,46px);margin:10px 0}h2{font-size:23px;margin:0}p{color:#b8c8dc;line-height:1.6;margin:8px 0}
.intro{max-width:1050px}.eyebrow{font-size:12px;letter-spacing:.16em;color:#72d5c7;text-transform:uppercase}
.facts{display:flex;gap:12px;flex-wrap:wrap;margin:22px 0}.facts span{padding:10px 15px;background:#203244;border:1px solid #3c536e;border-radius:8px}
article{margin:28px 0;background:#162333;border:1px solid #354c66;border-radius:12px;overflow:hidden}.heading{display:flex;gap:18px;align-items:center;padding:20px}
.number{font-size:28px;color:#72d5c7;font-weight:700}.size{margin-left:auto;white-space:nowrap;color:#aebfd2}.panorama{width:100%;height:auto;display:block;image-rendering:pixelated;background:#080f18}
a{color:#80dfd0}.links,.verification{padding:0 20px}.verification{font-size:13px}details{margin:16px 20px 20px}summary{cursor:pointer;color:#d2e5f5}
.sections{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-top:16px}.sections img{width:100%;display:block;image-rendering:pixelated}.sections span{font-size:12px}
.note{padding:16px 20px;border-left:3px solid #e0c074;background:#272d34;max-width:1050px}.links{font-size:14px}
@media(max-width:650px){body{padding:20px 12px}.heading{flex-wrap:wrap}.size{margin-left:46px}.sections{grid-template-columns:repeat(2,1fr)}}
</style><div class="eyebrow">Local ROM extraction · Revision A · 4 October 2026</div>
<h1>The original artwork is already panoramic.</h1>
<p class="intro">The backdrop selector contains four panorama sets. Each uses eight 256-pixel source sections,
giving a complete 2,048-pixel-wide image. The game streams a moving subset into its 512-pixel tilemap.</p>
<div class="facts"><span>3 verified race panoramas</span><span>1 additional ROM panorama</span><span>32 source sections</span><span>Original pixels — no generated artwork</span></div>
<p class="note">For widescreen planning, these are four art sets, not the many different tilemap snapshots seen during play.
The first three are confirmed in the corresponding races. The fourth is present in the ROM selector but its use is unconfirmed.
Menus, logos and HUD are separate 2D artwork; they are not included in this panorama count.</p>
__CARDS__
<h2>What this changes</h2><p class="intro">We can first try displaying the complete original panorama through the cached background path.
New artwork may be unnecessary. Alignment, palette fades, layer composition and the full-width wrap still need to be tested in-game.
This extraction does not change the renderer or enable a new launcher feature.</p>
<p><a href="original-backdrops.zip" download>Download all extracted panorama PNGs and source records</a> · <a href="sources.json">Source addresses and verification</a></p>
<p class="intro">Evidence includes 45,000 replayed frames, 300 sampled snapshots of all four tilemap layers, and direct extraction of all eight sections for each panorama.
The 32-value colour transfer used for the fourth preview agrees with the original palette words and captured output from all three races.</p>
<p>These are local game-derived files. Deluxe ’93 has not been extracted.</p></html>'''
    (args.output/'index.html').write_text(page.replace('__CARDS__',''.join(cards)),encoding='utf-8')
    with zipfile.ZipFile(args.output/'original-backdrops.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(args.output.glob('*.png')):
            archive.write(path,path.name)
        archive.write(args.output/'sources.json','sources.json')


if __name__=='__main__':
    main()
