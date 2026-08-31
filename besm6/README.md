# BESM-6 simulator

A simulator of the **BESM-6** (БЭСМ-6), the most widely used Soviet mainframe of
the 1960s–80s: a 48-bit machine, octal throughout, with sign-magnitude
floating point. Everything needed to boot **Unix** ships in [demo/](../demo/).

This build has **no command interpreter** — no `sim>` prompt, no scripts. To
change what the simulator does, edit [besm6_main.c](besm6_main.c) and rebuild.

The machine, its software and its mnemonics are Russian. Registers have Cyrillic
names with Latin synonyms, the disassembler speaks both БЕМШ and MADLEN, and
terminals can type Cyrillic from a Latin keyboard — so a plain ASCII terminal is
enough.

## Build and run

```sh
make                      # from the repository root; produces build/besm6
cd demo && ../build/besm6 # boot Unix and get a shell
```

Image names are hardcoded and relative, so run from the directory holding them.
`^E` stops the run and exits.

`make test` boots the same images under [tests/unix.exp](../tests/unix.exp) and
checks the replies. It needs `/usr/bin/expect`; CMake skips the test otherwise.

`besm6_main.c` performs the steps the old `demo/unix.ini` command script did,
in the same order.

## Machine model

* **Word.** 48 data bits plus a 2-bit tag marking the word as an *instruction*
  or a *number*. Fetching a data-tagged word raises a machine check.
* **Memory.** 512 K words. Addresses are 15 bits.
* **Radix.** Octal, everywhere.
* **Switch registers.** Octal addresses `1`–`7` are not RAM — they are the
  front-panel switches, in `memory[1]`…`memory[7]`.
* **Floating point.** Sign-magnitude, base-2 exponent.

## Registers

Cyrillic names are what appear in disassembly and traces; the Latin synonyms are
the C identifiers. `M[]` holds the index and special registers, indexed by the
constants in [besm6_defs.h](besm6_defs.h).

| Cyrillic | Latin | Bits | Meaning |
|----------|-------|------|---------|
| `СчАС` | `PC` | 15 | Program counter. |
| `РК` | `RK` | 24 | Current instruction. |
| `Аисп` | `Aex` | 15 | Effective address. |
| `СМ` | `ACC` | 48 | Accumulator. |
| `РМР` | `RMR` | 48 | Low-order-bits register. |
| `РАУ` | `RAU` | 6 | ALU mode bits. |
| `М1`…`М17` | `M1`…`M17` | 15 | Index/modifier registers (М17 is also SP). |
| `М20` | `M20` | 15 | Address modifier (MOD). |
| `М21` | `M21` | 15 | Program status (PSW). |
| `М27` | `M27` | 15 | Saved status (SPSW). |
| `М32`–`М33` | `M32`–`M33` | 15 | Extracode / interrupt return addresses. |
| `М34` | `M34` (`IBP`) | 16 | Instruction breakpoint (hardware КРА). |
| `М35` | `M35` (`DWP`) | 16 | Data watchpoint. |
| `РУУ` | `RUU` | 9 | Execution-mode bits. |
| `ГРП` / `МГРП` | `GRP` / `MGRP` | 48 | Main interrupt register and mask. |
| `ПРП` / `МПРП` | `PRP` / `MPRP` | 24 | Peripheral interrupt register and mask. |

Also present: the write-cache registers `BRZ0`–`BRZ7` / `BAZ0`–`BAZ7`, the page
table `TABST` / `RP0`–`RP7` / `RZ`. All ordinary C state.

## Options

Plain assignments in [besm6_main.c](besm6_main.c), before the run starts.

| C | Effect |
|---|--------|
| `besm6_latin = 1` | Disassemble as MADLEN (Latin) instead of БЕМШ. |
| `autotime = 1` | Do the front-panel date/time setup DISPAK expects at boot. |
| `GRP \|= GRP_PANEL_REQ` | Press the operator "request" button. |
| `pult_packet_switch = n` | Boot source: `0` = switch registers, `1`–`10` = a hardwired bootstrap. |
| `mmu_unit.flags \|= CACHE_ENB` | Model the БРЗ write cache. Accurate, ~20 % slower. |
| `mmu_unit.flags \|= CHECK_ENB` | Parity checking. |

The operator's switch registers and request button:

```c
memory[6] = 1;             /* switch register 6 */
memory[5] = 010;           /* switch register 5 */
GRP |= GRP_PANEL_REQ;      /* press "request" */
```

The OS reads registers 5 and 6 as a console command code. Upstream SIMH could
also open an SDL window of blinkenlights; this build has no graphics, and
everything it showed is reachable as C state.

## Loading programs

`sim_load()` in [besm6_sys.c](besm6_sys.c) reads a **text** file in DISPAK
format (`.b6`) and auto-detects binary `a.out` images:

```c
FILE *f = fopen("file.b6", "rb");
sim_load(f, "", "file.b6", 0);   /* a `п' line sets the PC */
fclose(f);
```

Each line starts with a one-letter type code, Cyrillic or Latin,
case-insensitive, followed by octal operands. `;` starts a comment.

| Code | Meaning |
|------|---------|
| `в` / `b` | Set the load **address**. Loading starts at 1. |
| `п` / `p` | Set the **start address** (the PC). |
| `ч` / `f` | A **floating-point** number. |
| `с` / `c` | An octal **data word**, up to 16 digits. |
| `к` / `k` | One or two **instructions**, comma-separated. |

Each word advances the load address by one; words below address `10` go to the
switch registers. A non-zero `dump_flag` writes memory back out in the same
format instead of reading it.

```text
в 1
к сл  7,  зп   11
к вчп 11, зп   10
к пе  6,  стоп
в 7
ч 1.0
п 1
```

## Disassembly

`fprint_sym()` prints the instruction in stop messages and in the CPU trace. The
format comes from the switch bits passed to it:

| Switch | Format |
|--------|--------|
| *(none)* | Four 12-bit octal groups. |
| `-m` | **БЕМШ** (Cyrillic) mnemonics. |
| `-ml` | **MADLEN** (Latin) mnemonics. |
| `-i` | Octal instruction fields: register, opcode, address. |
| `-f` | The word as a floating-point number. |
| `-b` | Six octal bytes. |
| `-x` | 13 hexadecimal digits. |

One word, four ways: `0000 2000 0000 0210` raw, `сч 4412(1)` as БЕМШ,
`xta 4412(1)` as MADLEN, `2.7e-20` as a real.

## Peripherals

Attach routines are declared in [besm6_defs.h](besm6_defs.h). The switches the
old `ATTACH` command took are passed in the global `sim_switches`: `-n` creates
a new image, `-e` requires an existing one. Set it before the call and clear it
after — `disk_attach()` ORs in `-e` itself.

Disks and drums share a geometry: *zones* of `8 + 1024` words (8 service words,
then 1 Kword of data), each word an 8-byte little-endian record.

**Magnetic disks `MD0`…`MD7`** — eight controllers of 8 units, `md_unit[0..63]`.

```c
disk_attach(&md_unit[0], "root3072.disk");
```

With `-n` the image is formatted and the volume number is taken from the digits
in the filename; it **must be 2048–4095** or the attach is rejected and the file
removed. Drive type is a unit flag: `DISK_TYPE_7_25M` (1000 blocks) or
`DISK_TYPE_29M` (4000 blocks).

**Magnetic drums `DRUM`** — two units, used as paging and swap store.

```c
sim_switches = SWMASK('N');                /* create empty */
drum_attach(&drum_unit[0], "unix0.drum");
drum_attach(&drum_unit[1], "unix1.drum");
sim_switches = 0;
```

**What is gone.** Unix boots from disks, drums and terminals, so nothing else is
here: line printer, punch tape, cards and magnetic tape are all removed. Their
I/O addresses are still decoded by `cmd_033()` in [besm6_cpu.c](besm6_cpu.c),
but do nothing — writes ignored, reads return zero.

## Terminals

The `TTY` device carries **24 serial lines** (`tty1`…`tty24`) plus **two
parallel "Consul" lines** (`tty25`, `tty26`). The demo uses `tty25` as the local
console and `tty26` as a telnet line.

```c
tty_attach(&tty_unit[25], "console");        /* bind to the local console */
tty_attach(&tty_unit[26], "Line=26,4199");   /* listen for telnet on port 4199 */
tty_attach(&tty_unit[3],  "none");           /* mark the line unusable */
```

Anything that is not `console` or `none` goes to `tmxr_attach()`, which accepts
only the `Line=<n>,<port>` form. A new telnet connection is greeted with an
encoding banner and comes up as a Videoton-340.

A line's mode is a character set, a terminal type and a backspace style, all set
together in the unit's flag word — **before** `tty_attach()`, which reads them:

```c
tty_unit[25].flags = (tty_unit[25].flags & ~TTY_CHARSET_MASK) | TTY_RAW8_CHARSET;
```

Character set — how bytes map to and from the machine's KOI-7:

| Mode | Meaning |
|------|---------|
| `unicode` | UTF-8 in and out. |
| `jcuken` | Russian via the ЙЦУКЕН layout on Latin keys. |
| `qwerty` | Russian as transliterated Latin: `Q`=я, `W`=в, `Y`=ы, `J`=й, `X`=ь, `C`=ц, `V`=ж, `` ` ``=ю, `~`=ч, `{`=ш, `}`=щ, `\|`=э. |
| `raw` | No conversion, but the hardware's 7-bits-plus-parity contract holds: input above `0177` is dropped, output masked to `0177`, parity synthesised. |
| `raw8` | The same, eight bits wide: nothing truncated, nothing dropped, no parity. The guest owns the character set. This is what `v7besm` uses to carry UTF-8. |

Terminal type: `vt` (Videoton-340, control codes translated to VT100 escapes;
the default), `tt` (MTK-2 Baudot teletype, serial lines only), `consul`
(Consul-254, lines 25/26 only), `off`.

