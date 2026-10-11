# RECVX-PSP engine integration plan

Assessment date: 11 October 2026. Implementation handoff for Luna.

## 1. Recommendation and evidence boundary

**Retain the original gameplay engine, integrate it in small executable slices, and replace platform services above the PS2 graphics transport. Keep the PSP GU renderer and asset tools.** Use a separate engine-test mode while the walking viewer remains available. Move towards the original task dispatcher and startup state machines; do not continue adding gameplay semantics to the viewer interpreter.

Vita demonstrates a practical way to compile the original systems together: preserve game entry, globals, task order, event interpreter and resource callers, then supply platform implementations. Its source is a valuable portability reference, not a ready-made PSP backend. Its 160 MiB heap, GS/VIF/VU translation, shader renderer and numerous compatibility compromises make wholesale import inappropriate. The README reports title-to-gameplay progress, missing models/text, slow scenes and absent FMVs; this assessment did not independently run Vita. See [Vita README][v-readme], [build][v-build] and [startup][v-main].

The first implementation task is **an opt-in PSP test that runs the original event scheduler on the existing RM_0000 event 38, verifies its 44 key-flag writes and termination, and reports unsupported commands explicitly**. This is a small integration proof, not a claim that the opening scene works. Section 8 specifies the package. The first playable progression target is the original RM_0010 item-0 pickup and its persistence on room reconstruction, with original input, player interaction, item insertion and scenario logic. Full inventory navigation, combat and movie decoding need not precede that target.

### Revisions inspected

| Source | Revision and observation |
| --- | --- |
| PSP local checkout | `67a16677288edb2c459e6dadb6d60b369d499a18`, `fix armature animation bug` |
| PSP remote HEAD, queried during assessment | `6c25b7bb3f7db92913f3072a296b0017cfbd826a`; local HEAD is two commits ahead, including `c7f95cd9` room actors |
| Original decompilation | `4b2cab1aaf383841c912d81f946601fada9eb001`, freshly checked out for comparison |
| Rinnegatamante Vita fork | `064aeb04d9bc6142bafea3e55a707935c1c2efb3`, freshly checked out; use this fork/revision, not the different clone URL in its README |
| PSPSDK source | `e2b2b313d4175823a263c2c6cf175311e824eb77`; compared with installed SDK files and installed `libcglue.a` disassembly |
| Local changes at start | Untracked `docs/PSP_PORT.md`; read as context and left untouched; no tracked production-code modifications |

References below use pinned external revisions. PSP file locations refer to the local revision above; line numbers are starting locations, not claims about future HEAD. “Verified” means source/build/data inspection unless a runtime environment is explicitly named. Existing hardware and PPSSPP observations in [PSP_PORT_AUDIT.md](PSP_PORT_AUDIT.md) remain historical evidence, not fresh results.

### Fresh validation

- `make -B -j4`: PSP EBOOT rebuild passed, no warnings/errors in its log; GCC 15.2.0. ELF text/data/BSS: 245,152 / 4,928 / 294,104 bytes. The audit's older executable sizes are superseded.
- `make legacy-portable-check` and `make legacy-link-inventory`: passed for the explicit source list, with legacy warnings. This proves compilation/object generation, not a linked game engine.
- `make test`: C ASan/UBSan runner, decompression and room Python tests passed; stopped at `tests/test_player.py:64`, the expected blocked position assertion. Scene, mesh and texture suites passed when run separately. Do not describe this baseline as all-green or weaken the assertion without investigating the changed collision contract.
- A separate PSP syntax check of `event.c` failed on missing declarations for `bhControlCamera`, `bhControlLight`, `bhSetAlphaFadeObject`, `bhSetExplosionEffect`, and a valueless return in `Obj_controll`. Vita supplies the four relevant headers and return fixes. The existing portable-check target does not include `event.c`.
- Decompressed the four packaged opening rooms with the current bounded decoder and inspected actual scenario bytes. Independently extracted RM_0000 and RM_0010 from the supplied ISO's `RDX_LNK.AFS` and confirmed byte-for-byte equality with those two packaged rooms. Section 7 records identities and offsets. This was not another 205-room scan.
- No new PSP, PPSSPP, Vita or PCSX2 gameplay run was performed. A 32-bit host link probe failed because this environment lacks 32-bit startup objects/libgcc. Existing 64-bit host parser tests remain useful, but are not original-ABI execution tests.

Assessment logs were written under `/tmp/recvx-assessment-*`; they are temporary diagnostics, not prerequisites for using this document.

## 2. Current architecture and choices

The current executable links original `flag.c`, `ps2_NaMem.c` and the extracted `system_init.c`. It also links PSP `legacy_runtime.c`, `legacy_platform.c` and `legacy_script.c`. The longer `LEGACY_PORTABLE_SOURCES` list in [Makefile](../Makefile) only produces syntax checks and standalone objects. It does not make `bhMainSequence`, original player control or the event scheduler part of the running EBOOT.

The prototype has substantial reusable work: bounded RDX expansion, owned asset readers, room/model/texture decoding, skinned Claire poses, authored motion-speed tracks, camera interpretation, static collision and stairs, door records, room actors and GU submission. However, [renderer.c](../src/psp/renderer.c) at 1140–1203 still advances the diagnostic player and chooses its camera; it is not a pure renderer. [main.c](../src/psp/main.c) at 237–402 constructs derived room data, copies events, applies limited script visibility and releases the source room. Diagnostic CROSS transitions bypass the original game/door state machines.

Newer work matters: `actors.c` and the armature fix extend the foundation beyond the old audit. `legacy_script.c` now handles opcode `0x24` with a visibility side table, and `0x2f` by advancing four bytes without implementing shadows. Thus the audit's old stop at `0x24` is historical. With fresh zero flags, source inspection predicts the next unsupported SCD0 boundary at `0x32` (object linking), offset `0x8e`; this prediction was not verified on PSP. Original `bhInitModelSet` also changes actor state flags, so the visibility side table is not equivalent engine behavior.

| Approach | Benefit | Risk / transition cost | Decision |
| --- | --- | --- | --- |
| Add original modules indefinitely to the current viewer loop | Small early diffs; preserves visible progress | Two player/camera/event authorities; incidental renderer timing becomes gameplay timing | Useful only as bounded test scaffolding |
| Import Vita-style compatibility and link everything immediately | Exposes dependencies; retains original control flow broadly | Large memory/backend burden, permissive stubs, PS2 graphics pipeline and difficult-to-localize failures | Use as reference, not first executable milestone |
| Incremental original engine with PSP service/render adapters | Original semantics, working presentation, inspectable milestones | Requires clear state ownership and deliberate extraction of tightly coupled functions | **Recommended** |
| Rewrite selected gameplay systems | Can avoid an awkward dependency | Recreates bugs and contracts; expanding independent engine | Limit adaptation to portable-C corrections and hardware/service boundaries |

The destination is one original engine loop above PSP backends. It need not retain the literal PS2 `njloop.c::main` or start all EE-compatible threads to achieve that. A PSP entry point can perform platform startup and invoke the original initialization/dispatch functions, replacing PS2 setup and buffer layout. Keep such changes small and documented against upstream.

