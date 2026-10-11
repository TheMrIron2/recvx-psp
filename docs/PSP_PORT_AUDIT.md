# PSP full-game port audit

Initial audit: 10 October 2026, against `86de5c39` plus the then-existing
working-tree changes. Follow-up PSP measurements below were recorded against
`e5fda38b` on the same date.
This is an architectural audit and implementation roadmap, not a claim of full-game compatibility or a line-by-line correctness review.

The next gameplay milestone is original player movement and camera control on PSP, followed by a scripted interaction and door transition with persistent state.
Build this by bringing original game logic onto a small PSP platform layer. Keep the viewer as a diagnostic tool and its validated asset readers as a foundation.

## Evidence and current scope

The user has now supplied PSP captures for the four connected opening-route rooms. This audit did not perform independent hardware testing.
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
At the initial audit there were no direct player-controller tests. The current
`make test` suite checks the original Claire collision dimensions, vertical
wall overlap, and floor/attribute filters.

The rebuilt EBOOT was also smoke-run in PPSSPP 1.19.3. The room checks pass
for `RM_0000`, `RM_0020`, and `RM_0030`; `RM_0020` enters walking mode and
responds to forward input. In `RM_0030`, the walking model responds to forward
input at the jail gate but remains at the boundary reported by the user on
hardware. This reproduces the known room collision limit. The current EBOOT
also runs the original `bhSysCallInit`/`bhInitSystem` path through task slot 0.
PPSSPP reports success, 2,912 bytes of arena use, task mask
`0x00300002`, and the original `-2`/`-20000` clip request. It then loads the
room and retains viewer behavior. Its bounded core interpreter executes 22
RM_0000 SCD0 commands, performs 18 original flag writes, and queues event 38
in task 7 (352-byte event body). It stops with an explicit unsupported-command
diagnostic at enemy-dependent opcode `0x24`, offset `0x7A`; that event body is
not yet executed. This closes M1's initialization and scripted-state gate, not
the playable-gameplay gates. Captures are in the ignored
`build/psp/ppsspp-smoke/` directory.

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
data. The runtime retains the model and motion source buffers for pose
evaluation. The first Claire chunk model now goes through the room mesh
decoder's hierarchy and triangle path and preserves its native room-unit scale
for a textured animated preview in walking mode. The model keeps its facing
while reverse input moves it backward. The renderer now maps the original Claire
motion table for idle (42), pivot (39), forward walk (0), and reverse walk
(16), plus run (4). It parses per-frame movement speeds from the player extra
stream and uses tracks 0 and 6 for forward and reverse walking, and track 3
for running while R is held. Position and angle tracks both feed the hierarchy
and skinning path; the player root now follows authored translation keys.
Motion and speed key
arrays are extent-checked, and posing reuses existing mesh storage without
frame-time allocation. The extracted skin table blends parent and bone
transforms for the single body mesh; user hardware checks confirm the animated
skin retains visible arms. The user also confirmed in PPSSPP that the walk and
run animations look correct. The texture-tail decoder loads 19 embedded T8
pages, and the initial Claire body mesh renders its five local material slots
with their UVs and palettes. PPSSPP captures
`m2-player-room-0030-spawn.png` and `m2-player-room-0030-forward-1.png` show the
red vest and blue jeans during movement. Other player models, a frame-by-frame
pose comparison, and original motion state-specific transition curves remain
open.

[The Makefile](../Makefile) links only `flag.c` and `ps2_NaMem.c` from the original game.
The original game directory contains 154 C files and about 248,000 lines, excluding CRI and SDK sources. This measures integration scope, not remaining development effort.
The current PSP ELF has 211,244 bytes of text, 4,912 bytes of data, and 115,160 bytes of BSS; the complete game will have a substantially different executable footprint.

## Architectural findings

### 1. Preserve the original gameplay contracts

[main.c](../src/ps2/veronica/prog/main.c) supplies the globals and a 23-entry task dispatcher.
[system.c](../src/ps2/veronica/prog/system.c) drives loading, menus, gameplay, events, doors, movies, and saves.
[game.c](../src/ps2/veronica/prog/game.c), through `bhMainSequence`, orders enemy, player, effect, object, camera, lighting, drawing, and event work.

