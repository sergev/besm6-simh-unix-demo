# BESM-6 regression test

One test: [unix.exp](unix.exp), an `expect` script that boots Unix and checks
what the operating system prints.

## Running

```sh
make test                 # from the repository root
ctest --test-dir build --output-on-failure
tests/unix.exp -v         # by hand; -v echoes the session
```

It needs `/usr/bin/expect`; CMake skips the test if that is missing. On any
mismatch it prints a `FAIL:` line naming the step that did not happen, and exits
non-zero.

## What it does

`besm6_main.c` hardcodes its image names and reads them from the current
directory, so the script copies `demo/`'s two packs and the `unix` kernel to a
scratch directory and runs there — the boot writes to both packs and creates the
drums, and the tracked images must stay clean. It sets `BESM6_DEBUG` to a file
with `BESM6_TRACE=none`, so operator messages land there and stdout carries only
what Unix printed.

Then, in order:

1. The four attach messages — `tty25` on the console, `tty26` listening on port
   4199, and the two drums created with `-n`.
2. The kernel's `startup()` and `iinit()` banners, which say memory was sized
   and the root pack mounted, so `md00` is attached.
3. `ls /bin` at the first shell prompt — a command read off the root pack and
   typed through `tty25` in `raw8`.
4. `date 2608301200`, since the machine has no clock-calendar.
5. `^D`, which ends the shell so `init` runs `/etc/rc`, which fscks and mounts
   `/dev/rmd1` on `/usr`. That exercises `md01` and the drums — `exece()` stages
   its argument list in swap, so with no drum every `exec` returns EIO.
6. `^E` to stop the CPU, then the exit status and the fact that the
   `BESM6_DEBUG` file was written and closed.

A pass means the a.out loader, the MMU with its write cache, disk I/O, the drums
as swap and the console TTY all worked together. Assert new behaviour by adding
`want` steps.