## 3. Source-based comparison and reusable Vita work

### Startup, tasks and timing

Original [main.c][o-main] defines `SYS_WORK sysp`, `ROM_WORK romp`, `CAM_WORK cam`, `BH_PWORK ply`, `ene[128]`, `eff[512]` and `bhSysTaskJumpTab[23]`. `njUserMain` checks **live** `sys->tk_flg` and `sys->ts_flg` before each ascending slot, then calls `PS2_jikken` and `bhCheckSoftReset`. PSP `legacy_runtime.c` currently supplies only part of that global set. Linking original `main.c` without removing duplicate definitions will fail or encourage inconsistent ownership.

The PSP [game_tasks.c](../src/psp/game_tasks.c) dispatcher snapshots `active_mask & ~paused_mask` once. That differs from the original when a callback changes a later slot in the same frame. The engine path must use the original live-mask rule. Do not force bit 7 on as the viewer currently does. Slots 6/7/8 are pad/game/event; slot 20, `bhSysCallMonitor`, is also critical: it contains startup and room loading, not merely debug statistics. Original [system.c][o-system] at `bhFirstGameStart` and `bhSysCallMonitor` allocates object/item storage, initializes inventory/player, reads system/player/weapon data, expands the RDX, sets the room, uploads textures, finishes the room and finally initializes events.

Vita [CMakeLists.txt][v-build] globs game and CRI sources, excludes `gcc_wrapper.c`, the replaced `ps2_MovieFunc.c`, and the separately included `ps2_Vu1Scissor2.c`; renames `njloop.c`'s main to `recvx_game_main`. [platform/vita/main.c][v-main] starts it on a platform EE thread. Original `njUserMain` is retained with state-change logging. Its optional new-game hook in [system.c][v-system] sets `AdvWork` and follows the title's state path; it does not replace gameplay with a viewer. Reuse that diagnostic principle, not its exact task masks without their prerequisites.

Original [game.c][o-game] `bhMainSequence` orders enemy, player, effect, object/item, camera and light updates before final draw work and later cut/event work. Normal `bhSysCallGame` sets `loop_ct=1`; `bhSysCallEvent` runs events separately. When `loop_ct>1`, `bhMainSequence` services messages/events inside the loop and `bhSysCallEvent` returns early. Calling events once from each place unconditionally would double-run them.

Timing has two distinct domains. In [ps2_NaSystem.c][o-nasystem], `Ps2SwapDBuff` drains/submits, calls `EorFunc`, waits for two VBlanks, swaps, resets `Ps2_vcount`, then calls `VsyncFunc`. `vsync_func` increments `sys->time` under its own game flags on each interrupt. [sync.c][o-sync] `bhControlVSync` handles sound, other counters and `evt_tim -= 2`; it is a swap callback, not automatically a callback for every physical VBlank. `njWaitVSync` is empty in this backend. Names alone are misleading.

The dispatcher's `PS2_jikken` tail calls `Ps2SwapDBuff` in [ps2_dummy.c](../src/ps2/veronica/prog/ps2_dummy.c) at 230. Replace that presentation boundary deliberately: retaining it alongside an independent PSP present/wait path would duplicate pacing and callbacks.

Vita [kernel.c][v-kernel] at 106–138 preserves wakeups arriving before sleep with per-thread semaphores; at 213–228 its VBlank thread raises interrupt handlers independently of presentation. EE threads share one CPU affinity with priority mapping. This explains how the original busy-wait/pacing logic can progress. PSP should use blocking display waits/counted notifications and a single engine writer, avoiding busy spins. Do not invoke allocation, file I/O or GU work in a PSP interrupt callback. Import semaphore semantics only where actual retained callers need them; do not allocate Vita's 128 KiB minimum stack for every PSP service thread.

Initial policy: one normal original update per two display VBlanks, with physical VBlank counts and presentation callbacks tracked separately. Preserve original flags and pause gates; do not feed the original player arbitrary viewer `dt` or the viewer's four-tick/drop policy. Compare stalls and event skipping against PS2 traces before adding catch-up. Suspend must rebase wall-clock scheduling, not execute minutes of accumulated simulation.

### Events and state

Vita compiles the original [event.c][v-event], including all 256 dispatch entries and 16 `BH_SCEWORK` tasks. It does not replace scenarios with a new VM. Its changes here are predominantly declarations, defined returns and table formatting; the scheduler and condition machinery remain original. In particular, Vita does **not** solve every safety issue in the interpreter.

Original [event.c][o-event] locations:

| Functions / location | Contract that integration must preserve |
| --- | --- |
| `bhInitEvent`, 339 | Clears task status, sends sound commands 3/4, calls `bhControlEnemy`, installs room scenario offsets, sets `swork.pip`, runs SCD0, then runs one scheduler pass under `sp_flg & 0x10` |
| `bhControlEvent`, 379 | Evaluates SCD1 then schedules tasks; scenario scanning and event continuation are different operations |
| `bhIfelCk` / `bhElseCk` / `bhEndif`, 395 | Original `G_Sp` continuation arithmetic; not the PSP experiment's general stack-of-targets abstraction |
| `bhCk`, 443 | MSB-first flags; type 10 also checks `etc_idx`/`flr_idx` for particular conditions |
| `bhSet`, 554 | Flag banks plus indexed player work access for type 13; not just `plp->flg` |
| `bhSleep` / `bhSleeping`, 9100 | Resume inside an instruction: the byte after `0xf8` is `0xf9`, with a persistent counter and task PC |
| `bhEvtNext` / `bhEvtEnd`, 9196 / 9212 | Yield versus termination; `bhEnd` is not interchangeable with event termination |
| `Event_init` / `Event_exec`, 12956 / 13001 | Event ID indexes table entry `id+2`; task level >=16 searches slots, including the original slot-15 fallback |
| `bhEventScheduler2`, 13017 | Ascending 16 tasks, persistent `data`, loop/work/motion state; `Event_T_timer` increments once per scheduler call with active work, not once per task |
| `bhScenarioCheck`, 13068 | Adds a stored room-relative scenario offset to the event-section base |

`EVT_WORK` fields look like pointers but the on-disk table stores **offsets**. `bhSetRoom` establishes `rom->evtp` as the section base; it does not convert the first two table words into absolute pointers. `Event_init` uses subsequent words as offsets too. Double-relocating this table can compile and corrupt execution immediately. `BH_SCEWORK` is documented as `0x134` bytes and has a signed `char loop` initialized to `-1`; unsigned-char compilation corrupts its first loop operation. See upstream [types.h][o-types] at 968 and 1948 (973 and 1953 in the PSP copy).

Retire `legacy_script.c` from engine-mode execution. Keep it as a bounded diagnostic/disassembly aid, clearly labeled approximate; it loses execution state on return and its queue lacks the original continuation/work fields. Never run it and original SCD0 against the same live `sys`. There is no reason to develop a second gameplay interpreter.

