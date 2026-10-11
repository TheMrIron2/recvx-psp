# Original event 38 test

Package 1 builds a separate PSP EBOOT that owns its own `SYS_WORK`, `ROM_WORK`, player record and original `BH_SCEWORK` tasks. It links the extracted original event core and the bounded room/event readers. It does not link the viewer or `legacy_script.c`.

The original 256-entry opcode mapping remains in [event_dispatch_table.inc](../include/ps2/veronica/prog/event_dispatch_table.inc), shared by the full event source and the restricted test table. The test build enables only `bhSet`, `bhSleep`, `bhSleeping`, `bhEvtNext` and `bhEvtEnd`; other opcodes stop with an explicit fault. The core follows the original PS2 decompilation at [event.c, pinned revision](https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/event.c) and uses [recvx-vita](https://github.com/Rinnegatamante/recvx-vita/tree/064aeb04d9bc6142bafea3e55a707935c1c2efb3) as the portability reference.

## Build and activate

Use the user's local USA PS2 ISO as the only source of room data:

```sh
make engine-event-test
make engine-event-test-assets ISO=/path/to/local/RECVX.iso
```

The EBOOT is `build/psp-engine-event-test/EBOOT.PBP`. Copy it and the whole `build/psp-engine-event-test/data/` directory to:

```text
ms0:/PSP/GAME/RECVX-EVENT/EBOOT.PBP
ms0:/PSP/GAME/RECVX-EVENT/data/rooms/RM_0000.rdx
ms0:/PSP/GAME/RECVX-EVENT/data/rooms/RM_0000.meta
```

Start **RECVX Original Event 38 Test** from the PSP Game menu or open the PBP in PPSSPP. The test prints `TEST PASSED` only after it validates the event section and span hash, runs `Event_exec(7, 38)` through `bhEventScheduler2`, checks 44 `bhSet` calls and all 16 key-flag words, confirms task 7 is inactive at body PC `0x10a`, and verifies `Event_T_timer == 1`. It also runs the sleep, yield, scheduler-gate, task-order and malformed-input checks before the original event. Press START to exit.

The test asset output lives under ignored `build/`; the room payload and any other copyrighted game data must stay out of commits.

The same portable harness can be run on the host after the asset target:

```sh
make build/host/event-core-test
build/host/event-core-test build/psp-engine-event-test/data
```

`make test` also runs the core contracts and the RM_0000 event proof when `build/psp/data/rooms/RM_0000.meta` is present.

## Hardware checklist

- Record PSP model, firmware/CFW, CPU clock and the local ISO used to prepare `data/`
- Confirm the event span SHA-256 is `e6e8d61eba16c34eca97697e426a9719b329ff2cf5cbe8d8d06de8d5242f56b8`
- Confirm the screen reports 44 writes, 45 instructions, timer 1, body PC `010A`, and all 16 expected key-flag words
- Cold-start the test three times and confirm the trace is identical; exit with START and relaunch once
- Boot the regular viewer EBOOT separately and check room loading, Claire walking/animation and GU display modes
- Save a PPSSPP log and a PSP photo or PSPLINK log with the test result and device details

The first PSP boot reported an address load/inst fetch fault in `bhSet` at an `lhu` from an odd event address. The core now reads the `bhSet` and `bhSleep` operands bytewise as little-endian values, and the acceptance harness includes an explicitly odd-address sleep/flag fixture. The rebuilt EBOOT has not yet been smoke-run on PSP; copy the updated `EBOOT.PBP` before the next device check.

## Package 1 results and limits

The extracted scheduler preserves the original ascending 16-task pass and one timer increment per pass with active work. The test keeps the event table's offsets relative to `rom->evtp`, verifies each body range before dispatch and records the faulting section PC. The PSP build asserts the event/task layout and signed `char` requirement. The host harness uses the same original core with a two-word event-table prefix because host pointers are 64-bit.

This is not SCD0 or opening-scene initialization. It does not call `bhInitEvent`, enemy control, sound commands, or any handler outside the restricted table. Later packages still need original room ownership, platform services and the player/actor initialization path. The default viewer remains a separate build and does not share its live state with this test.

The existing player-test baseline still fails at `tests/test_player.py:64`. Its first wall case requests `dt=0.1`, while the diagnostic updater caps the step at `0.05`; the resulting `z=-0.5` is exactly the current non-overlap wall boundary, so `blocked` remains zero while that assertion expects one. This collision-contract issue was investigated separately and is unchanged by Package 1.
