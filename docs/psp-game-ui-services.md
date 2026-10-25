# Original game UI services on PSP

The game build now links the original ranking, typewriter, inventory, item
inspection, file viewer, computer terminals, game-over, and map controllers.
Their generators retain the supplied original function bodies and static data;
PSP runtime files validate resources, arena ownership, indices and state before
those routines touch packed data. `config/psp_boot_unported.txt` remains the
list of explicit stops for services outside this completed batch.

Inventory uses the original ITEM1 textures, layout, palettes, messages and item
logic. Entry backs up room textures; exit restores their pixels, live palettes,
render settings and the original arena cursor. Backups that cannot fit beside
the live room texture list use an owned heap buffer, with a checked 16-byte
reservation in the original arena. This accommodates the PSP's TIM2 sources,
expanded pixels and palettes without overwriting the list or exceeding the
arena. Restore, replacement and shutdown free that buffer.
The PSP draw adapter supports
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

`bhStartDoorDemo` now uses the original two-state entry/handoff routine. Entry
starts the ten-frame black fade and native sound fade-out, resets door counters
and controller bytes, preserves the event-timer enable bit, clears input,
selects the destination and switches to main mode 2. While waiting it leaves
state unchanged. The room-ready flag starts the five-frame fade-in, clears the
door request, restores update flags and forces the camera cut. Invalid entry
states stop before mutation.

The sanitized door fixture checks both event-timer settings, all reset fields,
destination preservation, repeated waiting frames, completion callbacks and
invalid/null state. The native harness checks the entry/handoff using loaded
Claire, real arena interaction records and real sound/fade services, and still
passes 1,201 game frames. This completes the door-start handler; animation
initialization/loading/setup/exit (`bhInitDoor`, `bhReadDoorData`, `bhSetDoor`,
`bhExitDoor`) remain explicit boundaries.

## Player damage, scripted movement, traversal and attacks

The game build now extracts the requested retail player controllers and their
immediate dispatch dependencies with `tools/extract_original_player_remaining.py`:

- Damage/death, enemy-controlled damage/throw/death, parked animation and all ten
  event dispatch slots (nine original `pl_smove` routines).
- Stair ascent/descent, ledges, pushing, crouching and both ladder directions,
  including their alignment, motion, floor and exit phases.
- Attack dispatch, weapon ready/aim/release/fire/reload/knife, pitched weapon and
  scope controllers, targeting searches, ammunition and gun/knife collision.
- XYZ pose compensation, joint/position water splashes and player-eye camera
  interpolation, tracking and return to the active camera.

The new runtime checks reject invalid dispatch phases, weapon/player selectors,
model joints, motion frame counts, room floors/camera cuts, inventory banks,
enemy counts and capsule lists. Scoped weapon control uses `plp->at_flg` for the
retail alias at offset `0x3ec`, avoiding a cast through an unrelated system struct.
`bhControlMonitorCamera` retains the original empty body. `njDrawModel` retains
its original `vpDummy = model` bookkeeping; game geometry continues through the
existing chunk renderer. Neither routine implies a new basic-model renderer or
monitor-camera implementation beyond the supplied original behavior.

Damage testing also exposed an inclusive sound-bank ID boundary: Sony program,
sample-set, sample, waveform and MIDI tables store the highest ID, not the number
of entries. Native bank validation and lookup now include that final ID. Tests
play all four real Claire voice programs, including `0x403`, and the room's last
MIDI sequence (`0x271`) through the SAS call model. Compressed ADPCM remains on
the native hardware path.

Validation includes a sanitized five-frame parked-animation fixture and 32
malformed-work cases, the original action/collision regressions, real relocated
player/controller checks in the native 32-bit full-game regression, the native
SAS adapter regression, and the PSP game build. Scene-specific motion banks and
weapon motions must be loaded before their corresponding controllers run.
Remaining effect callbacks and other named unported services still report their
existing boundaries when reached. Real PSP hardware playback and traversal need
a device run.

The remaining door lifecycle now extracts the retail package reader, parameter
selection, animation callbacks, lights, fades and vibration scheduling. Package
checks cover the reserved buffer, section extents, model tree, callback selectors
and the light records actually read (unused trailing lights may be omitted).
Sound commands 0 and 4 through 9 retain the supplied retail table's empty
callbacks. Mounting `COMMON.MLT` also installs its embedded sequence bank at bank
1, as `sdMultiUnitDownload` does; door SPQs supply the instruments for those
shared sequences. Native SAS tests exercise all four shared sequences with a
matching door instrument bank and playback after replacing that bank.