### Rooms, files and memory

[room.c][o-room] `bhSetRoom` copies a 1,004-byte `ROM_WORK` disk prefix, relocates room pointers and nested cut pointers, realizes models in place, creates actor work, installs events and gathers texture work. `bhFinishRoom` completes motion, collision/player/camera-related setup. `binfunc.c` relocators mutate model/motion sources. Calling these on a second load of already-relocated bytes is invalid.

The current PSP tools package compressed RDX plus metadata and split player assets; runtime still builds much of the GU geometry/textures. Do not assume a complete draw-ready cooker exists. Initially keep one expanded RDX resident in engine mode, validate it first, and adapt the original room setup in stages. On PSP-2000+ this is a sensible simplification. The viewer may continue releasing its RDX after conversion because its derived objects have separate ownership.

Vita [cdvd.c][v-cdvd] implements ISO9660 lookup and sector/file reads, preserving original file identity and AFS callers. Reads finish synchronously; `sceCdSync` reports completion and errors are recorded separately. [mc.c][v-mc] performs host file operations immediately but retains one pending command/result until `sceMcSync` consumes it. [sif.c][v-sif] dispatches native RPC handlers and completion callbacks synchronously. These show that the *observable request/status contract* matters more than reproducing hardware latency. They do not establish that arbitrary success stubs are safe.

For PSP, add an original-identity asset service at `RequestReadInsideFile` / `GetReadFileStatus` / `GetInsideFileSize` and the relevant `gdlib`/`adxwrap` boundary. Preserve archive/entry/room-case IDs, sizes and statuses; use cooked files or original payloads behind a versioned manifest. Initially support the known SYSTEM/player/weapon/room/message requests only. Log and fail unsupported requests. A successful submission is not a successful read; short reads, cancellation and missing files must propagate. Synchronous reads are acceptable for early loading, with the same caller-visible terminal result. Async workers must publish completion to the engine thread without mutating `sys` themselves.

### Player and rendering

[player.c](../src/ps2/veronica/prog/player.c)::`bhInitPlayer` at 638 allocates model/motion/extra buffers well beyond the current 256 KiB legacy arena. `bhSetPlayer` installs model, collision and attachment state; `bhControlPlayer` at 1352 immediately needs `exp0`, model work matrices, motion tables and original mode dispatch. Zeroing a `BH_PWORK` and copying x/z into it is insufficient. `dread.c`, `binfunc.c`, `Motion.c`, `playpch.c`, `playpch2.c`, `pwksub.c`, `hitchk.c`, `hitchkl.c`, `rutchk.c` and `pl_evt.c` are a dependency cluster, not individually usable controllers.

Keep the extracted Claire assets, chunk decoding, texture upload and skinning machinery. The adapter must accept original `mtn_no`, `frm_no` (including fixed-point semantics), interpolation, root displacement, attachments and object transforms. The original motion system decides animation and gameplay frame attributes. The PSP presenter must not advance another animation clock or apply root translation twice. Model matrices are also gameplay inputs: rendering cannot become their only producer or stop updating them when an actor is culled.

The first rendering boundary should be **scene/model/actor/camera/2D submission**, above GS packets. Separate the existing renderer into diagnostic update and draw entry points. Derive a frame snapshot from `rom`, `cam`, `plp`, `ene`, `sys->obwp`/`itwp` with stable source indices, transforms, visibility, texture handles and motion pose. Keep masks keyed by original object IDs and original texture groups. Add adapters at `bhAllDrawModel` / `MdlPut` / used Ninja draw calls as their callers arrive, while preserving transform evaluation that occurs in those paths. Do not replace the entire Ninja API with no-ops.

Vita [gs_draw.c][v-gs] uses vitaGL framebuffers, shaders and a 16 MiB renderer allocation; `gs_gif.c`, `vif1.c`, `vu1_hle.c` interpret the original graphics stream. This is useful documentation of original render semantics, not the PSP architecture. Do not import it, the 10 MiB original texture buffer, packet arenas or EE address masking merely to make startup link. Preserve GU, cache writeback, swizzled textures and GE fences; retain a faithful camera composition option at 480×272.

### Input, UI and asynchronous media

Vita [pad.c][v-pad] creates active-low DualShock packets with axes and pressure bytes; native input then passes through original pad code. PSP already has normalized input and repeat logic. Choose one boundary: for integration, map PSP to the original peripheral input consumed by `pad.c::bhSetPad`, retaining original game mappings, held/pressed/released/repeat behavior. Do not run both repeat generators or equate PSP button bits with `sys->pad_*` bits. Sample/latch edges once per engine tick, including when multiple presents occur. Vita's touchscreen mapping is inapplicable to PSP; define reachable chords/context mappings for missing buttons before enabling affected actions.

Minimum playable services are readable original messages, yes/no choices, message completion, action/cancel and pause. Use original `message.c` state and encoded strings through a PSP glyph/sprite backend. UI rendering may be simple initially, but button outcomes and state must be original. The item pickup target also requires the original insertion/quantity logic and enough of `sub1.c`'s pickup flow; a full inventory/map/item-box frontend can follow.

Vita [movie.c][v-movie] is deliberately a placeholder: it exposes PLAYING first, then PLAYEND on a later server call, because the original front end must observe both. Copying this as production playback would skip story content and distort timing. Use it to understand the state contract. Strict integration mode must stop visibly on unavailable movies. An explicitly labeled diagnostic skip may implement the full start/playing/end handshake, but its run cannot pass a story-progression gate.

Vita [tsnddrv.c][v-sound] is more substantial: bank HD checksums, sample/sequence storage, channel busy bits, finite versus looping effects and status replies let the original sound state machines advance. `hseq.c`, `hsyn.c` and `cri_iop.c` are useful format/command references. Its 2 MiB emulated IOP address array and permanently retained banks are not required PSP allocations.

Implement service states `idle → requested → preparing → active → completed`, plus `failed/cancelled`, translated into each original API's actual status values. Preserve request identity, elapsed time, loops, pause and one-time completion. A missing one-shot must not become an eternal busy channel; a missing voice/movie must not silently finish. For diagnostic tests, a declared duration-driven silent service may exercise scheduling, but record every substitution and exclude it from audiovisual acceptance. For unavailable required services, latch a fault and halt the engine test with room/task/opcode/request context; keep the diagnostic UI alive. Do not spin forever or invent a normal game result.

Follow the audit's developer notes: convert user-supplied ADX/PSS media offline. Prototype one voice and one movie with PSP-supported playback, then preserve source sample counts/timestamps/loop points in the manifest. Verify the actual PSP decoder's container/profile/channel limits and suspend/skip lifecycle on hardware before mass conversion. `sceSas` is a sample synthesis/mixing backend, not an automatic MIDI interpreter; preserve/adapt sequence scheduling and banks or bake genuinely fixed tracks offline. Dynamic effects still require pitch/pan/priority behavior.

### ABI and source portability gates

