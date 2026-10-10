# PSP full-game port audit

Audited 10 October 2026 against `86de5c39` plus the existing working-tree changes.
This is an architectural audit and implementation roadmap, not a claim of full-game compatibility or a line-by-line correctness review.

The next milestone should be an original scripted gameplay sequence on PSP: an animated player, original movement and collision, automatic camera cuts, an interaction, and a door transition with persistent state.
Build this by bringing the original game logic onto a small PSP platform layer. Keep the viewer as a diagnostic tool and its validated asset readers as a foundation.

## Evidence and current scope

The user reports three rooms rendering correctly on real hardware. This audit did not perform additional hardware testing.
The working tree also contains a walking prototype and automatic camera selection that go beyond the status in [PSP_PORT.md](PSP_PORT.md); their full gameplay correctness is not established by the room-rendering result.

Checks performed during this audit:

- `make -B -j4`: successful PSP rebuild with no compiler warnings in the log
- `make test`: all existing tests passed, including the C ASan/UBSan runner
- Full ISO room validation: 205/205 static room models, 17,340 model records, and 9,425 room textures passed; decompression matched the supplied C reference
- ASan/UBSan loading and constructing `RM_0000` scene, mesh, and textures passed
- ISO directory inspection found 18 `.PSS` movies and 12 `.AFS` archives

The full-room scan used the existing tool, with output in `/tmp/recvx-port-audit-data` to preserve the current playable asset catalog:

```sh
python3 tools/prepare_psp_assets.py --iso recvx.iso \
  --rooms RM_0000.RDX,RM_0010.RDX,RM_0020.RDX \
  --library build/host/librecvx-test.so \
  --output /tmp/recvx-port-audit-data --validate-all
ASAN_OPTIONS=detect_leaks=0 build/host/port-test /tmp/recvx-port-audit-data
```

These checks cover the static-room path. They do not validate every embedded actor, animation, script, texture group, audio bank, or playable room state.
The Python tests and all-room scan use the unsanitized shared library; the separate C runner supplies the sanitizer coverage.
There are currently no direct player-controller tests in `tests/`.

Since this audit, the initial new-game route and its player archive dependency
have been traced from the supplied USA ISO. The `route-audit` tool follows
active door records and verifies target spawn IDs: Claire starts in `RM_0000`
at spawn 0, then the first three forward doors lead to `RM_0010`, `RM_0020`,
and `RM_0030`, each at spawn 0. The return doors in `RM_0010` and `RM_0020`
lead to the prior room at spawn 1. This confirms the first four room filenames
as an actual connected route; it does not establish that every event or
progression flag in those rooms is implemented.

The walking prototype now owns active type-0 ETC door bounds and their target
stage/room/spawn values. CROSS at an overlapping trigger stops and drains the
renderer, reloads the target room, and spawns at the recorded position. It
keeps an exit lock until the player leaves any arrival trigger. This is a
diagnostic route transition only; the original direction checks, door
animation, event scripts, and persistent progression state are not yet ported.

`make player-assets` extracts the initial Claire model/skin, motion, player
extra, texture payloads, and default weapon by the entry formulas used in
`system.c` and `dread.c`. In this ISO, player entry 10 is 1,294,440 bytes:
53,872 bytes of model records (including three `SKIN` records), 251,472 bytes
of motion records (60 populated entries and 40 empty slots), 2,264 bytes of
player extra data, and 986,820 bytes of texture payload. Default weapon entry
20 is 267,536 bytes. A portable PSP reader now indexes the split model and
motion streams with bounds checks, preserving model indices, skin associations,
model object counts, motion slots, and frame counts. The PSP status screen
reports the parsed inventory when `player-assets/` is copied alongside the room
data. The reader retains the model and motion source buffers for future
rendering work. The first Claire chunk model now goes through the room mesh
decoder's hierarchy and triangle path and preserves its native room-unit scale
for an untextured static preview in walking mode. The model keeps its facing
while reverse input moves it backward. It does not apply skin weights, sample
motion keys, or load the player texture tail, so this is not an animated or
faithful Claire render yet.

[The Makefile](../Makefile) links only `flag.c` and `ps2_NaMem.c` from the original game.
The original game directory contains 154 C files and about 248,000 lines, excluding CRI and SDK sources. This measures integration scope, not remaining development effort.
The current PSP ELF has 192,732 bytes of text, 4,912 bytes of data, and 108,248 bytes of BSS; the complete game will have a substantially different executable footprint.