The game-service extraction adds the supplied ant enemy controller, event timer,
fixed/player-eye cameras, scope overlays and reflected model submission.
`njMirror` uses a scalar plane reflection and the chunk renderer reverses winding
for reflected transforms. Full-screen and monitor captures synchronize a GE copy
of the PSP framebuffer into linear RGBA textures; capture projection and UV scales
account for the PSP's 480 by 272 output. Texture push/pop preserves the selected
list, palette and filter across display transitions. The timer's effect 26 is
included so its expiry path can run. These paths have host fixtures and PSP builds;
rendering appearance and sound output still require a real device run.

The native 32-bit regression passes setup and the first animation frame for all
85 populated door packages, preserves the retail rejection of the empty door
slot, and runs one complete door demo. It also passes the camera, scope, enemy,
timer/effect, reflection, capture/restoration and texture-state fixtures. The
sanitized package guard suite rejects 18 malformed cases. The sound dispatcher
and production SAS adapter suites pass, as do the PSP build/runtime checks.

Large-room decompression no longer requires the compressed and expanded copies
to coexist in the 12 MiB game arena. If the bounded decode reaches the compressed
input, it stops without overwriting it, copies that input to temporary heap
storage, and retries using the remaining arena capacity. The temporary copy is
freed before room validation and relocation. Malformed streams, extra trailing
input, and output exceeding the actual arena still stop with diagnostics showing
the decode result and byte counts. This leaves the permanent arena size unchanged.
The regression decodes all four packaged rooms above a reserved 1152 KiB prefix,
checks their exact output sizes/CRCs and arena guards, and exercises the staged
path for RM_0020 and RM_0030.

The PSP input bridge maps Up/Down/Left/Right to the retail peripheral bits
`0x1000/0x4000/0x8000/0x2000`. Analog axes account for the negation in `bhSetPad`,
so the stick and D-pad produce matching logical directions. The regression checks
both through the original pad mapper for all four control presets, including
press/release, diagonals and neutral.

Room validation keys enemy texture sharing by the full authored model-version
byte. RM_0020 includes zombie variant 32, which exceeds the retail SYS_WORK
table's 16-version dimension. Versions below 16 retain that table; higher versions
use a bounded PSP cache keyed by enemy ID and version, reset on room relocation
and game initialization. The extracted model loader uses the same cache accessor,
avoiding an index into another enemy's table entries. Validation failures also log
the rejected record category before the room-relocation boundary.

Enemy/room motion validation now uses the retail 512-slot MN_WORK capacity,
independently of the 128-slot player asset container. RM_0020 and RM_0030 contain
476-slot zombie banks. Each populated MTN record still passes the shared record
parser and alignment checks; streams exceeding 512 slots, truncated records and
invalid terminators remain rejected. Validation no longer allocates an entire
player asset container on the room-loading stack.

The native regression now loads queued textures and runs `bhFinishRoom` for all
four packaged rooms, exercising motion relocation, actor matrix allocation,
facial masks, lights, effect placement and spawn initialization. It retains the
arena guards, output CRCs, variant-32 texture-cache sharing/isolation checks and
checks the exact 512/513-slot boundary. Independent fixtures release their texture
ownership between rooms. The title check preserves existing workspace saves and
uses the corrected directional controls to choose New Game when Load is enabled.

The game profile now links all requested enemy entry points (`bhEne00` through
`bhEne30`, plus `bhEne53` through `bhEne56`) from the available retail source.
The enemy extractor retains their state tables and shared zombie, collision,
route, blood/splash, particle and material helpers, and reuses helpers already
ported elsewhere. Callback-table calls check the selector and callback before
invocation. Entry guards check actor ownership, model/motion work, phases and
finite positions; callback registration checks the 100-entry enemy table.

Adaptations include indexed XYZ key reads for spider controllers, the missing
linked-zombie allocator return, explicit no-action returns in state checks, a
bounded fallback wall direction, and the correct matrix-pointer type for Steve's
particles. Enemy sound requests use the retail six-slot request manager, native
SAS voice status and gain fades. ADPCM stays compressed throughout playback.

Native regression coverage includes a real RM_0020 zombie group, initialization
of linked actors through the original enemy scheduler, subsequent movement,
shared texture variants, positional enemy sound requests/fades/stops, and knight
idle/damage state transitions using owned fixture models and motions. Six malformed
actor/work/dispatch cases are rejected. All four room finalizations, the existing
full-game regression, native SAS adapter and sound-service tests pass. Only the
packaged room assets can be exercised here; boss-specific encounters and real PSP
behavior still need device coverage. The supplied source contains 88 empty void
bodies across the generated modules, including the unused `bhEne28` entry point;
these retain their original behavior rather than inventing missing enemy logic.


