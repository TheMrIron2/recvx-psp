"""Host-only container, PCM extraction and native ADPCM asset contracts."""
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.psp_movie_mux import access_units,mux,packets,read_timestamp,timestamp
from tools.prepare_psp_movies import extract_ads,movie_definitions


def pes(code,payload,pts):
    optional = timestamp(pts)
    return b'\0\0\1'+bytes([code])+struct.pack('>H',8+len(payload))+b'\x80\x80\x05'+optional+payload


class Movies(unittest.TestCase):
    def test_timestamps(self):
        for value in (0,1,3003,90000,(1<<30)-1,(1<<33)-1):
            self.assertEqual(read_timestamp(timestamp(value)),value)
        with self.assertRaises(ValueError):timestamp(1<<33)
        with self.assertRaises(ValueError):read_timestamp(b'\0'*5)

    def test_sector_mux(self):
        # A multi-sector AU followed by two small pictures exercises continuation
        # packets and retains emulation-protected zero bytes exactly.
        units = [b'\0\0\0\1\x09\xf0\0\0\1\x65'+b'\x88'*6000,
                 b'\0\0\1\x09\xf0\0\0\1\x41'+b'\x88'*17,
                 b'\0\0\1\x09\xf0\0\0\1\x41'+b'\x88'*2100]
        data,count = mux(b''.join(units))
        self.assertEqual(count,3)
        self.assertEqual(len(data)%2048,0)
        self.assertEqual(data[:8],b'PSMF0014')
        self.assertEqual(struct.unpack_from('>II',data,8),(2048,len(data)-2048))
        self.assertEqual(data[0x8e:0x90],bytes([30,17]))
        self.assertEqual(int.from_bytes(data[0x5a:0x60],'big'),99009)
        decoded = list(packets(data,2048))
        self.assertEqual(b''.join(p for c,p,t in decoded),b''.join(units))
        self.assertEqual([t for c,p,t in decoded if t is not None],[90000,93003,96006])
        for p in range(2048,len(data),2048):self.assertEqual(data[p:p+4],b'\0\0\1\xba')
        self.assertEqual(data[-4:],b'\0\0\1\xb9')
        with self.assertRaises(ValueError):list(packets(data[:-20],2048))

    def test_bad_access_units(self):
        for data in (b'',b'\0\0\1',b'\0\0\1\x65\xff',b'\0\0\1\x09\xf0'):
            with self.assertRaises(ValueError):list(access_units(data))

    def test_native_frame_tables(self):
        units = [b'\0\0\1\x09\xf0\0\0\1'+bytes([kind])+b'\x88'*size
                 for kind,size in ((0x65,7000),(0x41,300),(0x41,5000),(0x65,4000),(0x41,10))]
        data,count = mux(b''.join(units))
        # Independently walk the packet lengths: BF is not a PES packet and
        # does not have optional timestamp headers. Firmware consumes its
        # byte counts to split E0 data into decoder access units.
        tables=[];position=2048
        while position<len(data):
            self.assertEqual(data[position:position+3],b'\0\0\1')
            kind=data[position+3]
            if kind==0xb9:break
            if kind==0xba:
                position+=14+(data[position+13]&7);continue
            size=int.from_bytes(data[position+4:position+6],'big')
            payload=data[position+6:position+6+size]
            if kind==0xbf:
                self.assertEqual(payload[:2],b'\x01\xe0')
                length,n=struct.unpack_from('>HH',payload,14)
                self.assertEqual(length,2+4*n);self.assertEqual(len(payload),18+4*n)
                records=[struct.unpack_from('>HH',payload,18+i*4) for i in range(n)]
                self.assertEqual([f for f,_ in records],[0x80]*(n-1)+[0])
                tables.append([s for _,s in records])
            position+=6+size
        self.assertEqual(tables,[[len(u) for u in units[:3]],[len(u) for u in units[3:]]])
        self.assertEqual(sum(map(len,tables)),count)
        self.assertEqual(b''.join(p for _,p,_ in packets(data,2048)),b''.join(units))
        oversized=b'\0\0\1\x09\xf0\0\0\1\x65'+b'\x88'*65536
        with self.assertRaisesRegex(ValueError,'frame-size'):mux(oversized)

    def test_original_pcm_wrapper(self):
        ads = b'SShd'+struct.pack('<IIIIIII',24,1,48000,2,512,0xffffffff,0xffffffff)+b'SSbd'+struct.pack('<I',2048)+bytes(2048)
        data = pes(0xe0,b'video',9000)+pes(0xbd,b'\xff\xa0\0\0'+ads[:50],9010)+pes(0xbd,b'\xff\xa0\0\0'+ads[50:],10000)+b'\0\0\1\xb9'
        extracted,offset = extract_ads(data)
        self.assertEqual(extracted,ads);self.assertEqual(offset,10)
        with self.assertRaises(ValueError):extract_ads(data[:-100])
        with self.assertRaises(ValueError):extract_ads(data.replace(b'SShd',b'bad!',1))
        types,definitions = movie_definitions()
        self.assertEqual(len(types),22)
        self.assertEqual(definitions[types[0]&31],(320,352,0,64,640,320,1))

    def test_native_pcm_encoder(self):
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory);exe=directory/'encoder';pcm=directory/'pcm';out=directory/'vag'
            subprocess.run([shutil.which('cc'),'-std=c11','-O1','-g','-fsanitize=address,undefined',
                '-fno-pie','-no-pie',str(ROOT/'tools/convert_psp_audio.c'),'-lm','-o',str(exe)],check=True)
            values=[(int(10000*math.sin(i*.15)),int(12000*math.cos(i*.1))) for i in range(113)]
            pcm.write_bytes(b''.join(struct.pack('<hh',*v) for v in values))
            sanitizer_env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'}
            subprocess.run([str(exe),'--pcm','2','44100',str(pcm),str(out)],check=True,env=sanitizer_env)
            data=out.read_bytes();header=struct.unpack_from('<7I',data,4)
            self.assertEqual(data[:4],b'RVAG');self.assertEqual(header,(1,2,44100,96,0,113,0))
            self.assertEqual(len(data),64+96*2)
            filters=((0,0),(60,0),(115,-52),(98,-55),(122,-60))
            for channel in range(2):
                plane=data[64+96*channel:64+96*(channel+1)]
                self.assertEqual(plane[-15],7)
                samples=[];h0=h1=0
                for block in range(0,80,16):
                    shift=plane[block]&15;f=filters[plane[block]>>4]
                    for i in range(28):
                        n=(plane[block+2+i//2]>>(4*(i%2)))&15
                        if n>=8:n-=16
                        sample=max(-32768,min(32767,n*(4096>>shift)+((h0*f[0]+h1*f[1])>>6)))
                        h1,h0=h0,sample;samples.append(sample)
                error=sum(abs(samples[i]-values[i][channel]) for i in range(113))/113
                self.assertLess(error,300)
            pcm.write_bytes(b'\0')
            self.assertNotEqual(subprocess.run([str(exe),'--pcm','2','44100',str(pcm),str(out)],env=sanitizer_env).returncode,0)


if __name__ == '__main__':unittest.main()
