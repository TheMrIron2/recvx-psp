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
Pulse strips use native GE texture and blend passes for the authored base/offset
colors. Ordinary 3D fog uses the GE near/far ramp; it does not reproduce every
quantization step in the PS2 lookup table.

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

## Verification

Focused sanitizer targets cover ranking/typewriter, system-save cancellation,
real save files and failures, inventory entry/navigation/exit, item use and
combination, inspection actions, file pages/bookmarks, reverse skinning,
collision, doors, terminals, game-over, textures, rendering and GU commands.
`game-boot-map-assets-test` checks all 71 MRY index/floor resources used by the
map. The native 32-bit `game-boot-map-control-test` executes actual MRY loading,
model relocation, texture decoding and original map normal/zoom/exit tasks.

`build/host/game-boot-abi-test build/psp/data` exercises the full original engine,
including sustained opening-room gameplay and actual inventory rendering and
restoration. Host GU and codec tests inspect calls and data; they are not a PSP
rasterizer or a complete playthrough. The rebuilt EBOOT still needs real PSP
verification of these newly added UI, map and save/load paths. Door asset
initialization and many enemy/effect handlers remain separate explicit stops.
