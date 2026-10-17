# Native PSP movie assets

`tools/prepare_psp_movies.py` converts the original USA disc's
`MOVIE/MV_%03u.PSS` files on the build host. The PSP receives AVC video for
`sceMpeg` and Sony ADPCM for `sceSasCore`; no movie decoder or audio mixer runs
in game code.

```sh
python3 tools/prepare_psp_movies.py \
  --iso 'iso/Resident Evil - Code - Veronica X (USA).iso' \
  --output build/psp/data --ffmpeg /path/to/ffmpeg
python3 tests/prepare_psp_movies_test.py
```

Pass `--movie 0` to prepare the opening movie first. Repeat `--movie` to select
several movies, or omit it to prepare all disc movies. FFmpeg must include
`libx264` and the ADS demuxer. Discovery supports `--ffmpeg`, `$FFMPEG`, PATH,
and the optional build-host `imageio_ffmpeg` package. The script does not
install or download dependencies. Converted movie data stays in the ignored
build directory. Existing outputs are cached by source hash and cooker version.

## Runtime contract

Each original movie ID produces three files:

- `movies/MV_000.pmf`: PSMF0014, one AVC stream E0, 480×272, 30000/1001 fps.
  Video is constrained Baseline level 3.0, YUV420, one reference frame, no B
  frames, AUD on each access unit, and repeated parameter sets at IDRs.
- `movies/MV_000.vag`: existing RVAG version 1, 64-byte header, stereo 44100 Hz.
  Channels occupy separate contiguous blocks of native 16-byte Sony ADPCM
  frames. The per-channel size includes a terminal block with flag 7. Samples
  excludes terminal/padding samples; loop and loop-start fields are zero.
- `movies/MV_000.json`: source/output hashes, frame count, exact audio sample
  count and source A/V offset, dimensions, and presentation timestamps.