Backspace: `destrbs` (erasing, the default) or `authbs` (cursor-left only, as on
the real hardware).

Device-wide: `tty_rate` (300…19200 Hz, default 300) and `tty_turbo` (interrupt
timing follows model time when 1, wall clock when 0).

> Pressing the interrupt character on a telnet line reaches a small in-band CLI
> with its own `set`/`show`/`help`/`exit`, implemented in
> [besm6_tty.c](besm6_tty.c). It is the one command interpreter left in the tree.

## Debugging

Set a device's `dctrl` field non-zero to trace it:

```c
cpu_dev.dctrl = ~0;
mmu_dev.dctrl = ~0;
```

The disk device has named categories to OR together instead of `~0`: `DEB_OPS`,
`DEB_RRD`, `DEB_RWR`, `DEB_INT`, `DEB_TRC`, `DEB_DAT`, `DEB_STA`.

Two environment variables do the same without recompiling — `BESM6_DEBUG` names
the output file (`-` means stderr), `BESM6_TRACE` lists devices to trace:

```sh
BESM6_DEBUG=- BESM6_TRACE=cpu,mmu ./besm6     # trace to stderr
BESM6_DEBUG=run.log BESM6_TRACE=none ./besm6  # log file, no instruction trace
```

`sim_deb` is the single output file; `besm6_debug()`, `besm6_log()` and
`besm6_log_cont()` all write to it. A format string starting with `_` goes to
the file only — that is how operator dumps stay off a terminal Unix is using.

### The instruction trace

`cpu_dev.dctrl` logs every executed instruction with the state it touches:

```text
32012 R: 00 100 7766 зп -12
      Memory Write [77766] = 0000 0000 0000 0000
32013 L: 00 037 0000 ржа
      RAU = 00
```

The address, `L`/`R` for the half-word, the octal fields, the mnemonic — then
only the registers that *changed*, and any operand read or write. Instruction
fetches are not traced. Extracodes append their executive address as `= addr`.
The first line dumps every register. Faults get a line of their own:

```text
----- 00500L: Запрещенная команда -----
```

The trace runs to thousands of lines per millisecond of model time, so switch
`cpu_dev.dctrl` on and off around the region you care about. `trace_counter`
limits it to that many instructions.

### Breakpoints

The machine's own are all that is left — `М34` stops on an instruction fetch,
`М35` on a load or store:

```c
M[IBP] = 032013;   /* stop when PC reaches 032013 */
PC     = 032000;
r      = sim_run();
```

SIMH's software breakpoints are gone; nothing set one, and its `E`/`R`/`W` types
duplicated `М34`/`М35`.

### Stop codes

`besm6_main.c` prints the reason from `sim_stop_messages[]`:

| Message | Meaning |
|---------|---------|
| Останов | `STOP` executed. |
| Выход за пределы памяти | Ran past the end of memory. |
| Запрещенная команда | Illegal instruction. |
| Контроль команды | A data-tagged word fetched as an instruction. |
| Команда / Число в чужом листе | Paging fault on a fetch / on a data access. |
| Контроль числа МОЗУ / БРЗ | RAM / write-cache parity error. |
| Переполнение АУ | Arithmetic overflow. |
| Деление на нуль | Division by zero or a denormal. |
| Двойное внутреннее прерывание | Double internal interrupt. |
| Чтение неформатированного барабана / диска | Read from an unformatted drum / disk. |
| Останов по КРА / считыванию / записи | Breakpoint / load / store watchpoint hit. |
| Не реализовано | Unimplemented I/O or special-register feature. |

## Booting Unix

`besm6_boot_unix()` in [besm6_main.c](besm6_main.c) does it, in this order:

1. `besm6_latin = 1`, and `CACHE_ENB` so the БРЗ write-back cache is modelled —
   the kernel writes user memory through the map, so a build that only worked
   with the cache off would not have worked on the real machine.
2. `tty25` to `raw8` and attached to `console`; `tty26` offered over telnet.
3. Root pack on `md00`, `/usr` on `md01`, both writable; two drums with `-n`,
   empty. The drums are **swapdev**, and `exece()` stages the argument list in
   swap before touching the new image — with no drum, every `exec` fails with
   `error 5`.
4. `sim_load()` on `unix`, a binary `a.out`, which sets the PC from its entry
   point; then `sim_run()`.

```text
phys mem  = 3072 kbytes
user mem  = 2874 kbytes
swap size = 3072 kbytes
root size = 6000 kbytes

Single-user mode -- type ^D to run /etc/rc and go multi-user
# _
```

Line editing at that prompt is the *kernel's*: `^?` erases a character, `^U`
kills the line. `^D` ends the shell, `init` runs `/etc/rc` and the machine comes
up multi-user. `^E` stops the run. Nothing calls `sync(2)` for you.

There is no clock-calendar, so the date starts at whatever the filesystem was
stamped with; type `date` to set it.

A session writes `root3072.disk` and `usr3100.disk` in place. They are tracked
files, so `git status` shows them modified afterwards and `git checkout`
restores them. The regression test copies the images instead, for this reason.
