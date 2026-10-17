"""Cook original USA disc PSS movies for native PSP AVC and SAS playback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import mkiso
from tools.prepare_psp_audio import converter
from tools.psp_movie_mux import FRAME_TICKS,FIRST_PTS,mux,packets

VERSION = 1


def find_ffmpeg(explicit=None):
    candidates = [explicit,os.environ.get('FFMPEG'),shutil.which('ffmpeg')]
    try:
        import imageio_ffmpeg
        candidates.append(imageio_ffmpeg.get_ffmpeg_exe())
    except ImportError:
        pass
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and os.access(candidate,os.X_OK): return str(candidate)
    raise ValueError('FFmpeg with libx264 required: pass --ffmpeg or install imageio-ffmpeg on the build host')


def run(command):
    result = subprocess.run([str(x) for x in command],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError('host conversion failed: '+result.stderr.decode(errors='replace')[-4000:])


def movie_definitions():
    source = (ROOT/'src/ps2/veronica/prog/sdfunc.c').read_text()
    types = re.search(r'MovieTypeDef\[22\]\s*=\s*\{([^}]+)',source).group(1)
    values = [int(x.strip(),0) for x in types.split(',') if x.strip()]
    definitions = re.search(r'MovieDef\[4\].*?=\s*\{(.*?)\};',source,re.S).group(1)
    entries = [tuple(map(int,re.findall(r'\d+',entry))) for entry in re.findall(r'\{([^}]+)\}',definitions)]
    return values,entries


def extract_ads(source):
    audio = bytearray()
    first_video = first_audio = None
    for code,payload,pts in packets(source):
        if code == 0xe0 and first_video is None: first_video = pts
        if code == 0xbd:
            if len(payload) < 4 or payload[1] != 0xa0:
                raise ValueError('unsupported PSS private audio substream')
            if first_audio is None: first_audio = pts
            audio += payload[4:]  # Original ps2_MovieFunc.c::pcmCallback skips four bytes.
    if first_video is None or first_audio is None or len(audio) < 40:
        raise ValueError('movie requires timestamped video and PCM streams')
    if audio[:4] != b'SShd' or audio[32:36] != b'SSbd' or int.from_bytes(audio[8:12],'little') != 1:
        raise ValueError('unsupported Sony ADS movie audio')
    rate = int.from_bytes(audio[12:16],'little')
    channels = int.from_bytes(audio[16:20],'little')
    interleave = int.from_bytes(audio[20:24],'little')
    size = int.from_bytes(audio[36:40],'little')
    if channels != 2 or rate != 48000 or interleave != 512 or size > len(audio)-40:
        raise ValueError('unexpected/truncated original movie PCM layout')
    return bytes(audio[:40+size]),first_audio-first_video


def cook(source,number,output,ffmpeg,executable):
    types,definitions = movie_definitions()
    if number >= len(types): raise ValueError('movie index exceeds original table')
    definition = definitions[types[number]&31]
    _,_,_,original_y,_,original_height,_ = definition
    # A centered 4:3 image area in the 480x272 framebuffer, with the authored
    # 640x448 movie display rectangle retained inside it (coded DAR is wrong).
    width = 362
    height = round(original_height*272/448/2)*2
    top = round(original_y*272/448/2)*2
    if not height or top+height > 272: raise ValueError('unsupported movie display rectangle')
    digest = hashlib.sha256(source).hexdigest()
    signature = {'version':VERSION,'source_sha256':digest,'width':width,'height':height,'top':top}
    directory = output/'movies'; directory.mkdir(parents=True,exist_ok=True)
    stem = f'MV_{number:03d}'
    target = directory/(stem+'.pmf'); audio_target = directory/(stem+'.vag'); metadata = directory/(stem+'.json')
    if target.exists() and audio_target.exists() and metadata.exists():
        old = json.loads(metadata.read_text())
        if old.get('signature') == signature:
            print(f'{stem}: cached',flush=True); return old
    with tempfile.TemporaryDirectory(prefix=stem+'-',dir=directory) as temp:
        temp = Path(temp); original = temp/(stem+'.PSS'); original.write_bytes(source)
        es = temp/'video.h264'
        run([ffmpeg,'-hide_banner','-loglevel','error','-y','-i',original,'-map','0:v:0','-an',
            '-vf',f'scale={width}:{height}:flags=lanczos,setsar=1,pad=480:272:58:{top}:black',
            '-r','30000/1001','-fps_mode','cfr','-c:v','libx264','-preset','fast','-profile:v','baseline',
            '-level:v','3.0','-pix_fmt','yuv420p','-b:v','1200k','-maxrate','1600k','-bufsize','1600k',
            '-x264-params','bframes=0:ref=1:aud=1:repeat-headers=1:keyint=30:min-keyint=30:scenecut=0',
            '-f','h264',es])
        video,frames = mux(es.read_bytes())
        (temp/'video.pmf').write_bytes(video)
        ads,offset = extract_ads(source); (temp/'audio.ads').write_bytes(ads)
        samples = round(frames*FRAME_TICKS*44100/90000)
        adjustment = round(offset*44100/90000)
        filters = ['aresample=44100']
        if adjustment > 0: filters += [f'adelay={adjustment}S:all=1']
        elif adjustment < 0: filters += [f'atrim=start_sample={-adjustment}','asetpts=PTS-STARTPTS']
        filters += ['apad',f'atrim=end_sample={samples}']
        run([ffmpeg,'-hide_banner','-loglevel','error','-y','-i',temp/'audio.ads','-af',','.join(filters),
             '-ar','44100','-ac','2','-f','s16le',temp/'audio.pcm'])
        run([executable,'--pcm','2','44100',temp/'audio.pcm',temp/'audio.vag'])
        with (temp/'audio.vag').open('r+b') as audio:
            audio.seek(32); audio.write(bytes.fromhex(digest))
        # Independently demux the generated PS and require exact elementary data.
        rebuilt = b''.join(payload for code,payload,_ in packets(video,2048) if code == 0xe0)
        if rebuilt != es.read_bytes(): raise ValueError('movie PS round-trip mismatch')
        run([ffmpeg,'-hide_banner','-loglevel','error','-i',temp/'video.pmf','-map','0:v:0','-f','null','-'])
        result = {'signature':signature,'movie':number,'frames':frames,'fps_numerator':30000,
            'fps_denominator':1001,'first_pts':FIRST_PTS,'last_pts':FIRST_PTS+frames*FRAME_TICKS,
            'audio_rate':44100,'audio_channels':2,'audio_samples':samples,
            'source_audio_offset_ticks':offset,'audio_offset_ticks':0,
            'video_bytes':len(video),'audio_bytes':(temp/'audio.vag').stat().st_size,
            'video_sha256':hashlib.sha256(video).hexdigest(),
            'audio_sha256':hashlib.sha256((temp/'audio.vag').read_bytes()).hexdigest()}
        (temp/'video.pmf').replace(target); (temp/'audio.vag').replace(audio_target)
        metadata.write_text(json.dumps(result,indent=2)+'\n')
    print(f'{stem}: {frames} frames, {samples} stereo samples; native AVC + ADPCM',flush=True)
    return result


def prepare(iso_path,output,ffmpeg,movies=None):
    executable = converter()
    result = []
    with iso_path.open('rb') as iso:
        mkiso.check_iso(iso)
        entries = mkiso.dump_dir_records(iso,mkiso.LAYER0_PVD_LOCATION,0).files
        if not any(e.path.name=='SLUS_201.84' for e in entries): raise ValueError('USA SLUS_201.84 disc required')
        for entry in sorted(entries,key=lambda e:str(e.path)):
            match = re.fullmatch(r'MV_(\d{3})\.PSS',entry.path.name,re.I)
            if not match: continue
            number = int(match.group(1))
            if movies is not None and number not in movies: continue
            iso.seek(entry.lba); source = iso.read(entry.size)
            if len(source) != entry.size: raise ValueError('short original movie read')
            result.append(cook(source,number,output,ffmpeg,executable))
    if movies is not None and set(movies) != {r['movie'] for r in result}: raise ValueError('requested movie absent from disc')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--ffmpeg')
    parser.add_argument('--movie',type=int,action='append',help='restrict conversion; repeatable; default all disc movies')
    args = parser.parse_args()
    try: prepare(args.iso,args.output,find_ffmpeg(args.ffmpeg),args.movie)
    except (OSError,ValueError,subprocess.SubprocessError) as error: parser.exit(1,f'Movie preparation failed: {error}\n')