## Architectural findings

### 1. Preserve the original gameplay contracts

[main.c](../src/ps2/veronica/prog/main.c) supplies the globals and a 23-entry task dispatcher.
[system.c](../src/ps2/veronica/prog/system.c) drives loading, menus, gameplay, events, doors, movies, and saves.
[game.c](../src/ps2/veronica/prog/game.c), through `bhMainSequence`, orders enemy, player, effect, object, camera, lighting, drawing, and event work.

The PSP [player.c](../src/psp/player.c) is a new controller with fixed movement constants, gravity, and partial collision interpretation.
It copies the wall array but does not implement the original floor/other collision arrays, dynamic collision, movement states, animation coupling, or combat.
It is useful for navigation diagnostics, but extending it into the final controller would duplicate the substantial behavior already present in the decompilation.

Bring across `player.c`, `playpch.c`, `playpch2.c`, `pwksub.c`, `pl_evt.c`, `hitchk.c`, `hitchkl.c`, and `rutchk.c` incrementally.
Preserve movement, animation frame attributes, collision response, and flag behavior together.
Retain original object IDs and indices when converting render assets because gameplay, camera masks, and attachment logic refer to them.

### 2. Establish the ABI before adding gameplay modules

[types.h](../include/ps2/veronica/prog/types.h) directly includes Ninja, PS2 graphics, DMA, pad, memory-card, sound, and CRI headers.
Game headers inherit this dependency. The existing two portable modules largely avoid this problem.

Create portable game/Ninja type declarations and PSP service interfaces.
Implement the subset used by the next milestone, expanding it as real callers are integrated.
Keep the explicit source list and a record of unresolved functions; do not pull the entire PS2 SDK surface into the PSP build.

The binary readers and original relocation code assume 32-bit pointers and specific layouts. `bhSetRoom` copies 1,004 bytes of `ROM_WORK` and relocates offsets in place.
`bhMlbBinRealize` and `bhMnbBinRealize` also modify their inputs. PSP's 32-bit pointers help, but do not prove compiler ABI equivalence.

Add size/offset assertions for disk-facing structures, especially `ROM_WORK`, `CUT_WORK`, model/motion records, and save fields.
Check signed `char`, integer widths, alignment, bitfields, and function prototypes.
The existing [override_katana.h](../include/ps2/veronica/prog/override_katana.h) already documents a compiler-specific integer-width issue.
Audit pointer-to-integer casts and type punning; any transitional aliasing flags should be narrowly scoped. Avoid global fast-math until behavior comparisons support it.

Keep disk offsets separate from host pointers. Use explicit disk records and native runtime records for 64-bit host tests, or an intentionally configured
32-bit reference harness. Directly compiling the original relocators into a 64-bit host executable is not a valid compatibility test.

### 3. Room ownership must change for gameplay

[PSP main.c](../src/psp/main.c) frees the expanded RDX after constructing its viewer data. Original [room.c](../src/ps2/veronica/prog/room.c) retains data
through `sys->rdtp`, creates enemies/objects/items, installs event pointers, and completes motion setup in `bhFinishRoom`.
[event.c](../src/ps2/veronica/prog/event.c) retains script pointers from `rom->evtp` and dispatches through `bhScenarioJmpT[256]`.

Define explicit persistent, room, actor, frame, and streaming lifetimes.
Initially, a resident room buffer may be the simplest compatibility bridge,
but it must fit the measured budget. The production asset path should separate
required scripts/collision/placements/motion data from redundant source
geometry and textures. Never free source data while live game pointers still
refer to it, and never relocate the same records twice.

Preserve global progression and room-state flags across room unloading. A
door transition must wait for GE references and asynchronous reads before
reclaiming storage. It must also handle a failed load without leaving gameplay
pointing at partially initialized room data.

### 4. Reproduce the actual timing implementation

`njloop.c` appears to wait through `njWaitVSync`, but the PS2 implementation
of that function is empty. The effective pacing is in `Ps2SwapDBuff` in
[ps2_NaSystem.c](../src/ps2/veronica/prog/ps2_NaSystem.c): it waits for two
VBlanks, swaps, and calls `VsyncFunc`. The interrupt separately advances
`sys->time`; [sync.c](../src/ps2/veronica/prog/sync.c) advances other counters
and decrements `evt_tim` by two in `bhControlVSync`.