Vita [recvx_prelude.h][v-prelude] fixes EE 64-bit aliases, uses a 16-byte quadword, forces signed char and preserves full native pointers instead of EE physical masks. [ee_asm.h][v-asm] and translated math/matrix functions provide scalar EE/VU semantics, including register state shared across calls. They explicitly do not fully model VU floating-point clamping. Use selected scalar translations as reference implementations and test them; Allegrex being MIPS does not make EE MMI/VU assembly valid on PSP. Replace hot math with VFPU only after comparison and profiling.

Important hazards to address as their types enter the executable:

- PSP `legacy_types.h` currently declares `u_long` as **32-bit** and `u_long128` as a four-word struct without explicit 16-byte alignment. This was sufficient for the initial slice, not evidence of EE ABI compatibility. Vita's corresponding `u_long` is 64-bit. Use namespaced fixed-width EE aliases; do not conflict with newlib's `u_long` or globally change disk pointer widths.
- Assert PSP `sizeof`/`offsetof` for native `SYS_WORK`, `BH_PWORK`, `ROM_WORK`, `EVT_WORK`, `BH_SCEWORK`, actor/model/motion records, save prefix, and packed mode bytes. Existing PSP disk-record assertions do not prove all native engine layouts. The literal 175,260-byte clear in `bhInitSystem`, 1,004-byte room copy, 39,936-byte object allocations and packed `*(int*)&mode0` writes are especially dangerous if layouts drift.
- Compile original logic with explicit signed-char policy. Test negative floor IDs, sentinel values and task `loop=-1`. Audit signed shifts and bit-31 flag operations; `-fwrapv` does not define every invalid shift or pointer operation.
- Keep disk offsets distinct from pointers. Execute pointer-bearing original tests on PSP or a proper 32-bit host configuration; do not cast 64-bit host addresses to `int`. A host offset-reader test is a different test.
- Add missing prototypes and correct incompatible calls/returns rather than suppressing diagnostics. Vita's defined-return patches are candidates for review, not proof that zero is correct for every caller.
- Vita builds with `-fno-strict-aliasing`, `-fwrapv`, signed char, several disabled UB optimizations and automatic zero initialization. Its `writable_rodata.sh` moves all read-only data into writable sections. Do not copy this policy wholesale: scope transitional flags to imported logic, locate writes/uninitialized reads, fix individual objects and retain reproducible traces. Const-data relocation and global zero-init can conceal real defects.
- Old anonymous union/struct casts, model source mutation, DMA/uncached addresses and SSE/NEON/vector assumptions require review even if compilation succeeds. Never pass a PS2 physical/scratchpad address through to GU or PSP memory APIs.

## 4. State authority and backend contracts

Maintain two explicitly separate launch modes with a cold runtime reset when switching:

| State | Viewer mode | Engine mode |
| --- | --- | --- |
| Progression, inventory, flags | Diagnostic sandbox only | One `SYS_WORK`; original logic is the writer |
| Room identity and lifetime | Viewer catalog/owned derived assets | `sys` room fields plus one owned room and `ROM_WORK`; catalog only resolves assets |
| Player | `RecvxPlayer` | One `BH_PWORK` plus original extra/motion work; render snapshots are read-only |
| Camera | Diagnostic free/fixed camera | Original `CAM_WORK` and cut state; optional debug inspection cannot feed game logic |
| Actors / collision | Derived viewer copies | Original work/ATR arrays; render caches derive visibility/transforms and observe mutations |
| Events | Approximate interpreter display | Original `bhEtask`, PCs, counters, condition state and flags |
| Scheduling | Viewer dispatcher | Original live task masks and ordering; one clock owner |
| Transitions | Diagnostic room reload | Original door/system state machines and resource transaction |

**First authority transfer:** in package 1, original `Event_exec`/`bhEventScheduler2` exclusively own a test runtime's event tasks and key flags. Do not mirror them into `RecvxLegacyEventTask`. That establishes a real original-state island without changing viewer behavior. Later engine mode expands that same authority; it must never combine viewer movement writes with original player/event writes.

Suggested interfaces are conceptual, not mandatory public APIs:

- `EngineRoomOwner`: validated source allocation, relocation state, generation, native work and derived GPU handles; exactly one release path.
- `EngineFrameView`: borrowed immutable render snapshot, resource generation and pose references; GU cannot retain them past the declared fence.
- `PlatformRequest`: original identity, operation, state, error and completion generation; workers cannot write gameplay state.
- `EngineFault`: first failure with source caller, room/case, scenario/event ID, task, PC offset and service. Stop before unsupported side effects and preserve the last valid trace.

Collision must consult current `rom->walp/etcp/flrp` and dynamic lists, or a cache invalidated by original mutations. The current load-time copies would miss `bhWalAtariSet` and related commands. Visibility must derive from original actor flags, not a parallel override table.

## 5. Dependency map and ordering hazards

```text
PSP entry + ABI/layout checks + owned allocators + fault/trace service
  ├─ isolated original event core → real event-38 execution
  └─ file identity/status + player/system assets + resident validated RDX
       → original room/player/object initialization + model/motion realization
       → original live task dispatcher + pad + clocks
       → original player/collision/motion + camera → GU frame adapter
       → SCD0 initialization → SCD1 + persistent event tasks
       → messages + minimum item acquisition → persistent pickup
       → original door/system loading → leave/re-enter with persistence
       → full new-game opening, broader actors, inventory, combat and saves
```

This is integration order, not a strict one-way call graph. Several cycles must be broken at explicit test boundaries:

1. `bhInitEvent` calls enemies and actor-affecting SCD0 before its scheduler pass. Package 1 deliberately invokes the original core directly; it does not pretend to run full `bhInitEvent`. Later actor work must exist before room SCD0 runs.
2. Room realization needs model/texture services, while rendering needs room/player work. Build GPU assets before exposing a new room; keep gameplay matrices available without drawing. Offline/PSP asset decoders are backends, not replacement spawn rules.
3. `bhSysCallMonitor` waits on file/audio/texture status while game/event tasks are masked. A scheduler that freezes all services during a game pause/load will deadlock. Service pumping and diagnostic fault handling must remain runnable.
4. Original player motion reads matrices and frame attributes; collision can depend on scripted room changes. Port movement, motion decisions and interaction state together in a restricted supported room, rather than feeding original scripts a viewer position mirror.
5. `bhMainSequence`, slot 8 and VBlank callbacks each do different work. An added outer fixed-step loop can double events, timers or pad edges.
6. Door setup updates `sys->stg_no/rom_no` before the target is ready. Keep the source owner and a transition record until completion; no task may interpret new room IDs with stale room pointers. A failed load stays in an explicit fault/retry state, not partly resumed gameplay.

## 6. Memory policy for PSP-2000+

The old audit's PSP-1000-first baseline is superseded. Retaining the expanded RDX and original work buffers is allowed during integration. A compact gameplay-only room package, aggressive texture reduction and arena overlaying are no longer prerequisites for original events. The largest measured RDX in the old audit was about 8.52 MiB; that remains useful planning data, not a fresh full-engine budget.

### Build and runtime configuration

