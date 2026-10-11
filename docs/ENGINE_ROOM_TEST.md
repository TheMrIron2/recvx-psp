# Engine room owner test

Package 2 adds an isolated `engine-64` test profile. Its PSP program first reruns the Package 1 original event-core proof using RM_0000, releases that fixture, then loads RM_0010 through the new room owner. The event scheduler and the viewer's diagnostic interpreter do not share live room or task state.

## Build from the local ISO

Room assets are generated from the user's local PS2 ISO and remain under ignored `build/` output:

```sh
make engine-64-assets ISO=/path/to/recvx.iso
make -B engine-64
```

The testable PBP is `build/psp-engine-64/EBOOT.PBP`. Copy that file and the complete `build/psp-engine-64/data/` directory to a memory stick folder such as:

```text
ms0:/PSP/GAME/RECVX64TEST/EBOOT.PBP
ms0:/PSP/GAME/RECVX64TEST/data/rooms/RM_0000.meta
ms0:/PSP/GAME/RECVX64TEST/data/rooms/RM_0000.rdx
ms0:/PSP/GAME/RECVX64TEST/data/rooms/RM_0010.meta
ms0:/PSP/GAME/RECVX64TEST/data/rooms/RM_0010.rdx
ms0:/PSP/GAME/RECVX64TEST/data/rooms.txt
```

Launch **RECVX Engine Room Test 64MiB** from the PSP Game menu. PPSSPP can run the same EBOOT and data folder with its 64 MiB PSP model. The test prints a specific failure stage if the ISO-derived room files are missing or invalid. Press START to exit.

## What the owner guarantees

`EngineRoomOwner` owns one expanded RDX buffer from successful validation through close. Its public view exposes room and event bytes only after bounds and generation checks. Callers pin a view while original tasks retain pointers; close reports busy until all pins are released. A closed or replaced generation rejects old views.

Placement accessors preserve the source array index, placement ID, type, flags and `work_no`. The event view references the original RDX event directory without making another copy. Model-directory entries are range-checked and counted, but nested model records are not relocated or realized in this package. Allocation callbacks inject failure at both the packed and expanded buffer allocations and preserve matching release behavior; custom allocator context must remain alive until owner close.

The owner does not create `ROM_WORK` native pointers, call `bhSetRoom`/`bhFinishRoom`, realize models, or initialize player, actor, collision, motion, texture or task work. This is a bounded disk-backed view, not a playable room.

## Host and build checks

`make test` runs the owner cycle test when `build/psp/data/rooms/RM_0010.meta` exists. The direct test command is:

```sh
build/host/engine-room-owner-test build/psp-engine-64/data
```

The PSP screen reports the 18 MiB + 4 KiB touched allocation probe, kernel total/largest free memory, libc arena/used/free values, the RM_0010 resident size and counts, ten load/release cycles, and read/allocation cleanup probes. The final make check confirmed `MEMSIZE=1` in both `PARAM.SFO` and the PBP-embedded SFO. The PSP ELF stores heap size `-1` and heap threshold `4096` KiB. Its only unresolved ELF symbols are weak SDK metadata hooks; no engine APIs are stubbed into this test image.

The existing `tests/test_player.py:64` assertion remains a separate baseline failure. It expects the player to be blocked at `z=-0.5`, while the updater caps `dt=0.1` to `0.05` and treats that exact wall boundary as non-overlap. Package 2 does not change player or collision behavior.

## PSP-2000+ hardware checklist

Run this on the supported CFW configuration and keep the displayed values with the test result:

| Check | Result |
| --- | --- |
| PSP model and CFW | Not shown in the submitted capture |
| Startup kernel free / largest block | 4532 / 4096 KiB |
| Touched 18 MiB + 4 KiB allocation | PASS |
| libc arena / used / free at startup | 0 / 0 / 0 KiB |
| libc arena / used / free with RM_0010 resident | 4352 / 4096 / 256 KiB |
| libc arena / used / free after release | 0 / 0 / 0 KiB |
| RM_0010 validated on ten cycles | PASS, 1,829,316 bytes, generation 11 |
| Read and both allocation failure cleanups | PASS |
| Final screen | TEST PASSED |

Hardware memory approval passed in the submitted capture. The startup kernel largest block retained the 4 MiB external reserve, and the room's libc allocation returned to zero after release.

## Deferred dependencies and integration notes

- Native `ROM_WORK` relocation and the original room finalizer remain deferred until Package 4 has the original model, player and actor prerequisites
- `binfunc.c` mutates model pointer fields in place; future GU input must come from validated source records before relocation or from a separately owned realized representation
- The next engine slice still needs verified SYSTEM, player/costume/weapon and message identities from the local asset set
- No original room model, motion, player, texture-upload, audio or monitor service APIs are linked by this test profile
- The original PS2 optimization guide URL specified in `AGENTS.md` was unavailable to the web reader during this package; the profile follows the roadmap's explicit PSPSDK heap and reserve settings