Use the normal approximately 30 Hz NTSC game cadence as the initial reference, with explicit VBlank-derived time accounting.
Confirm it with PS2 traces, including pauses, doors, event waits, movies, and slow frames. Preserve the task order and pressed/held/repeat input semantics.
The viewer's variable `dt`, clamped to 50 ms inside rendering, is unsuitable as the original game's simulation clock.
Separate simulation, presentation, streaming, and audio service; document how missed presentation deadlines affect each counter.

### 5. Replace the PS2 backend at its service boundaries

[ps2_dummy.c](../src/ps2/veronica/prog/ps2_dummy.c) is a real backend despite
its name. It declares a 10 MiB texture buffer, a 1.75 MiB packet buffer, ordering
tables, GS state, and VU programs. Original startup also allocates 12,845,056
bytes and derives several fixed-address regions from that allocation.

Replace those allocations and hardware paths with PSP resource management.
Preserve useful Ninja-facing math, model, texture, sprite, and draw semantics.
Reuse the current GU renderer behind that boundary. Eighteen game C files
contain assembly, including `njplus.c`, `face_bh.c`, `objitm.c`, matrix/math
code, and VU strip code; assembly is not confined to obviously named drivers.
Audit these routines individually and retain scalar reference implementations
where practical before adding PSP acceleration.

## Subsystem disposition

| System and source anchors | Recommended treatment | First meaningful proof |
| --- | --- | --- |
| Tasks/state: `main.c`, `system.c`, `game.c` | Reuse state machines and ordering; replace startup, timing, and platform calls | Original game update runs deterministically in one initialized room |
| Input: `pad.c`, `padman.c`, `ps2_sg_pad.c` | Retain game button/repeat semantics; map PSP inputs at the platform boundary | Movement, aim/fire, action/run, inventory, map, pause, and context-specific controls are all reachable |
| Room/events: `room.c`, `event.c`, `pl_evt.c`, `sp_evt.c`, `door.c` | Reuse scenario VM and transition logic; provide owned room data and asynchronous load status | Original interaction changes flags and an original door leads to the correct destination/spawn |
| Models/motion: `dread.c`, `binfunc.c`, `Motion.c`, `MdlPut.c`, `njplus.c` | Preserve animation sampling, hierarchy, skinning, and attachments; replace VU draw/math work | Textured Claire idles, turns, walks, and changes animation without broken joints or root motion |
| Collision/player: `player.c`, `playpch*.c`, `pwksub.c`, `hitchk*.c`, `rutchk.c` | Reuse original movement and collision, including floor and dynamic state | Recorded movement/collision trace agrees with reference gameplay |
| Enemies/combat: `eneset.c`, `en*.c`, `zonzon*.c`, `weapon.c`, `subpl.c` | Port original dispatch, AI, hit tests, damage, and scripted actors in exercised groups | One encounter supports attacking, damage, death, ammunition use, and leaving/re-entering |
| Camera/light: `camera.c`, `cut.c`, `light.c` | Reuse original control and cut logic; map output to PSP projection/light state | Correct cuts, tracking, transitions, visibility, fog, and event cameras |
| Effects/render passes: `effect.c`, `effsub*.c`, `screen.c`, `face*.c` | Reuse effect behavior; implement PSP sprites, transparency, shadows, facial animation, and special passes | Representative combat and story scenes retain their visual cues |
| UI: `adv.c`, `item.c`, `itemview.c`, `fileview.c`, `map.c`, `message.c`, `gameover.c`, `ranking.c` | Reuse menu/inventory/puzzle logic; implement readable PSP 2D rendering | Items can be examined, combined, used, stored, and restored; documents and maps are readable |
| Files: `gdlib.c`, `adxwrap.c`, `ps2_dvd_image.c` | Preserve archive IDs, entry IDs, request/status contracts; replace disc/IOP access | All required assets resolve by original identity and reads fail cleanly |
| Audio: `sdfunc.c`, `sdc*.c`, `adxwrap.c`, `ps2_sg_sd.c`, `ps2_snddrv.c` | Retain command semantics; replace streaming, bank playback, mixing, and IOP RPC | Voice, music, and multiple effects play together with correct completion and loop behavior |
| Movies: `mwwrap.c`, `ps2_sfd_mw.c`, `ps2_MovieFunc.c`, `ps2_MovieWork.c` | Retain playback lifecycle; replace PS2 MPEG/IPU implementation | One real movie has synchronized audio, skip/pause/end behavior, and returns to the correct game state |
| Saves: `backup.c`, `bup_00.c`, `ps2_McSaveFile.c`, save/load screens | Preserve logical save contents; replace memory-card storage/UI | Save, quit, cold boot, and load restore progression, inventory, actors, and options |

