# simbesm — a BESM-6 simulator, on its way to Braam

A simulator of the **BESM-6** (БЭСМ-6), the most widely used Soviet mainframe
of the 1960s–80s: a 48-bit, octal, single-CPU machine with sign-magnitude
floating point. It boots Unix, and the kernel and disk images to do that are
in [demo/](demo/).

This repository is a fork of [Open SIMH](https://github.com/open-simh/simh),
reduced to the BESM-6 simulator alone. The goal is to port it to
[Braam](https://braamix.github.io) — an operating system that runs in a browser
tab, built from scratch in C++20 and compiled to WebAssembly. The build here is
still the ordinary host build; the Braam target comes later.

## Building

Needs a C99 compiler and CMake 3.20 or newer. Nothing else — no SDL, no pcap,
no external libraries at all.

```sh
make                    # or: cmake -B build && cmake --build build
```

That produces `build/besm6`.

```sh
make test               # or: ctest --test-dir build --output-on-failure
make clean
```

`make test` runs [tests/unix.exp](tests/unix.exp), which boots Unix
under `expect` and types at the shell. It needs `/usr/bin/expect`; without it
the test is skipped.

Build options: `-DCMAKE_BUILD_TYPE=Debug` for an unoptimised build with
symbols, `-DSIMBESM_WERROR=OFF` to stop warnings failing the build.

## Running

The simulator takes no script and no arguments: it configures the machine,
loads the Unix kernel and starts it. It names its images relatively, so run it
from the directory that holds them:

```sh
cd demo && ../build/besm6      # boot Unix, and get a shell
```

That leaves you at a shell prompt. There is no `sim>` prompt any more — `^E`
stops the machine and ends the run. See
[besm6/README.md](besm6/README.md#booting-unix).

The setup steps live in [besm6/besm6_main.c](besm6/besm6_main.c) as direct calls
on the device structures, in the order the old `demo/unix.ini` command script
performed them.

## Documentation

- **[besm6/README.md](besm6/README.md)** — the user manual: machine model,
  registers, loading and dumping programs, the symbolic assembler and
  disassembler, peripherals, terminals, debugging, worked examples.
- **[tests/README.md](tests/README.md)** — the one regression test,
  an `expect` script that boots Unix, and how to add to it.

## What this fork removed

Upstream SIMH carries dozens of simulators and a framework that supports all of
them. Everything BESM-6 does not reach is gone, which is most of it:

- **Every other simulator** — VAX, PDP-11, PDP-10, HP2100, and the rest.
- **The graphical front panel.** It was drawn with SDL2 and SDL2_ttf, and Braam
  has no SDL. The machine's own front panel — the switch registers and the
  request interrupt — is unaffected; it is just C state now.
- **The peripherals Unix never touches** — the АЦПУ line printer (`PRN`), the
  punch-tape reader and punch (`FS`, `PL`), the card reader and punch (`VU`,
  `PI`), and magnetic tape (`MG3`…`MG6`). What Unix boots from is disks, drums
  and terminals; the rest was dead weight.
- **Framework modules with no BESM-6 caller** — `sim_disk` (raw devices, VHD
  images), `sim_tape`, `sim_ether` and the DDCMP framer, `sim_card`, `sim_imd`,
  `sim_scsi`, `sim_video`, `sim_frontpanel`. BESM-6 disks and drums are plain
  files handled by `besm6_disk.c` and `besm6_drum.c`; its terminals use
  `sim_tmxr`, which remains.
- **The whole SCP command interpreter.** Nothing types commands at this build,
  so the command table and every command went: `DO`, `GOTO`, `IF`, `ON`,
  `ASSERT`, `EXPECT`/`SEND` and the rest of the scripting language; the `sim>`
  read-eval loop and the `simh.ini` cascade; `SET`, `SHOW`, `ATTACH`,
  `EXAMINE`, `DEPOSIT`, `BREAK`, `SAVE`/`RESTORE`; the shell-alike commands
  (`CD`, `DIR`, `TYPE`, `COPY`, `TAR`, `CURL`, …); the remote-console telnet
  interpreter; the developer test hooks (`TESTLIB`, `CheckSourceCode`,
  `RegisterSanityCheck`); and the ~3500-line `HELP` subsystem with all its text.
  Devices are configured, attached and run by direct calls instead — see
  [besm6/besm6_main.c](besm6/besm6_main.c). `SAVE`/`RESTORE` went with the rest,
  so this build no longer reads or writes SIMH save files.
- **The 137 KB GNU makefile**, `descrip.mms`, and the Visual Studio / MinGW /
  AppVeyor builders, replaced by `CMakeLists.txt`.
- **Everything in the framework that no longer executed.** A pass over what
  remained found much of it unreachable — kept alive by a global nothing
  assigned, a `UNIT` flag no device sets, or a switch no caller passes. Gone
  with it: the software breakpoint package (the machine's own `М34`/`М35`
  registers do the job), the console log file (`sim_log` was never opened —
  `BESM6_DEBUG` is now the one output file), host idling and catch-up ticks,
  the `ps | grep` probe for a debugger, the endian byte-swap on every disk
  read, buffered units, and eight of the nine calibrated timers.

What is left is [simh/](simh/) — `scp.c` plus four `sim_*.c` modules, about 2.6k
lines where upstream had 39k — and the ten BESM-6 sources in [besm6/](besm6/).
`scp.c` is now a runtime library: the event queue, unit attach/detach, the debug
and trace output path, and `sim_run()`, which brackets `sim_instr()` with console
mode, signal handlers and wall-clock timing.

## Licence

SIMH is copyright © 1993–2022 Robert M Supnik, Mark Pizzolato and others; the
BESM-6 simulator is copyright © Serge Vakulenko and Leonid Broukhis. Both are
under the MIT-style licence in [LICENSE.txt](LICENSE.txt), and each source file
carries its own notice.