The PSP [player.c](../src/psp/player.c) is still a diagnostic controller. Its
Claire collider now uses the original 3.5 radius and 16.5 height, default
player collision flags, wall floor/attribute filters, and room floor heights.
Static box, circle, and triangle walls share the overlap check and bounded
axis resolution. Floor attribute records are loaded, but dynamic walls,
original movement states, and combat remain incomplete. The original
controller remains the target for integration.

Bring across `player.c`, `playpch.c`, `playpch2.c`, `pwksub.c`, `pl_evt.c`, `hitchk.c`, `hitchkl.c`, and `rutchk.c` incrementally.
Preserve movement, animation frame attributes, collision response, and flag behavior together.
Retain original object IDs and indices when converting render assets because gameplay, camera masks, and attachment logic refer to them.

### 2. Establish the ABI before adding gameplay modules

[types.h](../include/ps2/veronica/prog/types.h) directly includes Ninja, PS2 graphics, DMA, pad, memory-card, sound, and CRI headers.
Game headers inherit this dependency. The existing two portable modules largely avoid this problem.

Continue adding portable game/Ninja type declarations and PSP service interfaces.
Implement the subset used by the next milestone, expanding it as real callers are integrated.
Keep the explicit source list and a record of unresolved functions; do not pull the entire PS2 SDK surface into the PSP build.

The first portable slice now lives in `include/psp/abi.h` and
`include/psp/ninja_math.h`: fixed-width game/Ninja values, core vector/matrix
shapes, and Ninja angle conversion are independent of the PS2 SDK. Initial
`ROM_WORK`, room header, cut prefix, actor placement, effect, collision, and
spawn disk layouts have compile-time size/offset assertions. These now also
cover room lights, event cameras, player model/motion records, and save records.
`include/psp/platform.h` provides normalized button levels and edges; the PSP
adapter contains the controller, clock, and kernel calls. The portable input
service adds PS2-style 30 Hz D-pad repeat timing with the original 10-tick
initial delay and 4-tick interval.
`include/psp/file.h`
now separates read, line, size, and close operations from the stdio adapter.
Room loads return a `RecvxRoomData` owner with a matching release function.
`include/psp/timing.h` advances provisional player simulation at a rational
30 Hz and records dropped ticks after stalls longer than four catch-up steps;
presentation and viewer camera input retain measured frame time.
The viewer releases room data after derived geometry and collision data are built;
retaining the expanded RDX for gameplay would exceed the measured
PSP-1000 free heap, so pointer-bearing original room data still needs a compact
resident representation or a budgeted streaming design. The broader original
ABI remains to be separated.

The binary readers and original relocation code assume 32-bit pointers and specific layouts. `bhSetRoom` copies 1,004 bytes of `ROM_WORK` and relocates offsets in place.
`bhMlbBinRealize` and `bhMnbBinRealize` also modify their inputs. PSP's 32-bit pointers help, but do not prove compiler ABI equivalence.

Add size/offset assertions for disk-facing structures, especially `ROM_WORK`, `CUT_WORK`, model/motion records, and save fields.
Check signed `char`, integer widths, alignment, bitfields, and function prototypes.
The existing [override_katana.h](../include/ps2/veronica/prog/override_katana.h) already documents a compiler-specific integer-width issue.
Audit pointer-to-integer casts and type punning; any transitional aliasing flags should be narrowly scoped. Avoid global fast-math until behavior comparisons support it.

Keep disk offsets separate from host pointers. Use explicit disk records and native runtime records for 64-bit host tests, or an intentionally configured
32-bit reference harness. Directly compiling the original relocators into a 64-bit host executable is not a valid compatibility test.

#### Initial original-module compile and link inventory

This first pass records source include dependencies and the service families
they pull in. Most headers include `types.h`, which imports PS2 SDK, graphics,
pad, sound, CD/DVD, memory-card, and CRI declarations; direct source edits
alone will not make these modules compile for PSP.

