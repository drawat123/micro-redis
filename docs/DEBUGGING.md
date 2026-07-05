# Debugging micro-redis (terminal gdb)

> The VS Code F5 debugger can be flaky in this dev-container setup. This is the
> reliable fallback — and terminal `gdb` is a core systems-engineer skill worth
> having anyway. It's the same debugger VS Code drives, minus the flaky layer.
>
> Run everything below **inside the container** (that's where the Linux binary,
> symbols, and gdb live). See `TOOLCHAIN.md §3` for the full VS-Code ⇄ gdb map.

---

## Prerequisite: a Debug build (symbols)

gdb needs debug symbols. The default build type here is `Debug` (adds `-g`), so a
normal build is already debuggable:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Verify symbols exist if unsure: `readelf -S build/micro-redis | grep debug`
(should list `.debug_*` sections).

---

## Quick start

```bash
gdb ./build/micro-redis
(gdb) break main          # stop at the start
(gdb) run 6380            # launch with args
```

---

## The commands that cover ~95% of debugging

| gdb command | short | Visual Studio / VS Code equivalent | What it does |
|---|---|---|---|
| `run 6380` | `r` | Start Debugging (F5) | launch the program with args |
| `break server.cpp:42` | `b` | breakpoint (F9) | stop at a file:line |
| `break make_listener` | `b` | function breakpoint | stop when a function is entered |
| `break foo.cpp:10 if x>5` | | conditional breakpoint | stop only when the condition holds |
| `continue` | `c` | Continue (F5) | resume until the next breakpoint |
| `next` | `n` | Step Over (F10) | run the next line, don't enter calls |
| `step` | `s` | Step Into (F11) | enter the function on this line |
| `finish` | | Step Out (Shift+F11) | run until the current function returns |
| `print myvar` | `p` | hover / Watch | show a variable's value |
| `print/x myvar` | | Watch (hex) | show it in hexadecimal |
| `info locals` | | Locals panel | all local variables |
| `info args` | | Locals panel | the current function's arguments |
| `backtrace` | `bt` | Call Stack panel | the full call stack |
| `frame 2` | `f 2` | click a stack frame | switch to stack frame #2 |
| `display myvar` | | Watch | auto-print myvar after every step |
| `delete` | `d` | remove breakpoints | clear all breakpoints |
| `quit` | `q` | Stop | exit gdb |

---

## Debugging the server specifically

The server blocks in `epoll_wait` until a client connects, so a breakpoint in the
read/echo path won't fire until traffic arrives:

```bash
gdb ./build/micro-redis
(gdb) break server.cpp:110     # somewhere in the read/echo handling
(gdb) run 6380
#   ... server is now waiting ...
```
Then, from **another terminal**, send it something:
```bash
nc localhost 6380
# type a line → gdb stops at your breakpoint
```

> A breakpoint that "never hits" is often just code that hasn't run yet — trigger
> the path (connect a client) before assuming it's broken.

---

## Post-mortem: debug a crash after it happened

When the program segfaults on its own, inspect the core dump:

```bash
ulimit -c unlimited          # enable core dumps (once per shell)
./build/micro-redis 6380     # ... it crashes, leaving a core file ...
gdb ./build/micro-redis core
(gdb) backtrace              # exact line + call stack of the crash
```

---

## Attach to an already-running server

```bash
./build/micro-redis 6380 &   # start in background, note the PID it prints
gdb -p <PID>                 # attach; use break/continue as usual
(gdb) detach                 # let it run free again
```

---

## TUI mode (a source view in the terminal, VS-like)

```bash
gdb -tui ./build/micro-redis
# or inside gdb:  (gdb) layout src
```
Shows the source with the current line highlighted as you step. `Ctrl-x a` toggles
it off.

---

## Often faster than a debugger

For an event loop, these frequently beat stepping:

- **printf/logging** — drop `std::printf("accept fd=%d\n", fd);` at key points and
  watch the flow. Cheap, and it shows timing/ordering a debugger obscures.
- **Sanitizers** — rebuild with ASan/UBSan and memory bugs report themselves with
  a stack trace, no debugger needed:
  ```bash
  cmake -S . -B build-asan -G Ninja -DMICRO_REDIS_SANITIZE=ON
  cmake --build build-asan
  ./build-asan/micro-redis 6380
  ```
  (Add ThreadSanitizer — a separate `-fsanitize=thread` build — once you have
  threads at rung 7.)

---

## .gdbinit (optional niceties)

Create `~/.gdbinit` in the container to set these every session:

```
set pagination off
set print pretty on
set debuginfod enabled off
```