## Effect callbacks and timer display

The PSP now links the original `bhDispTime` and effect callbacks 003–006, 008,
010–014, 016–019, 022–025, 027–029, 085, 100–105 and 107–110. Their weapon,
explosion trail/particle and rain drawing routines are included, together with
bomb/projectile collision helpers and emitted effects 119 and 132. Effects 004
and 028 have empty bodies in the supplied original source and retain that behavior.

UV animation reads check the original local tables and frame bounds. Auxiliary
work remains in the bounded effect pool; custom drawing validates particle frames
and emission indices. Queue writes use the existing capacity checks. Weapon flash
variants, linked wall/actor selectors, water ripple buffers and texture swaps are
checked before use. Stopping the persistent rain effect releases its auxiliary slot.

Effect 110 uses an owned 512×512 RGBA render target for the existing GE capture
path, rather than treating its empty PS2 render surface as a TIM2 file. Texture
swap restore and arena alignment use pointer-width arithmetic. Timer drawing
preserves the original 60 Hz conversion, field flags, separators and game-mode clamp.

Native coverage exercises animation expiry/looping, the custom drawing paths,
rain movement, wind state, render-target creation, timer glyph order and the
59:59.99 clamp, plus malformed callback state and existing full-game fixtures.
Visual appearance of these additional effects still needs real PSP coverage.

## Monitor, debris, linked flame and gas effects

Callbacks 111–114, 116–118, 120–123 and 127–129 now use their original update
bodies in the PSP game runtime. `bhDraw114` submits the original debris lighting
through the half-lit texture backend. Its first 15 atlas tiles use zero-based
coordinates; the reconstructed subtraction produced negative UVs. The original
empty `bhEff129` body remains empty.

Effect 111 registers an owned capture surface in the effect texture list, using
the same GE framebuffer transfer as the other monitor/render-texture paths. Raw
capture descriptors retain their owned source marker for texture garbage checks.
Model UV scrolling uses a scalar `npSetOffsetUV`: it checks command sizes, strip
lengths and per-triangle attribute words, validates the whole stream before
mutation, and preserves 16-bit UV wrap. Linked-model selectors, animation tables,
debris slow-motion divisors and monitor/particle state are checked before use.

Effect 118 counts successful child allocations, skips failed allocations and
retires lost or finished child indices once. A full effect pool therefore cannot
turn an allocation failure into `eff[-1]` or leave the shutdown counter waiting
for nonexistent children.

Regression fixtures cover all 55 debris atlas selections and their draw/update
paths, the four emitter burst counts, monitor allocation/capture/fade, linked flame
shutdown and pool exhaustion, looping/selected animations, timed sparks, gas
particle lifetimes, mirror activation, gas/smoke spawning and reversible UV edits
with attribute-bearing strips. Malformed-state coverage now includes 35 callback
fault cases. Visual verification of these effects on PSP hardware remains needed.

## Spark, explosion and bubble effects 155–168

The original callbacks 155–168 and their `setentry`, `effinit`, `effset` and
`getuv` helpers now run in the PSP runtime. Effect 169 is included because 168
spawns it. These effects use the existing textured 3D sprite renderer and matrix
backend; moving sprites preserve their rotated velocity, gravity, explosion
texture-bank changes, countdowns and saved transform restoration.

Local UV-table identity and frame bounds are checked before reads, including the
second lookup after bubble animation control commands. The bubble commands retain
repeat, pause, restart and termination behavior. Helper texture/color/orientation
selectors and emitter countdowns are checked, and render queue writes retain
capacity guards. A failed child allocation preserves the original emitter state
transition without dereferencing the failed slot.

Regression fixtures exercise all sprite variants and their texture-bank timing,
rotated quad orientation, finite and looping bubble animation, one-shot/burst/
periodic spawning, full-pool behavior and moving emitter restoration. Sprite draw
checks use owned font pages as fixture atlases where these effects' textures are
absent from the opening room. Hardware appearance still needs device validation.

## Repeated sprites, blends and smoke effects 170–182

The PSP now links original callbacks 170–182. They preserve the moving emitter's
saved transform and accumulated displacement, repeated sprite animations,
configurable source/destination blending, a static full-atlas sprite, timed
moving sprites, periodic emitters and smoke. `bhEff174` is empty in the supplied
original source and retains that behavior.