The native player reads small chunks of each RVAG channel into a bounded
SAS streaming buffer; it does not need to load the entire movie's audio.
Output PTS begins at 90000 and advances 3003 ticks per frame. Each access unit's
first PES packet carries its PTS; continuation packets retain exact elementary
bytes. The last stream sector includes the MPEG program-end marker so the
native player can finish and flush. Stream offset and size are big-endian at
PSMF offsets 8 and 12; both are multiples of 2048 bytes. The first 2048 bytes
are the PSMF header. The first/last presentation times occupy six bytes each at
0x54 and 0x5a. Width/height are 16-pixel units in the single stream descriptor.
These fields follow the [PPSSPP PSMF parser](https://github.com/hrydgard/ppsspp/blob/master/Core/HLE/scePsmf.cpp)
and the [PSMF container investigation](https://github.com/TotalKommando/psp-media-toolkit/blob/master/pspmedia.py).
The PS pack, system, PES, padding, and end packets use MPEG-2 Systems framing.

Cooker version 2 also writes Sony private-stream-2 (`0xBF`) access-unit size
tables before each IDR group. The table covers every encoded picture, counts
only elementary video bytes and ends each group with the final-entry flag.
Without these tables, generic FFmpeg/PPSSPP playback can succeed while the real
PSP's `sceMpegGetAvcAu` waits until the input ring fills and reports no data.
The layout is documented by the [PSP media toolkit's reference muxer](https://github.com/TotalKommando/psp-media-toolkit/blob/master/pspmedia.py).
The runtime rejects old files with `missing native frame index; recook
data/movies`. Rerun the preparation command above, then copy **both** the new
EBOOT and `data/movies` to the PSP. Verified version 1 assets are remuxed without
re-encoding video or changing the native ADPCM soundtrack.

The original PS2 `MovieTypeDef` and `MovieDef` tables determine placement.
The raw MPEG-2 stream's sample aspect ratio does not describe the original
on-screen movie rectangle. The cooker places that authored rectangle inside
a centered 4:3 canvas; movie 0 is 362×194 at (58,38) on the PSP framebuffer.

## Audio timeline

The disc uses a private BD stream. The original PS2 `pcmCallback` skips four
bytes in each private payload; concatenating the remainder reconstructs Sony
ADS (`SShd` / `SSbd`) with stereo 48000 Hz PCM and 512-byte channel interleave.
The build host demuxes this data, resamples to 44100 Hz, and reuses the existing
Sony ADPCM encoder through its explicit `--pcm` input mode. Any difference
between the first audio/video PES timestamps is applied as sample-accurate
padding or trimming on the host. The output audio starts at video time zero
and contains `round(frames * 3003 * 44100 / 90000)` samples per channel.

## Validation

Every conversion checks the final program stream against the complete source
AVC elementary stream and fully decodes the PMF with host FFmpeg before
publishing assets. Unit tests cover timestamp markers, multi-sector access
units, container bounds, original PCM extraction, malformed streams, and the
RVAG channel/terminator contract. The PCM encoder test uses ASan/UBSan and
checks reconstruction error against synthetic stereo input on the host.
Native firmware playback still requires testing on a physical PSP.
The native adapter test now also checks rejection and cleanup for legacy
streams without frame tables; the host mux test independently verifies every
table entry against the actual multi-sector frame lengths and IDR groups.

## Build and playback

`make game-assets FFMPEG=/path/to/ffmpeg` prepares the boot resources and all
movies from the disc selected by `ISO`. `make game-movie-assets` cooks only
movies. `make game` builds the PSP package. Copy `build/psp/EBOOT.PBP` and the
complete `build/psp/data` directory, including `data/movies`, to the PSP.

The player uses `sceMpeg` with a 256-sector input ring and two aligned video
surfaces, and streams the soundtrack through a 16 KiB compressed stereo SAS
ring. Consumed halves are refilled while the audio lock excludes synthesis;
ADPCM predictor state remains in the firmware across ring wraps. Movie
surfaces are released after playback, once any queued GU use completes. The audio
sample clock schedules video presentation. Neither file is loaded whole.
Each actual decoded picture receives its time from the authored 29.97 fps
timeline. Firmware access-unit timestamps can repeat or be missing, so they do
not determine picture times. The PSMF duration sets the expected picture count;
extra or missing decoded pictures remain errors after decoder drain.
The original event requests, movie task pause/restore, cancel buttons and
30-frame cancellation fade are retained.

The cooker places MPEG `program_end_code` (`B9`) in a separate final sector
after an otherwise empty padding packet. The native ring parser can return
`80618007` when it reaches that marker before the sector boundary. On open,
the adapter recognizes and excludes this exact padding-only tail from ring
input, then drains the decoder after all video sectors have been consumed.
The authored picture count remains mandatory. Other illegal streams, malformed
trailers, read failures and missing pictures remain errors. Existing version 2
movie files work with this runtime fix; no asset regeneration is needed.

The native adapter supplies four valid destination pointers to both AVC decode
and drain. The 6.61 firmware walks this list once for each returned picture
before returning to the caller; a single stack pointer cannot safely handle
multi-picture output. Three extra aligned 512×272 RGB surfaces (about 1.6 MiB)
hold additional pictures, returned on subsequent calls in presentation order.
The GE-facing two-buffer ownership rule remains unchanged. Hardware CSC writes
through uncached aliases, with cache maintenance around CPU access. MPEG
initialization, decode and teardown share the SAS lock so firmware codec calls
cannot overlap synthesis. Failure cleanup uses the already-held lock.

The hardware report at `sceMpeg_library + 0x7984` matches the
`sceMpegBaseCscAvc` import in the locally inspected 6.61 module. This identifies
the RGB conversion boundary, but does not alone prove which input caused the
exception. Regression tests exercise two- and four-picture output, queue order,
drain, cache ownership and lock release; a physical PSP rerun is still required.

Validation targets are `game-movie-assets-test`, `game-boot-movie-psp-test`,
`game-boot-movie-audio-test`, `game-boot-movie-test`,
`game-boot-event-movie-test`, and `game-boot-gu-test`. The native decoder tests
use typed SDK mocks; the full host boot uses a separate timestamp-only codec
call model, with no video pixel decoding. These tests cannot establish actual
firmware playback. A physical PSP run remains necessary to verify the decoder,
SAS ring refill, picture placement and synchronization together.