The `__PSP__` branch in `types.h` now skips the Sony PS2 EE, graphics, pad,
sound, CD/DVD, memory-card, and CRI headers. `psp/legacy_types.h` supplies the
fixed-width aliases and incomplete SDK records shared by the game headers,
with the vendored Ninja declarations included after PSP system headers. The
original `main.c`, `system.c`, `system_init.c`, and `game.c` pass PSP syntax
compilation via `make legacy-portable-check`. The original `bhInitSystem` and
`bhSysCallInit` bodies now live in the isolated `system_init.c` translation
unit and are linked into the EBOOT. The PSP runtime supplies the game globals,
a bounded 256 KiB arena, aligned game allocations, memory statistics, font
scale state, and the `njClipZ` request. The VBlank adapter advances the original
counter fields after `sceDisplayWaitVblankStart`; it does not yet call the
original audio/event work in `bhControlVSync`. The viewer's GU projection
remains independent of the stored clip request.

The PSP dispatcher invokes `bhSysCallInit` from slot 0. Its output task mask
activates warning, monitor, and sound-monitor slots; unported slots remain
visible by name. `system.c` still records its unsupported display-mode change
instead of calling the PS2 movie/display library. The service registry reports
service name, call count, and last caller on the PSP status screen.

`make legacy-link-inventory` compiles `main.c`, `system_init.c`, `system.c`, and
`game.c` and prints unresolved symbols per object. The remaining `main.c`,
`system.c`, and `game.c` dependencies span full startup, room and actor loading,
Ninja drawing, pad/timing, events, audio, saves, and movies. The EBOOT uses the
PSP task dispatcher and viewer for slot 7, owns the original room event data,
and runs a bounded subset of SCD0 flag, comparison, and event-queue commands.
It does not link the full `event.c` opcode table or execute the queued event
body; actor-dependent script operations remain explicit unsupported boundaries.

| Source group | Direct dependencies | PSP boundary still needed |
| --- | --- | --- |
| `Motion.c` | `Motion.h`, `ps2_NaMath.h`, `ps2_NaMatrix.h` | Split actor/motion ABI from `types.h`; portable angle and matrix services |
| `player.c`, `playpch.c`, `playpch2.c` | `player.h`, motion, cut, effect, flag, collision helpers, weapon, sound, room, and global `main.h` state | Actor/work-record ABI, game globals, input/task state, rendering, sound, and unsupported-call diagnostics |
| `pwksub.c`, `hitchk.c`, `hitchkl.c`, `rutchk.c` | Player state, `ps2_NaColi.h`, math/matrix, view, room and camera globals | Portable collision primitives, room-array ownership, camera/view contract |
| `camera.c` | `camera.h`, `njplus.h`, `ps2_dummy.h`, `main.h` | Camera work records, projection/view calls, global runtime state |
| `pl_evt.c`, `event.c` | Player/collision, room, effects, flags, messages, screen, sound, enemy dispatch, system state | Scenario pointer lifetime, event opcode services, presentation/audio and progression state |
| `MdlPut.c` | `MdlPut.h`, `njplus.h`, Ninja Cnk, texture functions, matrix and `main.h` | Object/hierarchy ABI and PSP draw/texture adapter |

`Makefile` links `flag.c`, `ps2_NaMem.c`, and the isolated `system_init.c` from
the original game. The include-level map for the source groups in the table is not yet a
complete undefined-symbol map: the motion, player/collision, camera, event, and
model-drawing groups have not been compiled against a PSP header shim. Collect
their compiler and linker diagnostics as each dependency group is isolated,
rather than filling unresolved symbols with success stubs.

The reference loop in `main.c::njUserMain` dispatches active, unpaused task
slots 0 through 22 in order, then calls `PS2_jikken` and the soft-reset check.
Within each `bhMainSequence` loop, game updates run enemy, player, effect,
object/item, camera, and light work before the final draw passes. These orders
are recorded here; the PSP prototype has not imported those callbacks yet.

`include/psp/game_tasks.h` and `src/psp/game_tasks.c` now provide the 23-slot
active/unpaused dispatcher in ascending slot order. The prototype binds slot 7
(`game`) to its existing renderer callback; this preserves a place to connect
the original task loop while keeping the viewer usable. Active slots without
handlers return a bitmask and are named on the diagnostic screen. This is a
dispatcher skeleton only: original task callbacks, `PS2_jikken`, reset checks,
and the `bhMainSequence` update/draw order remain unported.
`include/psp/services.h` and `src/psp/services.c` record unsupported backend
service calls for display mode, movies, audio, memory cards, disc streaming,
and vibration; only the display-mode path is currently connected to an original
caller.

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