Smoke keeps all ten atlas selections and ten color presets, using the original
fractional part of its speed parameter. Timed child allocation carries the
parent's lifetime selector. UV reads use the bounded local-table checks; blend
and smoke selectors, counters and float-to-integer emission intervals are checked
before use. Shared helper validation uses its own reserved kind so effect 170's
countdown checks cannot accidentally apply to every sprite helper call.

Regression fixtures cover all 100 blend combinations and 100 smoke style/color
combinations, repeat counts including continuous repetition, static/timed sprites,
child lifetime propagation, one-shot and periodic emission, full-pool allocation
failure and moving-emitter restoration. The existing owned fixture textures exercise
the sprite submission path; visual verification with these effects' actual room
atlases still requires the relevant scenes and PSP hardware.

## Linked eye/drop effects and debris 145, 147–154

Callbacks 145, 147–154 now run with the original eye-spawn helper and effect 146,
custom sprite drawing and effect 264's native particle submission. The eye helper
checks allocation success before indexing the pool; the reconstructed OR test
could otherwise write to `eff[-1]`. UV frame/pointer selectors and manual render
queue writes are bounded. The fire sprite retains its wake/grow/hold/fade/reset
phases and spawns the original 16-particle fire-spark effect.

Regression fixtures cover eye color/fade and pool exhaustion, full/hidden queues,
drop gravity/reset, custom fire drawing and spark-context release, debris variant
switching, all twelve impact frames at three alpha levels, phased line emission
and alternating spark bursts. These paths are also included in the full-game
native regression.

## Sony compact sequence loop controllers