Use a named `engine-64` development profile, distinct output directory and explicit memory settings. Preserve a viewer/PSP-1000 diagnostic profile. No production flags were changed during this assessment.

1. Keep user-mode PRX (`BUILD_PRX=1`, current module attributes) and `PSP_FW_VERSION=600`. Set **`PSP_LARGE_MEMORY := 1` explicitly** for the initial engine profile. Both inspected build.mak versions produce `MEMSIZE=1`; the installed SDK defaults to 1 above firmware 3.90 when unset, while freshly fetched SDK defaults to 2. Explicitness avoids that version-dependent default. Do not select mode 2 without investigating its loader-specific meaning. See [PSPSDK build.mak][sdk-build].
2. Replace the engine profile's fixed `PSP_HEAP_SIZE_KB(18 * 1024)` with `PSP_HEAP_SIZE_KB(-1)` and **`PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024)`** as an initial measured reserve policy. This proposes an elastic heap from the user partition; 4 MiB is an engineering starting point for stacks/modules/media, not a proven required reserve.
3. The current `_sbrk` uses the largest available contiguous user block minus the separate threshold when the configured size is negative. The negative magnitude is **not** the reserve in this implementation. This was checked in [libcglue/glue.c][sdk-glue] at 687 and in the installed library's MIPS relocations/disassembly. Do not rely on an old `PSP_HEAP_SIZE_KB(-4096)` convention alone. Pin SDK/libc versions and test allocation failures.
4. Force regeneration of `PARAM.SFO`, PRX and PBP. Parse the resulting SFO, including the PBP's embedded copy, and inspect the ELF heap/threshold symbols. A make command-line variable may not invalidate an existing SFO; separate profile outputs/config dependencies prevent stale packaging.
5. Boot from the rebuilt **PBP** under the selected 64 MiB PPSSPP model and supported CFW on real PSP-2000+. Record model, firmware/CFW, SDK revision, module sizes, kernel total/largest free block and allocator budget before/after initialization. Test a temporary allocation spanning beyond the old 18 MiB heap, touch it, then free it. An ELF-only emulator boot is insufficient proof of the SFO request.
6. Detect insufficient available memory early and keep the diagnostic path usable. Do not infer a 64 MiB application heap from 64 MiB physical RAM. Neither extra model-specific VRAM nor Media Engine memory is part of this malloc budget.

The threshold reserves memory outside the libc heap; it is different from free space *inside* the heap. Existing HUD fields need their units/allocator clarified. Report kernel free/largest, libc arena/free, original arena used/high-water and allocation-tag totals separately; do not add overlapping figures.

### Lifetimes to establish now

| Allocation class | Owner and release boundary |
| --- | --- |
| Global engine/save/options | Engine runtime; survives rooms; reset only by original new-game/load/title lifecycle |
| Player/costume/weapon | Player resource set; survives room change unless the original requests a swap |
| Room RDX + native arrays/actors | Room owner; resident while any original pointers refer into it; reconstructible from source and persistent flags |
| Event PCs, stacks and work pointers | Room generation; cancel/clear before freeing room/actor work, retaining only original global flags outside it |
| GU meshes/textures/poses | Resource owner; retire after GE has finished referencing them |
| Streaming/audio/movie buffers | Request/service owner; bounded ring buffers; cancel/join before reclaiming source memory |
| Decode/build scratch | Load transaction; released promptly, never retained by engine/model pointers |
| Per-frame transient work | Frame arena/list; reuse after its fence, no per-tick heap churn |

Respect `keepmem`, `mempb`, `memp`, `endp` and original reset points while replacing opaque fixed-address partitioning with named allocations. Before converting any in-arena pointer to an independent allocation, identify all rewind/copy assumptions. Do not replace the 256 KiB test arena with an arbitrary permanent 40 MiB arena plus duplicate malloc resources. Size it from the actual retained initialization path and enforce bounds at every allocation, including callers that currently assume success.

Instrument requested and actual bytes, alignment, owner, generation, live/peak counts, failures, largest free block, stack margin and GE list high-water. Sample allocation-level peaks during compressed+expanded overlap, geometry/texture conversion, actor creation, streaming and transitions. Stage-boundary HUD samples miss these peaks. Run repeated reconstruction and injected-failure tests, then hardware transition soaks.

PSP-1000 blockers are likely resident RDX plus duplicated GU data, original static actor/effect/work pools, player source+native+posed representations, transition overlap, audio banks/rings, executable growth and fragmentation. Resolve later with chunked cooked assets, reclaimable graphics source sections, resource caching with explicit eviction, measured pool sizes and bounded staging. Do not reduce semantic limits such as actor IDs or event slots merely to fit. Keep handles/owners independent of physical placement so reducing residency does not require changing gameplay logic.

Follow the [required PSP optimisation guide][psp-guide], read in full for this assessment: retain swizzled compact textures and explicit CPU/GE ownership; profile before VFPU/backend changes. VFPU optimization, double-buffered command lists and custom ME/VME work are deferred until measured gameplay justifies them. Extra-memory development does not relax data lifetime correctness.

## 7. Concrete original-data targets

These observations come from decompression of the local packaged rooms, with RM_0000/RM_0010 independently checked against the supplied ISO, not a complete scenario disassembler. Offsets within scripts are byte offsets. Reproduce extraction using `tools/psp_afs.py`, the room-name table in `ps2_dvd_image.c`, `recvxExpandBounded` and `recvxEventDataScript`; `tools/inspect_psp_start_route.py` demonstrates the archive lookup. Preserve hashes/identities in local fixture manifests and extract from the user's disc; do not commit game payloads.

### First core proof: RM_0000 event 38

| Field | Observed value |
| --- | --- |
| Expanded RDX SHA-256 | `a8fd928f2956ca2eadcb224ccb8120aa3a0867b6c527b5de268182acc15dbf0c` |
| Event section | RDX offset `0x18bec0`, 8,248 bytes, 39 event bodies plus SCD0/SCD1 |
| SCD0 | Section offset `0xa4`; starts with a condition on event flag 1, sets that flag and requests task 7/event 38 |
| Event 38 | Table index 40, section offset `0x1ed8`, absolute RDX offset `0x18dd98`, 352-byte table span |
| Event-span SHA-256 | `e6e8d61eba16c34eca97697e426a9719b329ff2cf5cbe8d8d06de8d5242f56b8` |
| Actual instructions | 44 six-byte `bhSet` commands for flag type 2, then `bhEvtEnd` at body offset `0x108`; remaining bytes are padding |

For a zero-initialized `ky_flg[16]`, the independently decoded expected words are:

```text
7b73cdf4 4b7e06ff 7b4f7f3f f1e74000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
```

This oracle was computed from the actual operands and original `bhSet` semantics; it is not a fresh original-engine runtime trace. The proof must execute the original handlers and compare all 16 words, task termination and PC. With initial timer zero and one scheduled task, expect `Event_T_timer=1`, task 7 inactive and PC at `0x10a`; another scheduler call with no active tasks must not increment the timer. This covers genuine new-game state initialization without needing enemies, inventory UI, audio or rendering. It does **not** authorize skipping the rest of SCD0 and calling the opening initialized.