These are dependency groups, not independent compile targets. Many modules
cross several boundaries. Temporary unsupported calls should produce a useful
diagnostic identifying the caller/room/opcode; silently returning success from
file, sound, or event operations can create hard-to-diagnose progression bugs.

## Memory and asset pipeline

The current heap is 18 MiB with `PSP_LARGE_MEMORY := 0`. Retain PSP-1000 as the
provisional baseline implied by this configuration, and use larger-model
memory only as an optional enhancement until a different target is chosen.
Do not count all physical RAM or model-specific additional VRAM as available
to the application without validating the chosen runtime and APIs.

Fresh static-room measurements:

| Measurement | Room | Result |
| --- | --- | ---: |
| Largest reported construction payload | `RM_2031` | 17,037,840 bytes / 16.25 MiB |
| Largest expanded RDX | `RM_0030` | 8,931,872 bytes / 8.52 MiB |
| Largest room texture allocation | `RM_0100` | 6,456,320 bytes / 6.16 MiB |
| Largest static triangle count | `RM_2030` | 13,639 |
| Largest static batch count | `RM_0050` | 219 |

The construction payload reported by `prepare_psp_assets.py` sums expanded
RDX, scene vertices, mesh storage, and texture storage. It excludes some
auxiliary allocations: camera/cut/light arrays, scene metadata, player walls,
allocator overhead, and other runtime state. Compressed plus expanded data
has a separate peak earlier in loading; the compressed input is freed before
geometry construction. Neither sum is a complete PSP high-water measurement.

The largest reported construction payload leaves only about 1.75 MiB of the
configured heap before those omissions. Keeping the full RDX resident for
gameplay would also keep that major allocation live during play. This needs
resolution before loading player, weapons, enemies, persistent UI, and audio.

Implement the following in order:

1. Add allocation accounting by lifetime, largest-free-block reporting, stack
   margins, executable/BSS accounting, command-list usage, and stage timings.
   Track both steady-state play and door/movie/save transition peaks.
2. Make wireframe and spatial-debug geometry optional/lazy. Current mesh
   construction allocates six 16-byte wire vertices per triangle regardless
   of display mode: 528,192 bytes for `RM_0000`, and 1,309,344 for the largest
   static mesh. This is a concrete saving without changing game art.
3. Expand the extractor into a versioned asset manifest covering all archives,
   models, motion/skin data, scripts, UI, audio, and movies. `SYSTEM.AFS` is
   an immediate dependency: `system.c` selects player/costume entries there
   before `dread.c::bhReadPlayerData` parses them. Preserve original IDs.
4. Add a PSP asset cooker that emits draw-ready geometry and swizzled textures,
   plus explicitly owned gameplay records. Keep original decode paths as
   references. A chunked package should allow bounded staging reads instead
   of simultaneously retaining an entire source RDX and all converted output.
5. Admit rooms against a total budget including the current player, weapon,
   required actors, script state, audio rings, renderer buffers, and a measured
   reserve. Do not solve failures by raising parser limits or heap size alone.
   Initially target at least 2 MiB heap reserve after the measured worst load;
   treat that as an engineering target to revise from full-game measurements.

Do not assume all actor textures use the static-room T8 format or that all
models use the currently supported chunk types. Inventory every relevant
asset class before choosing conversions, reductions, or support limits.

## Rendering and performance