The user supplied a real-PSP walking-mode capture for `RM_0030` before motion
playback was added. It reports 112 models, 8,732 room triangles, 5,553 KiB
free heap, a 5,297 KiB largest block, 251 KiB stack, 19 KiB scene, 1,027 KiB
mesh, 4,329 KiB textures, 818 KiB of avoided wireframe storage, 298 KiB
player model/motion source, and 163 KiB player mesh. Frame time is 16.68 ms,
CPU plus submission is 0.33 ms, GE wait is 0.01 ms, and the GU list peaks at
8 KiB of 64 KiB.
This earlier user-reported capture establishes the initial runtime baseline;
the runtime clock is set to 333 MHz CPU and 166 MHz bus.

The user later supplied five immediate-spawn captures for `RM_0000`,
`RM_0010`, `RM_0020`, and `RM_0030` in the ignored build directory
`build/psp/M0/`. The software clock setting is 333 MHz CPU and 166 MHz bus;
the PSP model is not recorded. Values below are read from the HUD. Memory and
GPU command-list sizes are KiB; frame timings are milliseconds. “Avoid” is
wireframe storage avoided in the current display mode.

| Capture | Room | Models / triangles | Heap / largest / stack | Scene / mesh / textures | Avoid | Frame / CPU+submit / GE wait | GU current / high-water |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `snap106.bmp` | `RM_0000` | 67 / 5,502 | 5,548 / 5,292 / 245 | 22 / 646 / 1,537 | 515 | 33.37 / 18.46 / 5.30 | 4 / 4 |
| `snap107.bmp` | `RM_0010` | 58 / 4,366 | 5,548 / 5,292 / 245 | 17 / 513 / 1,368 | 409 | 33.37 / 18.51 / 3.24 | 4 / 4 |
| `snap108.bmp` | `RM_0020` | 85 / 5,795 | 5,548 / 5,292 / 245 | 28 / 681 / 3,719 | 543 | 33.36 / 18.46 / 0.27 | 3 / 3 |
| `snap110.bmp` | `RM_0030` | 112 / 8,732 | 5,548 / 5,292 / 245 | 19 / 1,027 / 4,329 | 818 | 33.36 / 18.41 / 0.14 | 3 / 3 |
| `snap111.bmp` | `RM_0030` | 112 / 8,732 | 5,548 / 5,292 / 245 | 19 / 1,027 / 4,329 | 818 | 50.05 / 18.67 / 15.41 | 8 / 8 |

The four immediate room samples are about 30 FPS. The extra `RM_0030` capture
shows one 50.05 ms frame interval (about 20 FPS), with GE wait rising to
15.41 ms while CPU plus submission remains near 18.7 ms. This is evidence of
a single missed presentation deadline, not sustained 20 FPS. All five HUDs
report ELF T/D/B of 205/4/112 KiB, player source data of 298 KiB, player mesh
of 163 KiB, and zero separately allocated walls and doors. These captures are
the current prototype baseline for the four connected opening-route rooms. The
PSP model is not recorded, but that does not block this scope's baseline.

The construction payload reported by `prepare_psp_assets.py` sums expanded
RDX, scene vertices, mesh storage, and texture storage. It excludes some
auxiliary allocations: camera/cut/light arrays, scene metadata, player walls,
allocator overhead, and other runtime state. Compressed plus expanded data
has a separate peak earlier in loading; the compressed input is freed before
geometry construction. The runtime HUD now adds stage-boundary free-memory
samples, but it does not instrument allocations inside each stage, so these
samples are not exact transient peaks.

The offline largest-room construction payload leaves only about 1.75 MiB of
the configured heap before those omissions. This is a planning signal for
future large-room and full-game memory work; it is not a current-route blocker.
Revisit the complete room budget as gameplay systems and broader room coverage
are added.

Implementation status, in order:

1. The runtime HUD reports free and largest heap blocks, stack margin, known
   room/player allocation categories, module text/data/BSS, current and maximum
   GU-list use, and frame/CPU/GE timing. The room check screen reports file,
   scene, mesh, texture, collision, total-load, and door-transition times, plus
   free-memory samples at stage boundaries. Milestone 0 is closed for the
   current prototype scope: four connected route rooms have immediate-spawn
   PSP captures with memory, frame, and GE telemetry at the enforced 333/166
   MHz clock. No representative stress, steady-play, or transition workload
   exists yet; defer those profiles and allocation-level peak tracking until
   gameplay creates one.
2. Wireframe geometry is generated only after selecting wireframe mode. The HUD
   reports its current cost and the memory avoided while it is unused: six
   16-byte vertices per triangle, 528,192 bytes for `RM_0000`, and 1,309,344
   bytes for the largest static mesh.
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
  The [PSP Survival Guide optimization notes](https://github.com/TheMrIron2/psp-survival-guide/blob/main/optimisation.md)
  specifically warn against assuming indexed geometry is faster.
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
| 0: Measured baseline (closed for prototype scope) | Reproducible build, current viewer, memory/frame/GE instrumentation, content inventory | Four connected opening-route rooms have immediate-spawn PSP measurements for memory, frame, and GE use at the enforced 333 MHz CPU / 166 MHz bus clock; one missed-deadline `RM_0030` sample is recorded. No representative stress, steady-play, or transition workload exists yet; profile them when gameplay provides one |
| 1: Game foundation (closed) | Portable ABI, math/input/file services, explicit room ownership, original task/timing skeleton | Required modules compile without PS2 SDK imports; disk-layout assertions pass; original initialization and a real SCD0 state run with an explicit unsupported-command boundary |
| 2: Original player (in progress) | Player assets from `SYSTEM.AFS`, motion/skin support, original movement/collision and camera control | Animated Claire can idle, turn, walk/run and navigate a representative room with reference-matching poses, collision, and cuts |
| 3: Playable sequence | Original events, interactions, messages, object state, doors, and basic audio | A contiguous original sequence includes a pickup or puzzle interaction, a scripted event, correct room transition, and persistent state on return |
| 4: Game systems | Combat/enemy families, inventory/item box/map/files, save/load, menus, effects, complete audio/movie lifecycle | A representative chapter is completable with sound, combat, saving, cold-boot loading, death/retry, and cutscenes |
| 5: Full content | Remaining characters, enemies, bosses, scripts, puzzles, cinematics, and game modes | Supported campaign and bonus modes finish from their normal entry points; no required opcode, asset, or backend path is unsupported |
| 6: Release quality | Performance tuning, visual/audio QA, readable UI, robust I/O, suspend/resume, packaging | Complete playthroughs and long transition/save/load soak runs pass on every supported PSP model; no progression blockers, memory growth, or sustained frame/audio failures |

Start audio/movie feasibility during milestone 2 so a late discovery does
not invalidate the memory or asset design. Start save-format work during the
playable sequence; saves become a practical way to exercise later content.

The initial route trace confirms the first four default rooms are connected
through their original door records. This does not prove that the whole route
forms a scripted sequence; follow interaction scripts and progression state
before selecting its acceptance path. Implement all behavior exercised by
that sequence, then expand coverage; avoid invented door links or synthetic
flag changes in the production path.

## First implementation backlog

1. Milestone 0 is closed for the current prototype scope. Five user captures
   record immediate spawns in the first four connected route rooms, including
   one `RM_0030` missed-deadline frame. Runtime reporting covers memory, frame,
   GE, module, and command-list use. No representative stress, steady-play, or
   transition workload exists yet. Defer those profiles, offline-maxima
   captures, and allocation-level peak tracking until gameplay creates one.
2. Milestone 1 is closed. Fixed-
   width game values, room/model/motion/save disk-layout assertions, Ninja
   angle math, PSP input and clock adapters, 30 Hz D-pad repeat timing, a
   rational 30 Hz simulation tick, a stdio-backed file service, and scoped
   room-data ownership are now in place. The PSP type boundary now lets original
   `main.c`, `system.c`, `system_init.c`, and `game.c` pass a repeatable syntax
   check. The original system initializer runs on PSP; the bounded SCD0 core
   sets original flags and queues event 38 before stopping at unsupported
   enemy-dependent opcode `0x24`. Keep that boundary visible while bringing
   actor and event services across in the next milestones.
3. Milestone 2: finish the player asset and pose path. The initial Claire
   model, skin, motion, and texture entries are extracted from `SYSTEM.AFS`;
   `make player-assets` records stream sizes and model inventory. The PSP
   preview samples original idle, pivot, forward, reverse, and run motions,
   applies root and joint translation/rotation tracks, and uses the authored
   forward, reverse, and run speed tables. R held during forward input selects
   the original run motion; the user confirmed the walk and run animations in
   PPSSPP. Motion changes blend root and joint transforms with the source
   controller's per-state counts and rates: idle 5 ticks at 42598/65536,
   pivot and walk 8 at 42598/65536, reverse 8 at 49152/65536, and run 4 at
   42598/65536. Initial frames preserve the source controller's start-frame
   or proportional frame mapping. Source control also decrements the pivot
   frame on right turns, increments it on left turns, and turns at 4.5 degrees
   per tick while running versus 4 degrees while walking; the PSP path now
   follows those rates. Equal 300 ms PPSSPP captures
   (`m2-player-room-0030-turn-right.png` and `-turn-left.png`) show pivot slot
   39 at frame 23 for right and frame 9 for left. The texture-tail decoder
   loads the 19 embedded T8 pages and draws the initial Claire body's five
   local material slots with their UVs, palettes, sampler flags, alpha, and
   diffuse color.
   PPSSPP captures show the textured actor while walking; other player models
   and a frame-by-frame pose comparison remain open.
4. Milestone 2: bring original movement, collision, and camera behavior onto
   the PSP runtime. The state controller remains diagnostic; room camera poses
   and automatic cut selection use original room records. Tests cover camera
   variant selection and cut-edge hysteresis; camera movement now has PPSSPP
   smoke traces, but no original-runtime pose comparison. The original player, motion, camera, and collision
   modules pass the PSP syntax check, and `make legacy-link-inventory` records
   their unresolved services. The player object still depends on broad combat,
   enemy, audio, effect, model, and global game services, so syntax-checking
   these modules does not connect the original state loop. The diagnostic path
   uses Claire's collision size and room floor heights; tests cover wall height,
   attribute filtering, and floor attribute loading. Static box, circle, and
   triangle wall contacts now resolve each movement axis with an eight-step
   search against the overlap predicate. The `RM_0030` spawn 0 host trace uses
   Claire's authored forward-walk speed keys: it reaches the east wall on tick
   6 at x=35.49939 and remains there through tick 30. Active ETC type-2 stair
   regions with nonzero parameter 0 now produce up to four edge walls generated
   by the original `bhSetDansaLimitAtari`; neighboring stair regions suppress
   shared edges. The four opening-route rooms have no active stair records.
   PPSSPP smoke now exercises the normal stair in `RM_1030` using a test-only
   spawn override that places Claire just outside its trigger. Wall, floor,
   door, and stair records are room-owned. The
   player preserves original type-1 step-sound IDs and type-3 slow, water, and
   hazard surface states; slow floors apply the original 0.8 movement scale.
   Existing host coverage checks floor parameter loading, surface
   classification, and slowed displacement. Type-0 floor target selection
   follows the source's normal position box or `attr & 1` front-point region,
   its four cardinal facing masks, and the player flag suppression for bits
   `0x2` and `0x4`. A host trace against `RM_0000` selects floor records 2 and 3
   only for their allowed heading bins. The selected record index is exposed
   on the player and diagnostic HUD; hardware rendering omits this text, while
   software-rendered PPSSPP captures show it. Item-use handling of the selected
   record, stair height/state transitions, audio/effect responses, and the
   original player state loop
   remain open. PPSSPP 1.19.3 was restarted from the
   refreshed EBOOT. The new `m2-player-room-0030-contact-stable.png` capture
   shows Claire holding at the visible wall boundary during sustained input.
   That hardware-rendered image cannot provide the player coordinates or
   blocked flag. A scripted RM_0030 path uses the authored walk speeds and
   source cut areas to move from cut 2 to cut 0, then
   cut 1 to cut 3. The `camera-cut2`, `camera-diagonal`, and `camera-cut1`
   captures show those views; cut 3's source camera has the interpolation flag
   and speed 30, and the `camera-transition-near`, `camera-transition`, and
   `camera-transition-settled` frames show the view moving through that cut
   while keeping Claire visible. The transition opens onto a large unrendered
   dark area in the room view. This confirms visible cut selection and motion
   in PPSSPP, but does not compare camera poses to the original runtime.
   Source review found `bhInitActiveCamera` initializes the camera focus at
   Claire's position, then `bhControlActiveCamera` moves its horizontal focus
   halfway toward her each game tick. The renderer now keeps that focus state
   and uses it for camera-area attributes, position offsets, and look angles;
   a cut or camera-variant change resets the focus to Claire's current position.
   The refreshed EBOOT repeated the RM_0030 cut path in `m2-focus-camera-*`
   captures, including the wall hold and the cut 1-to-3 transition.
   After switching the desktop to 2560×1440 and PPSSPP to software rendering,
   the refreshed `m2-player-room-0030-camera-*` captures are full-screen
   2560×1440 images with the HUD visible. In the latest clean run,
   `camera-transition-near` reports cut 2/4 at (9.4, 145.3), while
   `camera-transition` and `camera-transition-settled` report cut 4/4 at
   (20.0, 143.9) and (33.2, 142.3). Claire remains visible through the
   automatic cuts and reaches the far side of the metallic heap without a wall
   hit on this route. Direct contact with the heap remains an intended
   obstruction, confirmed separately by the sustained wall-hold capture.
   Source review found that follow-mode angular limits clamp the desired
   player-relative view around the base angle while preventing backward camera
   motion; `tracking_camera_angle` now follows that rule and handles signed
   limits. The three flagged source cameras (`RM_1070`, `RM_3190`, and
   `RM_7050`) all have zero pitch and yaw limits, so stock room data cannot
   exercise this clamp. A test-only `RM_3190` spawn in cut 3 launched in the
   refreshed EBOOT (`m2-follow-camera-spawn.png` and
   `m2-follow-camera-walk-*.png`), but forward input met the workbench and
   produced no observable camera movement. Those frames do not validate the
   tracking rule.
   A source comparison of `bhCheckWall` found that rectangular walls use the
   player radius at their corners; `circle_intersects_rectangle` now preserves
   that rounded boundary instead of stopping at the square-expanded corners.
   The PSP triangle-wall predicate now ports the source `bhCheckInnerTriangle`
   checks for all four triangle orientations. This removes the extra collision
   area that stopped Claire before the open route around the heap; the heap
   itself remains solid. Type-7 wall edges apply the floor attribute, use the
   room wall height when their authored height is zero, and test vertical
   overlap against the wall edge. The diagnostic PSP predicate now follows
   those rules. A refreshed software-rendered PPSSPP run held forward and run
   input into the `RM_0030` debris and wall area; its 2560×1440
   `m2-focus-room-0030-wall-hold.png` capture shows Claire at `(-22.9, 71.6)`
   with `Wall` active. This confirms the diagnostic collision result, not an
   original-runtime match. A separate route now passes around the heap and
   reaches cut 4, as recorded above.
   Static inspection of all 205 packed rooms found active type-1 stair records
   in 54 rooms. Claire's extracted normal stair motions 31, 32, 35, and 36 each
   have 27 frames and root position tracks; her initial archive has no tracks
   in special hatch slots 62 through 65. PPSSPP captures in
   `m2-player-room-1030-stair-*` show Claire approach and turn toward the
   normal stair trigger; she then moves out of the fixed camera view, so the
   images do not confirm her landing position. A focused host trace confirms
   motion slot 36, the 25-tick turn, 19 root-motion frames, 10 recovery ticks,
   and the floor-0 snap. This uses a test-only spawn override under the ignored
   smoke directory; the packaged `build/psp/data` room stays unchanged. It does
   not compare stair poses against the original runtime. Special hatch motion,
   reference traces for animation/collision/camera, and original camera-pose
   comparisons remain open.
5. Milestone 3: execute the queued event body and build the first original
   interaction and door sequence. Preserve event flags, script position,
   transition completion, and state on return; keep unsupported operations
   visible while integrating the required actor and presentation services.

This sequence exposes the expensive unknowns before expanding art coverage
or polishing presentation. No reliable completion estimate follows from
four successful rooms; ABI integration, actor rendering, audio banks, and
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