### First playable progression: RM_0010 item 0

Choose the item at placement index 0, item ID `0x1f`, ETC index 2, persistent item flag `0x7b` (123). Its ETC record has type 4 and attribute 0, avoiding the special pickup-animation branch used by item 1. Identify it by original ID until the extracted message/item names are verified; do not invent a PSP-specific item or script.

Evidence from the room:

- Expanded SHA-256: `d1a55687952984044e7b6da6c89a37dc00de2d6b849cfa29a3a90c10442e43f0`.
- Event section at `0x3eec0`, 416 bytes; SHA-256 `ce5b44d3ef64dbf9a2996577885fd5adb87d1a81238b669a8b286e45b183da7d`.
- SCD0 offset `0x0c`, length 56: `bhItmSetCk` at script offset `0x0c` names item 0/flag 123/ETC 2. This reconstructs visibility/trigger state from persistent flags.
- SCD1 offset `0x44`, length 284: starts with `bhItmCk` for flag 123/ETC 2. `hitchk.c::bhCheckExmAtari`, type 4, sets `sys->sb_id` and acquisition request; `sub1.c::GetItem` performs original capacity/quantity/choice logic; `bhItmCk` consumes success (`cb_flg & 0x800`), disables the trigger, hides the item, sets `it_flg` and clears `ic_flg`.
- SCD1 also queues event 0. Its 64-byte span uses actor work/motion and frame waits before setting room flag 0. The room has actor ID 67 (`0x43`), which maps to `bhSubpl` in `eneset.c`, rather than a combat enemy family. It cannot simply be omitted: implement the exercised scripted actor/motion path or fail the room gate.
- There is an original SCD1 door command to stage 0/room 2/spawn 0/type 1 at script offset `0x40`, conditional on map/item/interaction state. This is a later transition target, not permission to bypass the cemetery's own scenario initialization.

Implementation anchors: [hitchk.c](../src/ps2/veronica/prog/hitchk.c)::`bhCheckExmAtari` at 4590, type-4 branch at 4848; [sub1.c](../src/ps2/veronica/prog/sub1.c)::`ItemTaskCheck` at 3025, `StatusMain` at 3242 and `GetItem` at 3727; [event.c][o-event]::`bhItmCk` at 2090 and `bhItmSetCk` at 2747; [system.c](../src/ps2/veronica/prog/system.c)::`bhSysCallItemselect` at 838. Follow the original slot-9 acquisition path, including its pause masks and message result, rather than calling item insertion directly from PSP input.

Start the pickup test from a documented original post-cell checkpoint, captured or reconstructed from the original new-game path with a reviewed state trace. A test checkpoint is acceptable; claiming a full new game by fabricating flags is not. During initial component tests, load the real room and fixture state explicitly and label it an integration checkpoint.

Acceptance sequence: original action input at ETC 2 → original acquisition prompt → cancel leaves inventory/flag/item intact → accept inserts the correct original item/count → SCD1 sets flag 123 and removes it → unload/reconstruct the same room through engine ownership → SCD0 keeps it absent, inventory unchanged. Test full inventory as well. This proves actual original progression without first implementing every inventory screen, combat AI or multimedia scene. Door travel and return extend the test once both room endpoints are supported.

Do not choose the RM_0000 lighter/cell-release cutscene as the first tiny package. Its SCD0 already links actors, lights and motion; SCD1 branches on flags 96–99 and launches multiple events with voices, cameras and actor control. The original sequence is a valuable next story gate, but event 38 alone does not implement it. RM_0030 is similarly a poor first target because it launches a group of actor events and depends on prior progression state.

## 8. Ordered work packages for Luna

Every package keeps the viewer bootable, uses an explicit source list, and records unsupported operations. Host tests validate portable data/contracts; PPSSPP validates the linked PSP slice; real PSP validates memory, cache/GE behavior and timing. Do not broaden art coverage while an earlier state/lifetime gate is failing.

### Package 1 — Execute original event 38, independently of the viewer

**Result:** an opt-in `engine-event-test` target/mode executes the actual event body using original task state, prints a deterministic pass/fault trace and exits or returns to diagnostics. This is the immediate handoff.

**Touch:** `Makefile`, `src/ps2/veronica/prog/event.c`, `event.h`, native types as needed, `src/psp/legacy_runtime.c` or a new exclusive engine runtime, `event_data.c`, and new test/trace code. Suggested files are `event_core.c` for mechanically extracted original core and `src/psp/engine_event_test.c` for the harness; Luna may choose equivalent maintainable boundaries.

**Implementation:**

1. Record the existing player-test failure; investigate it in a separate focused change before gameplay packages. It need not block an independent VM proof. Do not mix a collision rewrite into this package.
2. Extract the smallest shared original core: original event globals, `Event_init`, `Event_exec`, `bhEventScheduler2`, `bhSet`, `bhEvtEnd`. Keep one implementation shared with the eventual full engine, with no duplicate bodies in `legacy_script.c`. Preserve the original full table mapping in the normal build; give the restricted test build a clearly declared supported-command table with all other entries faulting. A single authoritative opcode mapping avoids future drift. The full 256-entry table otherwise retains every handler and its dependency tree; linker garbage collection alone does not solve that.
3. Add only required portability corrections: proper headers/prototypes/returns, signed char, layout assertions, and bounded dispatch validation. Keep original scheduling/flag behavior. Guard PC, operand extent, event ID, task/work indices and an instruction watchdog. A fault must escape all nested dispatch/condition loops, not merely return the ordinary false/yield result. Do not skip over an unknown instruction.
4. Allocate one isolated `SYS_WORK`, `ROM_WORK` and original task set through a test runtime that cannot coexist with the viewer's global writer. Own the validated event bytes for the entire run. Set `rom->evtp` to their base with offset words unchanged. Initialize globals explicitly, set `sp_flg & 0x10`, call `Event_exec(7, 38)` then the original scheduler. Do **not** call incomplete `bhInitEvent` or fake its enemy/audio dependencies.
5. Extract the fixture locally using the existing bounded RDX/event readers; validate section/span/hash against section 7 and compare every key-flag word, PC, task status and timer. Trace offsets, not raw pointers, so results compare across runs.
6. Add the original sleep/yield/end handlers to this test slice and a small synthetic multi-task fixture: `f8 f9 03 00`, then a flag write and event end. Check the persistent PC at the embedded sleeping opcode, resumption over successive scheduler calls, ascending task order and no timer increment without active work. Synthetic fixtures verify contracts; event 38 supplies the original-data proof.
7. Add negative tests for truncated operands, out-of-section offsets, unsupported opcode, disabled scheduling and attempted unload with active tasks. Preserve the faulting PC and confirm no later flag writes. A low-level harness can test malformed data before touching game globals.

**Dependencies:** no renderer, original player, audio, title screen or large heap required. Run the native VM harness on PSP/PPSSPP; use a properly configured 32-bit host harness if available. Do not force existing 64-bit parser tests through truncated native pointers.