The stairs-room sequence parser now implements CC 99 (loop start/end selection),
CC 6 (loop number) and CC 38 (repeat count) instead of stopping on these controller
messages. The controller identities and zero-count continuous loop convention
are documented in [VGMTrans's Sony PS2 reader](https://github.com/vgmtrans/vgmtrans/blob/master/src/main/formats/SonyPS2/SonyPS2Seq.cpp).
Each sequence owns its loop markers. Jumps restore the stream position, running
status and compressed immediate/delta state. New playback starts with fresh loop
state; stopping or replacing a sequence prevents further loop events. Invalid
commands, missing markers and zero-time infinite loops produce bounded faults.
Unsupported-controller diagnostics now include the controller/value, slot, channel
and stream position.

The native SAS adapter regression plays bank key 0x14, sequence 0x203 beyond the
first loop and after stop/restart. Isolated fixture packs verify numbered loops
on channel 3 with loop ID 127, finite and continuous playback, immediate timing,
invalid loop markers and the existing event watchdog. Compressed sample playback
still uses the production SAS adapter; no software ADPCM decoder was added.

## Shared actor model work during drawing

The draw guard accepts model descriptors in any registered player, enemy, effect,
object or item model table, in addition to the shared effect-model table. The
original linked-zombie and fragment controllers borrow their parent's `mdl`
entries; requiring every actor to point into its own table incorrectly stopped
room 0/2 after the stairs transition. Model-pointer alignment, object counts and
owned node/pose spans are still checked before drawing. Rejections log the actor,
model pointer, ownership result, node count and pose pointers for diagnosis.

Focused draw tests cover parent/child sharing, enemy-model fragments, player and
object/item model reuse, and malformed pointers/counts/spans. The real RM_0020
zombie fixture now initializes the normal camera pass and draws enemies and
linked objects after their scheduler updates.

## In-game sound levels and dialogue waits

The native sound bridge now uses the original listener-to-emitter direction for
player, footstep, weapon and enemy sounds. The former reversed vector applied
rear attenuation to sounds in front of the camera. Sound-bank gain follows the
linear Sony level used by `sdShotSetVol` / `sdMidiSetVol`; compressed music and
dialogue use the separate `adxwrap.c::AdxVolTbl` curve. Opening music entry 114 at
script volume -45 now receives -14.2 dB attenuation instead of -22.5 dB.
Footstep spatial updates retain the sample/program gain instead of replacing it.

`CheckPlayEndAdx` returns the retail playback flag (one while playing), and
`GetTimeAdx` reports hundredths of a second as `GetAdxPlayTime` originally did.
The previous inverted flag could release scenario waits before dialogue ended.
Focused tests cover both playback states, clock units, positioned footsteps,
player voices, exact native music/dialogue gain and music fade-in after muting.
The full native game regression passes through the opening scenario. Audible
mix balance still requires listening on PSP hardware; all runtime sample
playback remains native SAS ADPCM.

## Zombie companion checks in scenario commands

Opcode 142 (`bhZombieUpDieCk`) reads the linked zombie actor's flags through
`exp1`. That actor is allocated in the global `ene[128]` pool by
`bhEne01_SetLinkEnemy`, outside the room arena. Its guard now accepts exact
actor entries in that registered pool, along with complete owned actor records.
The same correction covers motion-pause opcodes 45 and 48. Null, interior,
out-of-pool and truncated pointers still stop before the original dereference.

The focused event-remaining sanitizer test covers all 128 pool entries and the
invalid-pointer cases. The real stairs-room fixture invokes the original
opcode 142 handler with both companion flag states and checks the room flag,
then invokes both motion-pause handlers and checks parent/child state updates.

## Inventory 3D preview composition

The inventory submits its item model between separate ordering-table flushes,
then submits the remaining menu panels and controls. The GU adapter previously
disabled depth testing for every later 2D draw, allowing those panels to paint
over a correctly loaded and drawn item. It now preserves the model projection's
depth mapping across flushes and converts each UI priority to the corresponding
reversed GE depth. Panels behind the item fail its depth test; foreground
controls still pass. Opaque UI pixels write depth; translucent pixels only
write color. A new display frame resets
this composition state, preserving the existing path for screens without 3D.

The production GU adapter test checks model/background/control depth ordering
across separate submissions and reset on the next display frame. The native
inventory regression now services the original item-read monitor, opens the
lighter context, reads ITEM1 entry 54, and verifies textured geometry, nonzero
alpha, projected visible bounds and the preview scissor before closing and
restoring the room textures and palette. Hardware appearance remains to be
verified on PSP.

## Native bank envelopes, blue-panel retention and title shadow depth

Sony sample volume is byte 16, with ADSR1/2 at bytes 18/20, as recorded by
[VGMTrans's Sony PS2 instrument reader](https://github.com/vgmtrans/vgmtrans/blob/master/src/main/formats/SonyPS2/SonyPS2InstrSet.cpp).
The bridge previously read velocity-high as volume and read both ADSR words
one byte late. COMMON menu samples and the five floor sounds actually use
`80ff/5fe6`; the old shifted `e680/005f` gives SAS an extremely slow attack.
The production adapter tests now check all eleven COMMON programs and five
floor programs, native envelope words, nonzero volume and unpaused key-on.
ADPCM remains compressed and is played by native SAS.

The blue inventory panel is submitted before the item model, with farther
menu artwork submitted afterward. The GU adapter now establishes the UI depth
projection before the first model and uses a color pass followed by an
opaque-only depth pass, retaining the original alpha-fail FB_ONLY behavior.
This keeps both the opaque blue panel and the model above later far artwork,
while translucent UI cannot hide models with depth writes. The command-list
capacity estimate includes both passes.

The title's pulsing plate calls its shadow at reciprocal Z minus .001, which
can legitimately be negative at the start of its fade. Finite signed overlay
distances now pass through the projection and clamp to the GE depth range;
non-finite values still stop. GU regressions cover signed title shadows,
blue-panel/model/far-panel ordering and opaque-only alpha/depth state.

## Effects 191–207

The service extractor retains the original `effsub0.c` callbacks 191–199 and
`effsub5.c` callbacks 200–207. This includes sprite sheets, palette cycles,
burst and periodic emitters, linked moving particles, opaque effect queues,
and the six polygon-particle drawing passes in effect 199. Effect 197 retains
its empty original body. All seventeen names are removed from the unported
boundary list.

Callback checks bound modes, animation frames, variant indices, burst counts,
linked effect-pool parents, and lifetime/interval conversions. The extractor
bounds random atlas selections at their upper endpoint and ensures effect 203
has a positive lifetime divisor, including particles fast enough to round it
to zero. Its alpha and angular damping calculations use wide intermediates.
Effect 196 preserves transitions from eight-frame to three-frame loops.

The native regression exercises sprite lifetimes and geometry with the original
allocator's local quad corners, all 24 palette/type combinations, looping and
held animations, burst and timed child allocation, linked particle expiry and
parent cancellation, opaque queue submission, fast-particle divisors and the
polygon-particle draw passes. Twelve additional malformed-state cases cover
this batch, bringing the callback guard fixture to 93 cases. Rendering fixtures
reuse owned opening-room textures to verify submission independently of the
room-specific effect atlases.
