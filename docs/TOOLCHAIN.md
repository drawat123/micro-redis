# Visual Studio → Linux CLI — A Crosswalk

> For someone fluent in the VS debugger/profiler on Windows, moving to the Linux
> command-line toolchain. Every VS feature you rely on has a CLI equivalent —
> this maps them 1:1, then gives a Docker dev setup so you build on real Linux.

---

## 0. The mental shift

Visual Studio bundles **six separate tools** behind one GUI. On Linux they're
separate programs you compose:

| What VS gives you | The Linux tool(s) |
|---|---|
| The `cl.exe` compiler (hidden behind "Build") | **clang** / **gcc** (`clang++`, `g++`) |
| `.sln` / `.vcxproj` project system | **CMake** (generates the build) + **make**/**ninja** (runs it) |
| The graphical debugger (F5, breakpoints, watch) | **gdb** (Linux) / **lldb** (macOS) |
| The Performance Profiler | **perf** + flamegraphs (Linux); Instruments (macOS) |
| Memory/leak diagnostics | **valgrind**, and compiler **sanitizers** (ASan/TSan/UBSan) |
| Code analysis / squiggles | **clang-tidy**, **clang-format**, `-Wall -Wextra` |

You'll drive them from a terminal. It feels lower-level at first, then faster —
because each is scriptable and reproducible (which is why CI uses them).

---

## 1. Compiler:  Build ▶  →  `clang++` / `g++`

Compile one file:
```bash
clang++ -std=c++20 -Wall -Wextra -O2 main.cpp -o app
./app
```
- `-std=c++20` — language standard (VS: "C++ Language Standard" dropdown)
- `-Wall -Wextra` — turn on warnings (always). VS's `/W4`.
- `-O0 -g` — no optimization + debug symbols (Debug build). VS "Debug".
- `-O2` / `-O3` — optimized (Release build). VS "Release".
- `-o app` — output name.

**Note on your Mac:** `gcc` there is actually Apple **clang** (an alias). Real
GNU gcc and the Linux headers only exist inside the Linux container (§7).

---

## 2. Build system:  `.sln`/`.vcxproj`  →  CMake

You won't hand-write compile commands past a toy. CMake is the cross-platform
`.vcxproj` — it *describes* the project; make/ninja *builds* it.

`CMakeLists.txt` (the whole build in a few lines):
```cmake
cmake_minimum_required(VERSION 3.20)
project(micro_redis CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)
add_executable(server src/main.cpp)
```

Configure once, then build (repeat as you edit):
```bash
cmake -S . -B build          # generate (like "load the .sln")
cmake --build build          # compile (like Build ▶)
./build/server               # run
```
- `-S .` source dir, `-B build` out-of-source build dir (keeps the tree clean).
- Switch Debug/Release: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`.

**Cheat:** `cmake --build build -j` builds in parallel (all cores).

---

## 3. Debugger:  VS Debugger (F5)  →  gdb

Same concepts, keyboard-driven. Build with `-g -O0` first (symbols, no optimizer).

```bash
gdb ./build/server
```

| VS action | gdb command |
|---|---|
| Start debugging (F5) | `run` (or `r`) |
| Set breakpoint (F9) | `break main.cpp:42` (`b main.cpp:42`) |
| Breakpoint on function | `break execute` |
| Continue (F5) | `continue` (`c`) |
| Step over (F10) | `next` (`n`) |
| Step into (F11) | `step` (`s`) |
| Step out (Shift+F11) | `finish` |
| Watch / inspect a variable | `print var` (`p var`), `display var` |
| Call stack window | `backtrace` (`bt`) |
| Locals window | `info locals` |
| Switch stack frame | `frame 2` (`f 2`) |
| Conditional breakpoint | `break foo.cpp:10 if x > 5` |
| Quit | `quit` (`q`) |

**Crash investigation (the killer feature):** run under gdb, and on a segfault it
drops you at the exact line — `bt` shows the whole call stack. For a post-mortem
of a crash outside gdb, enable core dumps (`ulimit -c unlimited`) then
`gdb ./server core`.

> On **macOS** the debugger is **lldb** (gdb is painful there due to code-signing).
> Commands are similar (`b`, `r`, `n`, `s`, `bt`, `p`). But you'll mostly debug
> **inside the Linux container with gdb**, since that's where the code runs.

---

## 4. Profiler:  VS Performance Profiler  →  perf + flamegraphs

`perf` is the Linux sampling profiler (like VS's CPU Usage tool).

```bash
perf stat ./build/server            # summary: cycles, instructions, cache misses, IPC
perf record -g ./build/server       # sample with call graphs → perf.data
perf report                         # interactive top-down view (like VS's tree)
```

**Flamegraph** (the visual VS-profiler-style view):
```bash
perf record -g ./build/server
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
# (Brendan Gregg's FlameGraph scripts)
```

Read cache/branch behavior directly:
```bash
perf stat -e cache-misses,cache-references,branch-misses ./build/server
```

> `perf` is **Linux-only** and wants kernel access. It works best in a real Linux
> VM or cloud box; inside Docker Desktop it may be limited. Rungs 0–4 don't need
> it — introduce it at rung 4+ (the perf pillar). On macOS the native equivalent
> is **Instruments** / `sample`, but the target companies use `perf`, so learn `perf`.

---

## 5. Memory & correctness:  VS diagnostics  →  sanitizers + valgrind

Two complementary approaches. **Sanitizers** are the modern default — you
recompile with a flag and the program self-reports at runtime.

```bash
# AddressSanitizer — use-after-free, buffer overflow, leaks
clang++ -std=c++20 -g -fsanitize=address,undefined main.cpp -o app && ./app

# ThreadSanitizer — data races (use once you add threads, rung 7)
clang++ -std=c++20 -g -fsanitize=thread main.cpp -o app && ./app
```
| Sanitizer | Catches | When |
|---|---|---|
| **ASan** | use-after-free, overflows, leaks | always |
| **UBSan** | undefined behavior (overflow, bad casts) | always |
| **TSan** | data races | rung 7 (threads) |
| **MSan** | uninitialized reads | Linux/clang only (optional) |

> Don't combine ASan and TSan in one binary. Keep two build configs.

**valgrind** (heavier, no recompile needed) — memory errors + heap profiling:
```bash
valgrind --leak-check=full ./build/server        # memcheck
valgrind --tool=massif ./build/server            # heap-usage over time
```
> valgrind does **not** run on Apple-Silicon macOS — it lives in the Linux
> container. Day to day, prefer sanitizers (faster); reach for valgrind for
> deep leak hunts.

---

## 6. Static analysis / formatting:  VS squiggles  →  clang-tidy + clang-format

```bash
clang-tidy src/*.cpp -- -std=c++20        # lint: bug-prone patterns, modernize hints
clang-format -i src/*.cpp                 # auto-format (like Ctrl+K, Ctrl+D)
```
`clang-tidy`'s `modernize-*` checks are perfect for your goal — they flag
old-style C++ and suggest the modern form. Wire it into CI later.

---

## 7. The Linux dev environment (Docker) — where you'll actually build

You're on Apple-Silicon macOS; `epoll`/`io_uring`/`perf`/`valgrind` are Linux-only.
So: **edit files on the Mac, build & run inside a Linux container.** Docker is
already installed.

The scaffold will add a `Dockerfile` like:
```dockerfile
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y \
    build-essential clang clang-tidy clang-format cmake ninja-build \
    gdb valgrind git redis-tools \
    liburing-dev linux-tools-generic \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /work
```
- `build-essential` = gcc/g++/make; plus clang, cmake, ninja
- `gdb valgrind` = debug + memory tools (both work on arm64 Linux)
- `redis-tools` = **`redis-cli` + `redis-benchmark`** to drive/benchmark your server
- `liburing-dev` = io_uring headers (rung 5)
- `linux-tools-generic` = `perf` (best-effort in Docker; see §4)

Typical loop:
```bash
# from the repo root on your Mac — mount the code, drop into Linux:
docker build -t microredis-dev .
docker run --rm -it -v "$PWD":/work -p 6380:6380 microredis-dev bash
# now inside Linux:
cmake -S . -B build && cmake --build build -j && ./build/server
```
- `-v "$PWD":/work` mounts your code (edit on Mac, compile in Linux — no copying).
- `-p 6380:6380` exposes your server's port to the Mac so `redis-cli -p 6380` works from either side.

> For serious `perf` profiling later, consider a lightweight Linux VM
> (`brew install colima` → `colima start`, or `multipass`) or a cheap cloud box —
> `perf` has fewer restrictions on a real VM than in Docker Desktop.

---

## 8. One-page cheat sheet

```text
COMPILE     clang++ -std=c++20 -Wall -Wextra -g -O0 f.cpp -o app
BUILD       cmake -S . -B build && cmake --build build -j
RUN         ./build/server
DEBUG       gdb ./build/server   → b file:line · r · n · s · bt · p var · c · q
PROFILE     perf stat ./app   |   perf record -g ./app && perf report
LEAKS       clang++ -fsanitize=address,undefined ...   |   valgrind --leak-check=full ./app
RACES       clang++ -fsanitize=thread ...
LINT/FMT    clang-tidy src/*.cpp --   |   clang-format -i src/*.cpp
DRIVE IT    redis-cli -p 6380 SET foo bar   |   redis-benchmark -p 6380 -t set,get -n 100000
```

---

## 9. Gotchas coming from Windows/VS

- **No implicit "it just built."** If CMake config changes, re-run `cmake -S . -B build`. If only source changed, `cmake --build build` is enough.
- **Case-sensitive filesystem** on Linux: `#include "Server.h"` ≠ `server.h`.
- **Line endings:** keep files LF, not CRLF (configure git: `git config --global core.autocrlf input`).
- **Headers vs Windows APIs:** no `<windows.h>`, no `WinSock`. It's `<sys/socket.h>`, `<sys/epoll.h>`, `<unistd.h>`, POSIX all the way.
- **Linking errors look scary but are literal:** "undefined reference to `foo`" = you declared `foo` but didn't compile/link its definition. Add the .cpp to `add_executable`.
- **Segfault ≠ helpful dialog.** You get "Segmentation fault (core dumped)". That's your cue to run it under gdb.

---

*Keep this open in a split pane for the first few weeks. It stops being a
reference and becomes muscle memory faster than you'd expect.*