**Acceptance:** linked map contains the original scheduler/handlers; actual event 38 matches the oracle; sleep/yield tests pass; unsupported work terminates diagnostically; default viewer still builds; no game payload is committed. Store an opcode/task trace from PPSSPP and a PSP smoke run when hardware is available, separately identifying unrun checks.

**Stop condition:** disagreement in flags, timer, PC, layout or lifetime; accidental linkage to the experimental interpreter; or a success stub required to make this narrow test pass. Do not expand its supported opcode set speculatively.

### Package 2 — Establish engine room ownership and the 64 MiB profile

**Result:** engine-test mode owns a validated, resident room and the original work records needed by the next scenario test; repeated construction/release has stable memory use.

**Touch:** build profile, PSP startup/allocator telemetry, `room.c`, `binfunc.c`, `dread.c`, existing room/model/player readers and new `EngineRoomOwner` adapter.

**Approach:** implement section 6 configuration; expand ABI assertions before using native relocators. Construct GU assets from validated disk records before in-place relocation, or give adapters an explicit relocated representation; never feed relocated bytes back into offset readers. Preserve original placement IDs and actor work indices. This package ends at validated room ownership and the bounded native metadata/event view; it does not claim complete original room initialization. Defer the full `bhSetRoom`/`bhFinishRoom` call path until package 4 supplies player/actor/model prerequisites. Do not call those functions against null model services just to close this package.

**Dependencies:** package 1 fault/trace behavior; actual SYSTEM/player/weapon/message asset identities for later initialization. Enumerate unresolved symbols for this new slice rather than blanket-stubbing SDK APIs.

**Acceptance:** load/unload RM_0010 repeatedly; validate pointer ranges/generation and model/actor counts; inject allocation/read failures at each stage; confirm all cleanup paths; inspect SFO and measure real PSP-2000+ heap. The 18 MiB viewer remains independently buildable. Stage one room at a time initially; preserve failure recovery without requiring two complete resident rooms.

**Stop condition:** double relocation, stale event/model pointer, allocator rewind crossing live player data, GE reference to retired data, or apparent memory success proven only in an emulator. Hardware memory approval can remain explicitly pending while independent host work proceeds.

### Package 3 — Make drawing passive; run original tasks, pad and clocks

**Result:** engine-test mode has one live-mask dispatcher, one simulation clock and a GU frame adapter that cannot change game state.

**Touch:** PSP main/renderer/game tasks/input/timing; original `main.c::njUserMain`, selected `system.c` callbacks, `sync.c`, `pad.c`, camera/Ninja adapters.

**Approach:** split viewer simulation from submission without changing its behavior. Move original globals to one owner. Retain original dispatcher/table or mechanically share its small loop; unimplemented runnable slots fault by name. Connect initialized callbacks incrementally. Keep service pumping outside gameplay pause. Initially render a stationary test snapshot while validating the dispatcher with pad/event-core callbacks; keep unsupported gameplay callbacks inactive in this explicitly declared harness. Package 4 supplies the initialized original player and enables `bhSysCallGame`/`bhMainSequence`. Map original matrices/cuts/projection to GU as their producers become available. Use the original callback phases from section 3, without copying PS2 spin waits or packet allocation.

**Dependencies:** owned work/room resources; real pad input contract; a separate initialization fixture until monitor startup is ready.

**Acceptance:** tests for a slot enabling/disabling a later slot in the same dispatch, pause masks and no forced game bit; deterministic tick/edge/counter trace at normal rate and after a stall; engine snapshots unchanged after drawing; renderer continues while the engine fault screen is active. Compare elapsed VBlanks and event clocks with a PCSX2 reference, then check real PSP pacing.

**Stop condition:** rendering still advances position, animation or camera; `bhControlEvent` runs twice per tick; callbacks use freed resources; a paused load cannot complete.

### Package 4 — Original Claire control and the required scripted actor

**Result:** original Claire movement, interactions, motion decisions and camera work run in RM_0010, and its actor-67/event-0 path can complete.

**Touch:** original player/motion/collision cluster, `subpl.c`, required object functions, `room.c` finishing, camera/cut, PSP pose/scene adapters. Extend the original event core with the actual SCD0/SCD1/event-0 handlers.

**Approach:** initialize original player extra/model/motion work through reviewed `bhInitPlayer`/`bhReadPlayerData`/`bhSetPlayer` paths. Integrate idle/turn/walk/run/reverse first using real assets and matrices. Then enable `bhCheckExmAtari` and original scripted actor work/motion. Add room initialization handlers with real state mutations. Admit unsupported actor/effect paths only as explicit faults, not hidden empty actors. The existing visual pose backend may be used while original motion controls it.

Implement this as three reviewable increments: 4a stationary original player/model/room initialization; 4b original locomotion, collision and camera under the game callback; 4c the room's scripted actor and complete exercised event path. Keep later callbacks disabled in the declared harness until their prerequisites pass; do not report earlier increments as a playable initialized room.

**Dependencies:** packages 2–3; resolve the baseline collision assertion; scalar math/matrix services; room work fully initialized before `bhInitEvent`.

**Acceptance:** repeated recorded-input traces compare position, floor, angle, motion/frame, cuts and interaction indices with PS2; verify dynamic wall changes affect collision; event 0 yields/resumes and changes its room flag; no independent `RecvxPlayer` integration in engine mode. Validate visible Claire skinning on PPSSPP and PSP; CPU pose correctness must survive skipped rendering.

**Stop condition:** null model/extra pointers, different movement root applied twice, stale copied collision, wrong fixed-point frames, or a never-finishing actor frame wait. If this cluster is too large for one change, split stationary initialization, locomotion and scripted actor into separate PRs with these gates.

### Package 5 — First authentic playable progression: pickup and persistence

**Result:** the RM_0010 item-0 sequence from section 7 works through original logic, including cancel/full-inventory behavior and persistent absence after reconstruction.

**Touch:** full supported room scenario path, `message.c`, `sub1.c::ItemTaskCheck/StatusMain/GetItem`, `item.c` data, original object/item work and a minimal PSP 2D frontend.

**Approach:** preserve `swork.pip`/`sys->itm` ownership, item quantities and original completion flags. Implement only exercised acquisition/UI services initially, extracting shared logic if necessary rather than creating another inventory implementation. Use original text resources and decision handling. Run SCD0 once per initialized room, SCD1 and tasks at their original cadence. Record any presentation omissions; required waits must use real completion.

**Dependencies:** original action/facing/floor detection, room event 0, original message state and enough sound-service contract to avoid blocking. This target requires a small original inventory slice, not complete equipment/combination/item-box/map screens.

**Acceptance:** all section 7 steps pass under original scenario bytes; item 0 disappears only after successful acquisition; cancel/full inventory cannot lose or duplicate it; reconstruct the room twice and verify flag 123, inventory and trigger state. Compare against a matching original checkpoint and input sequence. This is the first playable progression gate; the event-38 proof alone cannot close it.

