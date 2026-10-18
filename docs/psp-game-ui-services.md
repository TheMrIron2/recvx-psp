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
