# Original game UI services on PSP

The game build now links the original ranking, typewriter, inventory, item
inspection, file viewer, computer terminals, game-over, and map controllers.
Their generators retain the supplied original function bodies and static data;
PSP runtime files validate resources, arena ownership, indices and state before
those routines touch packed data. `config/psp_boot_unported.txt` remains the
list of explicit stops for services outside this completed batch.

Inventory uses the original ITEM1 textures, layout, palettes, messages and item
logic. Entry backs up room textures; exit restores their pixels, live palettes,
render settings and the original arena cursor. The PSP draw adapter supports
rotated/flipped sprites, textured quads, border lines, per-batch scissoring and
multiple ordering-table flushes without clearing the already drawn scene.
The shared 2D queue draws larger camera distances first, keeping backgrounds behind
nearer panels, item icons, text and fades. Equal-depth submissions retain their
original order. Original `njDrawQuadTexture` and `njDrawPolygon` accept reciprocal
depth, so their adapters convert it to camera distance before sorting; the
negative-priority overlay bridge compensates when calling these APIs. Title
backgrounds, menu plates and fades therefore use the same ordering as inventory
sprites. The native integration test captures an assembled inventory
frame at `/tmp/recvx-game-inventory.ppm`, including textured triangle sprites
and per-vertex color, so visual checks exercise the actual menu composition.
Pulse strips use native GE texture and blend passes for the authored base/offset
colors. Ordinary 3D fog uses the GE near/far ramp; it does not reproduce every
quantization step in the PS2 lookup table.

Ninja model strips use counterclockwise GE front faces after the camera-to-GU
coordinate conversion. Clockwise selection culls the exterior and exposes model
interiors. Back-face culling remains enabled for single-sided
materials; double-sided materials disable it as authored.

Screen-space effects preserve the original ordering table's alpha-dependent
depth writes (`Ps2ZbuffOff` / `FB_ONLY`). The GU draws their color without
writing depth, then writes depth only for fully opaque pixels in a color-masked
pass. This prevents the opening full-screen translucent effect from hiding
Claire and other subsequently drawn models, while opaque texels still occlude
geometry. Ordinary 3D geometry keeps its normal depth writes.

## Save slots

`CreateSaveScreen`, `ExecuteSaveScreen`, `CreateLoadScreen` and
`ExecuteLoadScreen` use actual PSP files, rather than reporting a PS2 memory card.
Fifteen slots live in `data/../saves/SAVEDATA-00.bin` through
`SAVEDATA-14.bin`. CROSS selects/confirms, CIRCLE cancels, and UP/DOWN selects a
slot. Existing slots require an additional overwrite confirmation. Title Load
is enabled when a validated slot exists.

Each slot contains the original fixed-width 0x838-byte SAVEFILE and additive
byte checksum. Reads validate the full image before restoring the original
save payload. Writes stage save-count, ribbon and special-save changes, verify
a synchronized temporary file, preserve the prior slot as `.bak`, rename the
new image into place, then verify it again before changing live gameplay state.
A missing or corrupt primary can recover from a valid backup. System settings
and ranking-record persistence through the separate system-save screen still
use the explicit no-card lifecycle; those are not game-save slots.

Scenario opcode 56 (`bhFrameCheck`) compares player, room-enemy, or object
animation frames using the original signed 16.16 frame value and optional
half-frame threshold. It consumes all six bytes and returns the original
condition result so event loops resume when the animation reaches that frame.
The PSP port reads the packed frame operand bytewise, including at odd script
addresses, and validates actor indices and object arena storage before use.

Scenario opcodes 158 (`bhPuruPuruFlagSet`) and 159 (`bhPuruPuruStart`)
execute the original two-byte handlers. Selector zero enables event vibration
mode; every nonzero selector disables it. The sound service retains that mode,
while pattern requests return without hardware output because stock PSP has no
vibration unit. The interpreter rejects truncated operands before either call.

Scenario sound opcode 134 (`bhEasySESet`) executes its original 16-byte command
and `PlayGameSe4Event` routing. Player, room-enemy, object and item model joints
supply bounded positions. Scripted gain and pan overrides survive voice starts
and update the native SAS voices without decoding ADPCM. Frame-based curves
retain the original volume/pan sentinel values and zero-duration endpoints.

The sound batch also enables opcodes 70/71 (next-room settings), 117–119
(slot layout, flags and volume), 121/122 (enemy sounds), 127 (pan), 131
(the original two-byte no-op), 135 (flag reset), 146–149 (alternate background
and BGM commands), 167 (alternate BGM volume), 182 (room sound case) and 212 (system sound). All command
spans are checked before handlers advance the script or change sound state.