**Stop condition:** injecting `cb_flg & 0x800` from PSP code to simulate success, hand-setting item flags, fabricated messages/conditions or silently skipped actor/event work. Re-evaluate the target only if measured dependencies are materially larger, retaining a real-data acceptance sequence.

### Package 6 — Original door transaction and monitor-driven room loading

**Result:** a supported original door request loads the correct room/case/spawn, releases old resources safely and preserves progression on return.

**Touch:** `bhSetDoorDemo`, `bhStartDoorDemo`, `bhSysCallDoordemo`, `bhSysCallMonitor`, file-request service and room/resource owners; minimum original door presentation/backend.

**Approach:** route original `sys->door` into the load transaction; retain task masks, sound/load waits, fades and resume state. Replace the viewer CROSS reload entirely in engine mode. Integrate monitor cases as a sequence of small changes: asset request/result, expansion/room setup, textures/finish, events, then resume. Choose endpoints whose scenario dependencies already pass; RM_0010→RM_0020 specifically requires the cemetery actor/event slice before accepting a full traversal. A diagnostic door-animation skip must be labeled and cannot count as final door parity.

**Dependencies:** packages 2–5; completion/error states for file/audio/door work; both room admission checks.

**Acceptance:** leave/re-enter with the pickup absent, correct spawn/facing and persistent flags; wrong-facing/inactive triggers do not fire; missing target asset and mid-load failure leave an operable fault/retry path; repeated transitions have bounded memory and no old task/work references. PSP hardware must verify GE drain/cache behavior and load-time peaks.

**Stop condition:** new room IDs exposed to old-room gameplay, leaked pending requests, flags reset on reload, or transition success before target SCD0/required services finish.

### Package 7 — Restore startup and broaden one original story sequence at a time

**Result:** original title/new-game path reaches the supported opening sequence without checkpoint injection, then expands toward a complete chapter.

**Touch / dependencies:** original `adv.c`, startup callbacks in `system.c`, relevant backup/menu interfaces, `message.c`, required `subpl`/event/camera functions and PSP 2D/media services. Requires the earlier state, player, acquisition and transition gates; new-game persistence must use the same `SYS_WORK`, not a second frontend state model.

**Approach:** integrate warning/title/opening/monitor states and required message/font/system resources, then the RM_0000 lighter/cell scene using its actual event graph. Start offline voice/movie playback feasibility before this package, so media behavior is established when needed. Add only exercised actors, effects, cameras and inventory services; keep the coverage ledger tied to rooms/cases and event IDs. Next gates are combat, save/cold-load, death/retry and full inventory, followed by broader chapter coverage and PSP-1000 residency work.

**Acceptance:** original new-game initialization produces the same checkpoint state used earlier; authentic cell progression and route traversal work; unavailable media cannot masquerade as a successful playthrough. Hardware tests cover audio/video timing, skip, suspend/resume and long sessions.

**Stop condition:** the new-game path only works because debug flags/skip files are present, or service stubs report success for missing gameplay. Full-game performance optimization follows representative playable workloads.

## 9. Significant decision gates

| Question that can change the next work | Targeted experiment and decision |
| --- | --- |
| Can the original event core be isolated without dragging in every opcode service? | Package 1's linked map and event-38/sleep traces; extract shared core/table boundaries if necessary, not a new interpreter |
| Are native layouts and signedness correct on the selected PSP toolchain? | Compile/run layout manifest on PSP; compare disk-facing offsets and original debug metadata; stop room relocation at the first mismatch |
| Does the 64 MiB profile actually expand the usable partition/heap on supported CFW? | SFO inspection plus touched allocations and kernel/libc measurements on PSP-2000+; revise reserve/profile if needed, not engine semantics |
| Is RM_0010's actor-67 motion path a small enough first-room dependency? | Trace its SCD0/SCD1/event 0 and original `bhSubpl` calls from the chosen PS2 checkpoint; enumerate required assets/handlers before implementing pickup UI |
| How much gameplay matrix work is embedded in draw paths? | Run stationary/player traces with GU submission disabled; compare `mlwP->owP`, root/foot/link positions; move required evaluation into a shared original pose phase |
| Do timing and event skip match the original under missed frames? | Record task mask, `loop_ct`, VBlank count, `sys->time`, `evt_tim`, `Event_T_timer` and PCs for normal/pause/stall/skip runs on PS2 and PSP |
| Can the offline media choices preserve waiting semantics within memory/CPU limits? | One actual voice and movie under room load, including pause/skip/error, with source-duration metadata and hardware memory/underrun measurements |

The baseline player-test failure is an immediate regression investigation, not an architectural reason to retain the debug controller. The Vita fork's progress is source-level guidance here; untested fixes must earn acceptance on PSP. If code is adapted, preserve attribution/license notices and record the specific upstream revision and local changes.

## 10. Handoff recommendation

**Luna should begin with package 1: link a shared slice of the original event core and run RM_0000 event 38 in an isolated PSP test mode, checking all 44 writes, original task termination and persistent sleep/yield semantics.** Keep the walking loader unchanged. Follow with owned room/large-memory setup, then passive GU presentation plus original task/input/timing integration. These steps establish original-engine authority before expanding into the RM_0010 pickup and room-transition milestones.

[v-readme]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/README.md
[v-build]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/CMakeLists.txt
[v-main]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/vita/main.c#L20
[v-system]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/src/ps2/veronica/prog/system.c#L362
[v-kernel]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/ps2compat/src/kernel.c
[v-event]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/src/ps2/veronica/prog/event.c
[v-cdvd]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/ps2compat/src/cdvd.c
[v-mc]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/ps2compat/src/mc.c#L22
[v-sif]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/iop/sif.c
[v-gs]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/gs/gs_draw.c
[v-pad]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/ps2compat/src/pad.c#L150
[v-movie]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/vita/movie.c
[v-sound]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/iop/tsnddrv.c
[v-prelude]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/ps2compat/include/recvx_prelude.h
[v-asm]: https://github.com/Rinnegatamante/recvx-vita/blob/064aeb04d9bc6142bafea3e55a707935c1c2efb3/platform/ps2compat/include/ee_asm.h
[o-main]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/main.c
[o-system]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/system.c
[o-game]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/game.c#L20
[o-nasystem]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/ps2_NaSystem.c#L65
[o-sync]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/sync.c#L27
[o-event]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/event.c
[o-room]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/src/ps2/veronica/prog/room.c#L47
[o-types]: https://github.com/AshfordFamily/recvx-decomp/blob/4b2cab1aaf383841c912d81f946601fada9eb001/include/ps2/veronica/prog/types.h
[sdk-build]: https://github.com/pspdev/pspsdk/blob/e2b2b313d4175823a263c2c6cf175311e824eb77/src/base/build.mak#L41
[sdk-glue]: https://github.com/pspdev/pspsdk/blob/e2b2b313d4175823a263c2c6cf175311e824eb77/src/libcglue/glue.c#L687
[psp-guide]: https://github.com/TheMrIron2/psp-survival-guide/blob/main/optimisation.md