Follow the required [PSP optimisation guide](https://github.com/TheMrIron2/psp-survival-guide/blob/main/optimisation.md).
It was read in full for this audit. The current GU backend, swizzled T8 textures in main RAM, and explicit cache writeback already follow useful parts of that guidance.

Prioritize correctness needed by gameplay: actor hierarchy and skinning, object animation, original camera projection, fog, dynamic lighting, alpha ordering/depth behavior, shadows, particles, mirrors, and render-to-texture effects.
The static mesh reader flattens the first MLB into room geometry; actor support also needs motion and skin data, demonstrated directly by `dread.c`, `MdlPut.c`, and `njplus.c`. Test poses and frame attributes against the original before optimizing them.

The current lighting pass chooses one directional and up to three point lights and assigns lighting per triangle.
It is an approximation, not the original VU lighting implementation. Make visual exceptions measurable using reference captures from representative rooms and story scenes.

For performance work:

- Instrument CPU simulation, skinning, lighting, command submission, GE waits,
  streaming, and audio separately on hardware. A stable 30 fps presentation
  gives roughly 33.3 ms per frame, with headroom needed for workload variation.
- Use the VFPU explicitly for hot matrix/skin calculations with a reference
  path for comparison; enabling the VFPU thread attribute does not vectorize
  ordinary C. Preserve gameplay-sensitive math and Ninja angle conventions.
- Keep swizzled CLUT textures; test a bounded VRAM cache only where it helps.
  The current double RGBA8888 framebuffer plus 16-bit depth buffer consumes
  1,392,640 bytes at 512 by 272. Choose framebuffer depth and extra render
  targets from a complete VRAM map and image comparisons.
- Preserve camera visibility and add inexpensive object bounds where useful.
  Reduce redundant adjacent texture/palette/state submissions without
  reordering transparency or breaking object identity. Benchmark strips,
  compact vertex formats, and state batching against the existing path.
  The guide specifically warns against assuming indexed geometry is faster.
- Track command-list capacity before adding actors/effects. The renderer has
  one 64 KiB list and waits for GE completion each frame. Consider two command
  lists after profiling, with matching lifetime rules for dynamic vertices,
  palettes, and texture storage; measure the latency cost.
- Measure compiler options and instruction-cache effects. Keep `-O2` as the
  baseline until representative hardware runs show a better choice.

Custom Media Engine work is a later option for sufficiently independent
streaming/audio work. It needs explicit cache/ownership synchronization and
may lose PPSSPP compatibility. Prefer proving supported media decoding paths
first; custom ME/VME code should not become a prerequisite for the first
playable sequence.

-- DEVELOPER NOTE: Code Veronica streams its audio in the ADX format internally; converting these to a PSP-native (ATRAC, AAC, MP3...) format would allow Sony's optimised audio streaming libraries (eg. `sceAtrac`) to be used out of the box.

## Audio and movies need early feasibility work

Audio is more than adding an ADX decoder. `ps2_sg_sd.c` handles MIDI sequence
banks, program/sample banks, shot banks, and PCM streams; `ps2_snddrv.c`
uses IOP RPC and playback status. Inventory real bank contents and command
usage before deciding which assets to decode, synthesize, or convert offline.
Preserve pitch, pan, volume, looping, prioritization, pause, and completion
semantics. A silent backend that never finishes playback can stall events.

Prototype one voice stream, one music source, and overlapping effects early,
with bounded ring buffers and an audio worker independent of frame rendering.
Measure decode/mix cost under a demanding room load. Select native compressed
media versus software decoding/conversion from those results, not extensions
or the presence of decompiled CRI code alone.

The supplied PS2 ISO contains PSS movies. `ps2_MovieFunc.c` uses MPEG demux,
PS2 IPU output, DMA, and PS2 threads. Treat PSP playback as a backend
replacement, and prototype offline conversion to a PSP-supported hardware
decode format. Verify container, profile, resolution, timestamps, memory use,
and audio synchronization on hardware before converting all 18 movies.
Keep the original game's start/status/time/pause/skip/end contract. Movie
completion and cancellation are progression requirements, not optional polish.

-- DEVELOPER NOTE: Offline conversion is almost certainly the preferred path here for audio and video.
Don't make the PSP do the hard work at runtime - allow the user to supply their copy of the game and process them once ahead of time for better performance.
This can be done once as a script in a batch. For MIDI playback, PSP games typically play MIDI via the `sceSas` library for mixing.
Per PPSSPP dev hrydgard: "sceSas doesn't have native midi support ... there might be some shared utility library. also midi is a very simple file format, if you got the sample banks with your piano samples and what not, playing it through sceSas should be pretty easy"

## Delivery sequence and acceptance gates

| Milestone | Deliverable | Acceptance gate |
| --- | --- | --- |
| 0: Measured baseline | Reproducible build, current viewer, memory/frame/GE instrumentation, content inventory | Three known-good rooms plus the largest-memory, largest-geometry, and largest-batch rooms have hardware measurements with device model and clock settings recorded |
| 1: Game foundation | Portable ABI, math/input/file services, explicit room ownership, original task/timing skeleton | Required modules compile without PS2 SDK imports; disk-layout assertions pass; original initialization and scripted state can run with explicit unsupported-service diagnostics |
| 2: Original player | Player assets from `SYSTEM.AFS`, motion/skin support, original movement/collision and camera control | Animated Claire can idle, turn, walk/run and navigate a representative room with reference-matching poses, collision, and cuts |
| 3: Playable sequence | Original events, interactions, messages, object state, doors, and basic audio | A contiguous original sequence includes a pickup or puzzle interaction, a scripted event, correct room transition, and persistent state on return |
| 4: Game systems | Combat/enemy families, inventory/item box/map/files, save/load, menus, effects, complete audio/movie lifecycle | A representative chapter is completable with sound, combat, saving, cold-boot loading, death/retry, and cutscenes |
| 5: Full content | Remaining characters, enemies, bosses, scripts, puzzles, cinematics, and game modes | Supported campaign and bonus modes finish from their normal entry points; no required opcode, asset, or backend path is unsupported |
| 6: Release quality | Performance tuning, visual/audio QA, readable UI, robust I/O, suspend/resume, packaging | Complete playthroughs and long transition/save/load soak runs pass on every supported PSP model; no progression blockers, memory growth, or sustained frame/audio failures |

Start audio/movie feasibility during milestones 1–2 so a late discovery does
not invalidate the memory or asset design. Start save-format work during the
playable sequence; saves become a practical way to exercise later content.

The initial route trace confirms the first four default rooms are connected
through their original door records. This does not prove that the whole route
forms a scripted sequence; follow interaction scripts and progression state
before selecting its acceptance path. Implement all behavior exercised by
that sequence, then expand coverage; avoid invented door links or synthetic
flag changes in the production path.

## First implementation backlog

1. Finish the profiling work with executable/BSS, command-list high-water,
   load/transition timings, then establish stress-room hardware baselines.
   The HUD already reports heap/stack margins, payload estimates, frame,
   CPU/submission, and GE wait times. Make debug geometry optional and record
   the savings.
2. Introduce portable types and the minimum Ninja math/platform boundary.
   Assert disk layouts and produce a compile/link dependency inventory for
   the original player, motion, camera, collision, and event modules.
3. Decode the extracted initial player model, skin, motion, and texture
   entries from `SYSTEM.AFS`; document model formats and resident costs. The
   initial stream extraction and size inventory now exist as `make
   player-assets`.
4. Implement owned gameplay room data and connect the original initialization
   and timing path, keeping the current viewer available for diagnosis.
5. Drive the real player and cameras, then the first real script interaction
   and door. Add reference traces for position, animation, camera, flags,
   script position, and asynchronous completion at each integration step.

This sequence exposes the expensive unknowns before expanding art coverage
or polishing presentation. No reliable completion estimate follows from
three successful rooms; ABI integration, actor rendering, audio banks, and
the first original scripted sequence are the milestones that will support one.

## Release-quality definition

Maintain a coverage ledger for rooms and their scenario variants, event
opcodes, enemies/bosses, player characters/costumes, weapons, item interactions,
save slots, movies, and modes. Parser coverage and playthrough coverage must
remain distinct. Compare deterministic input runs against the original using
state traces as well as screenshots; use tolerances appropriate to math
differences without accepting progression or collision divergence.

Keep a faithful camera composition option while adapting to the PSP display.
Simply changing the aspect ratio can reveal geometry the original cut masks
hide. Verify readable text, maps, inventory and puzzle screens at PSP
resolution, and provide a complete control mapping for PS2 inputs absent on
PSP. Save data should have an explicit versioned schema and integrity checks;
the original save path copies the range from `sys->version` to `save_end`,
which must not silently change with runtime structure layout.

Require cold-boot save/load, corrupt/missing asset handling, low-storage and
interrupted-save recovery, repeated door transitions, movie skipping,
HOME/exit, and suspend/resume testing. Use PPSSPP for rapid integration and
real hardware for GE/cache behavior, pacing, memory, I/O, audio, and power
events. Record worst-frame and percentile timings, memory peaks, load latency,
and audio underruns for representative play rather than a viewer-only FPS.

Add PSP build and portable-test CI alongside the existing decomp progress
workflow. Keep synthetic tests public and run full supplied-ISO validation
locally or in a private asset-equipped environment. Pin toolchain and asset
format versions so release packages can identify incompatible data cleanly.