Opcode 105 (`bhCommonCtr`) now includes all 53 subcommands present in the
original `Common_controll`: actor/joint translation and rotation, motion setup
and explicit frames, companion motion flags, linear/Overhauser interpolation,
control-point capture, visibility and event counters. Packed halfwords use
bytewise PSP-safe reads. The interpreter checks each command's actual span,
selector, joint/control-point index and interpolation divisor before mutation.
Unknown subcommands remain explicit faults.

Opcode 41 (`bhCamPauseSet`) retains its original two-byte selector: zero
holds event-camera mode 3, one resumes mode 1, and other values leave the mode
unchanged. The camera guard accepts the authored pause state; the original
controller preserves position, keyframe and timer while paused and resumes
from that saved state. Truncated commands are rejected before changing mode.

## 3D performance

Model rendering uses a bounded 256-entry cache for transformed positions,
normalized normals and smooth vertex lighting. The cache expires for every
model draw, so skinning, morphs, model matrices and lights cannot reuse stale
results. UV coordinates remain per strip corner, material changes invalidate
cached colors, and flat shading / zero-normal face fallbacks retain per-face
lighting. Cache misses use the existing PSP VFPU transform helpers. Camera
space light positions and directions are prepared once per model.

The GU retains texture/palette bindings across batches sharing the same
immutable texture view. Palette snapshots and shadow texture-state changes
force a fresh bind. The main loop counts render time toward the original
30 Hz cap instead of adding two vblanks after every render.

Every 120 presented frames, `boot.log` records a `perf` line with average
`frame_us`, `tick_us`, `gu_build_us`, `gu_wait_us` and geometry-work counts.
`frame_us` includes presentation waits; `tick_us` includes game logic and GU
submission/wait. High `gu_wait_us` points toward GPU pressure; high tick time
with low GU wait points toward CPU work. These are hardware measurements when
running on PSP, and do not infer FPS from host tests.

For a repeatable CPU stress check, run
`ASAN_OPTIONS=detect_leaks=0 RECVX_RENDER_BENCHMARK=1 build/host/game-boot-render-test`
after `make game-boot-render-test`. A 24-corner strip emits 66 vertices but now
transforms and shades only its three unique vertices. In the same host
sanitizer build, 1,320,000 submitted vertices took 0.797 s before and 0.412 s
after this pass. This synthetic result is not a real-PSP frame-rate claim.

Opcode 52 (`bhObjLinkSetPly`) preserves the original 12-byte player-to-object
link command, including ignored byte-1 padding, joint selection, signed
hundredth-unit offsets and the link-enable bit. Nonzero disable selectors
still store the parent/offset fields without requiring a live joint. Enabled
links validate the player pose and follow it through the original model/link
controllers. Offset halfwords are read bytewise for PSP alignment safety.

## Verification

Focused sanitizer targets cover ranking/typewriter, system-save cancellation,
real save files and failures, inventory entry/navigation/exit, item use and
combination, inspection actions, file pages/bookmarks, reverse skinning,
collision, doors, terminals, game-over, textures, rendering and GU commands.
`make game-boot-gu-raster-test PPSSPP_HEADLESS=/path/to/PPSSPPHeadless`
executes the shipping GU adapter through PPSSPP's software GE. Its seven pixel
checks cover translucent full-screen effects, opaque occlusion, mixed opaque /
translucent texture pixels, transparent texels, front/back-face winding and a
colored shell that detects exposed interiors. The PRX produced by
`make game-boot-gu-depth-probe` can also run on PSP hardware.
`game-boot-map-assets-test` checks all 71 MRY index/floor resources used by the
map. The native 32-bit `game-boot-map-control-test` executes actual MRY loading,
model relocation, texture decoding and original map normal/zoom/exit tasks.

`build/host/game-boot-abi-test build/psp/data` exercises the full original engine,
including sustained opening-room gameplay and actual inventory rendering and
restoration. Host GU and codec tests inspect calls and data; they are not a PSP
rasterizer or a complete playthrough. The rebuilt EBOOT still needs real PSP
verification of these newly added UI, map and save/load paths. Door asset
initialization and many enemy/effect handlers remain separate explicit stops.

The full-game scenario table now maps all 223 assigned original opcodes. The
remaining 130 handlers are extracted by `extract_original_event_remaining.py`,
with packed halfword reads adapted for odd-address PSP script data. The 33
original `dm0` slots remain unsupported. `check_game_boot_opcodes.py` compares
each slot and handler name against the original table and rejects duplicates.

