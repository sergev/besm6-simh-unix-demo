# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Scope: BESM-6 only

**Work in this project is focused exclusively on the BESM-6 simulator in `besm6/`.** This repo is a fork of [Open SIMH](https://github.com/open-simh/simh) reduced to that one simulator; the goal is to port it to [Braam](https://braamix.github.io), a WebAssembly OS that runs in a browser tab. The SIMH framework lives in `simh/` (`scp.c`, `sim_*.c/.h`, `sim_defs.h`) and is upstream infrastructure — treat it as read-only unless a change genuinely belongs there.

Everything BESM-6 does not reach has been deleted: all other simulators, the SDL front panel, the entire SCP command interpreter (scripting, the `sim>` REPL, SET/SHOW/ATTACH/EXAMINE, SAVE/RESTORE, the remote console, HELP), every peripheral Unix does not boot from (line printer, punch tape, punch cards, mag tape), and the framework modules `sim_disk`, `sim_tape`, `sim_ether`, `sim_card`, `sim_imd`, `sim_scsi`, `sim_video`, `sim_serial`, `sim_frontpanel`. What remains in `simh/` is `scp.c` plus `sim_console`, `sim_sock`, `sim_timer`, `sim_tmxr`. Don't reintroduce the deleted ones; see [README.md](README.md) for why each went.

The **BESM-6** is a Soviet mainframe: 48-bit word, single CPU. Its documentation
and this code write word values in octal.

## Build

CMake, C99, no external libraries. The binary lands in `build/besm6`.

```sh
make            # or: cmake -B build && cmake --build build
make test       # or: ctest --test-dir build --output-on-failure
make clean
```

`Makefile` is only a wrapper; [CMakeLists.txt](CMakeLists.txt) is the build. `t_value` is a 64-bit integer unconditionally (the 48-bit word needs it); nothing is probed for, and there are no feature macros. `-Werror` is on by default — turn it off with `-DSIMBESM_WERROR=OFF`.

## Run and test

**There is no script interpreter and no command interpreter at all.** [besm6/besm6_main.c](besm6/besm6_main.c) is the entry point: it configures the machine, attaches the images, loads the Unix kernel and runs it, all as direct calls on the device structures. `simh/scp.c` is now a runtime library — event queue, attach/detach, debug output — with no `cmd_table`, no `SET`/`SHOW`/`ATTACH`/`EXAMINE`, no `DO`/`IF`/`EXPECT`, no `sim>` loop and no `HELP` text. An entry point brackets its work with `sim_scp_init()` / `sim_scp_exit()` and runs the machine with `sim_run()`, printing the stop reason from `sim_stop_messages[]`.

Image names are hardcoded and relative, so run from the directory holding them:

```sh
cd demo && ../build/besm6      # boot Unix, and get a shell (^E ends the run)
```

A session writes to `demo/root3072.disk` and `usr3100.disk` — `git checkout` restores them.

`make test` runs [tests/unix.exp](tests/unix.exp) under ctest: an `expect` script that copies the images to a scratch directory, boots there, types at the shell (`ls /bin`, `date`, `^D` to go multi-user) and checks the replies. It needs `/usr/bin/expect`; CMake skips the test if that is missing. Assert new behaviour by adding steps to it.

Tracing is likewise C now — set `cpu_dev.dctrl` / `mmu_dev.dctrl` / `drum_dev.dctrl` rather than `set cpu debug`, or use the `BESM6_DEBUG` (output file, `-` = stderr) and `BESM6_TRACE` (comma-separated device list) environment variables. `sim_deb` is the one output file: `besm6_debug()`, `besm6_log()` and `besm6_log_cont()` all write to it, and a format starting with `_` goes to the file only, not the console. The only `MTAB` table left is `tty_mod`, which `besm6_tty.c`'s own in-band telnet CLI walks.

## Architecture notes (`besm6/`)

- **Word model.** 48-bit words held in `t_value`. Constants and traces are written in octal. Floating point is sign-magnitude with a base-2 exponent. Bit macros in `besm6_defs.h` number bits **right-to-left starting at 1** (`BBIT(n)`, `BIT40`, `BIT41`, `BIT48`, …) — the opposite of most SIMH machines, so read the header before touching arithmetic.
- **Registers.** `M[]` holds the index/modifier registers (М1–М17) plus special registers (PSW, SPSW, PC, …), with indices defined in `besm6_defs.h`. Cyrillic register names (М1–М17, СМ/ACC) show up in traces.
- **Memory.** `512*1024` words. Drums/disks are organized in zones of `ZONE_SIZE = 8 + 1024` words (8 system words + 1 Kword of user data), each word stored as an 8-byte little-endian record.
- **File layout.** `besm6_cpu.c` = fetch/execute + `DEVICE` tables + `sim_devices[]`; `besm6_arith.c` = ALU/FPU; `besm6_mmu.c` = memory mapping & protection; `besm6_sys.c` = load/dump, symbolic assembler/disassembler; `besm6_disk.c`/`besm6_drum.c` = mass storage; `besm6_tty.c` = the only peripheral left (terminals); `besm6_gost.c` = GOST-10859 to Unicode/UTF-8, used by the Consul lines. The I/O addresses of the removed peripherals are still decoded by `cmd_033()` in `besm6_cpu.c`, but do nothing.
- **PC name clash.** `besm6_defs.h` does `#define PC PC_Global` to dodge a namespace conflict — keep that in mind when grepping.

## Conventions

- Comments and identifiers in `besm6/` are frequently in Russian (Cyrillic). Match the surrounding language when editing a file rather than converting it — except where a file has already been translated, as `besm6_defs.h` has. When translating, keep the machine's own mnemonics in Cyrillic (`ГРП`, `ПРП`, `БлП`, `АУ`, `УУ`, `Зп(М29)`, …): they are the names used by the hardware documentation, and only the prose around them should become English.
- `.editorconfig` is authoritative for whitespace; `.clang-format` for C/C++ style. `besm6_defs.h` uses `//` comments; the rest of the tree still uses `/* */`.
- `SortIncludes` is off in `.clang-format` on purpose — the SIMH headers are order-dependent and sorting them does not compile.
- clang-format miscounts column widths for Cyrillic, so a `/* */` comment carrying Russian text inside a multi-line macro makes it add backslash continuations and oscillate between runs. Put such prose above the `#define` rather than trailing it.