The batch includes player/object subcontrollers, actor links and motion pauses,
facial controls, inventory transfers, floor/wall/position changes, light/fog
interpolation, timers, queued voices and effects. Shared original services now
cover effect allocation/snapshots/cleanup, blood and sand spawning, muzzle
flashes/casings/lighter effects, door setup, stair motion, alpha-fade setup,
main texture release and extra-game inventory initialization. PSP adapters check
actor/work mappings, weapon IDs, joint poses, room resource indices and script
lengths before the relevant reads. Particle sprites use GU billboard triangles;
object emitters use native SAS MIDI slots with positional gain/pan and script
ramps. Compressed sound remains decoded by PSP hardware.

Native validation checks 223 populated slots, 33 rejected reserved slots, every
truncated length of the 130 new instruction layouts (including all 18 player
and two object subcommands), and representative state changes with exact PCs.
The opening-game harness also runs 1,201 frames. Focused tests cover particle
billboard geometry/matrix preservation and persistent object sound registration,
slot reuse, replacement, deletion, full capacity and scripted gain/pan ramps.

Dispatch coverage is not completion of every game subsystem. Existing explicit
boundaries in `config/psp_boot_unported.txt` still apply to unported effect and
actor callbacks; spawning an effect does not implement its future update.
These checks are host/native validation, not a complete playthrough or a
replacement for testing the new EBOOT on real PSP hardware.

The next callback batch ports `bhEff000`, `bhEff002`, `bhEff007`, `bhEff300`,
`bhEff301`, `bhEff302`, `bhEff_E02_SandDust`, `bhEff_E02_SandParticle`, and
`bhEff_E02_SandParticle2`. These are original casing, muzzle-flash, blood mesh,
blood-pool, staged-particle and sand updates, extracted with their supporting
wall/water queries, segment collision tests and collision-plane normal tracking.
The original effect tables already reference these functions; their explicit
unported entries have been removed.

The auxiliary effect contexts remain owned slots in the original 512-slot pool.
Checks reject overlapping or invalid contexts, invalid modes/types, stage and
vertex indices, UV animation pointers/frames and full render queues. Sand's
untextured particle draw callback now submits GU triangles with fixed projected
pixel size, preserving the original depth scaling and view aspect. Existing
textured particle billboards retain their world-space size behavior.

Validation covers all 19 staged-particle presets through auxiliary-slot release,
all six sand dust animations, sand gravity and emitter timing, 11 muzzle-flash
variants through completion, blood mesh/pool updates, both casing initializations,
13 malformed callback states, wall clipping/normal and water-volume queries.
The native opening-game regression still runs 1,201 frames; the focused GU sprite
fixture passes 176 checks. These checks do not establish visual parity for every
new effect on hardware. Other effect, enemy and player callbacks remain explicit
boundaries in `config/psp_boot_unported.txt`.

`bhCheckExmAtari` now runs the original five examination probes for doors,
stairs/ladders, ledges, examine/message points and item/object interactions.
It preserves facing and floor restrictions, stair occupancy, wall-clearance
checks, door destinations, player/controller handoffs and input backups.
The collision extractor uses checked item/object lookup at each original access;
entry checks validate the expression work, static/dynamic record spans, probe
types and finite dimensions. The existing original message, stair, floor and
wall services supply its dependencies.

The sanitized collision fixture passes 72 checks, including the new interaction
branches and invalid work/record/slot rejection. The 32-bit native harness also
checks door, examination and item transitions with loaded Claire and real arena
work, and passes its 1,201-frame opening-game regression. Door demo and specialized
player-action callbacks that follow these transitions retain their own explicit
boundaries until ported.

The `unported original specular chunk lighting` boundary is now implemented.
Textured strips follow the retail color-function tables in `ps2_NinjaCnk.c`
and `ps2_Vu1Strip.c`: Easy lighting uses the overbright ramp, Simple single
lighting uses the retail fixed power of 17, and Simple multi lighting excludes
ambient from its specular ramp. Multi-light paths retain the retail white
specular tint; single-light paths use the material tint. Untextured strips use
the retail diffuse/ambient path. Ignore-light, ignore-specular, ignore-ambient
and constant-material overrides retain their respective paths.

Lit textured RGB is packed at half scale and the PSP GE doubles RGB after
texture modulation, keeping highlights on darker texels without another draw
pass or changing alpha. The sampler bit is captured per batch and explicitly
cleared for subsequent ordinary textured batches. TEXFUNC's RGB-doubling bit
is also documented in the [PPSSPP GPU state implementation](https://github.com/hrydgard/ppsspp/blob/master/GPU/GPUState.h);
the installed PSPSDK `sceGuTexFunc` implementation was checked to preserve it.

Validation passes 443 sanitized renderer checks, the typed GU adapter checks
(including doubled/ordinary modulation on the same texture without alpha-state
leakage), and the 1,201-frame native game regression. Visual parity of the newly
unblocked scene still needs real PSP testing.
