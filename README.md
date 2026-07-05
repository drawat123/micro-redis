# micro-redis

A from-scratch, Redis-compatible in-memory key-value server, built in modern
C++ as a systems-engineering learning project. It grows in **rungs** — each a
complete, working program — from a bare event loop up to a benchmarkable,
persistent, sharded cache.

> **New to Docker / CMake / GoogleTest?** This README explains each as you use
> it, and `docs/TOOLCHAIN.md` maps every tool to its Visual Studio equivalent.
> Read `docs/PRIMER.md` first for the mental model of how Redis actually works.

**Current rung: 0 — a non-blocking TCP echo server on `epoll`.**

---

## Why Docker?

This project uses `epoll`, later `io_uring`, `/proc`, and `valgrind` — all
**Linux-only**. You're on macOS, so we compile and run inside a small Linux
container. You keep editing files normally on your Mac; the container just
provides the Linux compiler and tools. Think of it as a disposable Linux box
that shares your project folder.

**One-time setup:** install Docker Desktop and make sure it's running (whale icon
in the menu bar), then build the dev image (takes a few minutes the first time):

```bash
cd ~/Documents/Study/micro-redis
docker build -t microredis-dev .
```

---

## The daily loop

**1. Drop into the Linux container** (from the repo root):

```bash
docker run --rm -it -v "$PWD":/work -p 6380:6380 microredis-dev
```

What the flags mean:
- `--rm` — delete the container when you exit (your files live on the Mac, not in it)
- `-it` — interactive terminal
- `-v "$PWD":/work` — share the current folder into the container at `/work` (edit on Mac, build in Linux, no copying)
- `-p 6380:6380` — expose the server's port to your Mac, so `redis-cli`/`nc` work from either side

You're now at a `bash` prompt **inside Linux**. Everything below runs there.

**2. Configure once** (generates the `build/` folder; also downloads GoogleTest the first time):

```bash
cmake -S . -B build -G Ninja
```

**3. Build** (repeat this every time you change code):

```bash
cmake --build build
```

**4. Run the tests:**

```bash
ctest --test-dir build --output-on-failure
```

**5. Run the server:**

```bash
./build/micro-redis          # listens on 6380 (or pass another port)
```

---

## Try the echo server

With the server running in one container shell, open a **second** shell into the
same environment (or use your Mac terminal, since the port is exposed) and poke it:

```bash
# from your Mac (port 6380 is forwarded) or inside another container shell:
nc localhost 6380
# type anything, press Enter — it echoes back. Ctrl-C to quit.
```

> `redis-cli` won't work yet — it speaks the RESP protocol, which we implement in
> **rung 1**. Rung 0 is a raw echo, so test it with `nc`.

---

## Debugging & tools (all inside the container)

```bash
# Debugger (see docs/TOOLCHAIN.md §3 for the gdb⇄Visual-Studio command table):
gdb ./build/micro-redis        # then: run · break server.cpp:42 · bt · print var

# Sanitizer build — catches memory bugs & UB at runtime:
cmake -S . -B build-asan -G Ninja -DMICRO_REDIS_SANITIZE=ON
cmake --build build-asan
./build-asan/micro-redis

# Memory check with valgrind:
valgrind --leak-check=full ./build/micro-redis

# Lint & format:
clang-tidy src/*.cpp -- -std=c++20 -Isrc
clang-format -i src/*.cpp src/*.hpp
```

---

## Project layout

```text
micro-redis/
├── docs/                # PRIMER.md (how Redis works), TOOLCHAIN.md (VS→Linux)
├── include/             # YOUR headers (.hpp) — interfaces & header-only utils
├── src/                 # YOUR sources (.cpp) — implementations + main.cpp
├── reference/rung0/     # Claude's rung-0 solution — peek only if truly stuck
├── CMakeLists.txt       # build description (auto-compiles every src/*.cpp)
├── Dockerfile           # the Linux dev environment
└── README.md
```

> Headers go in `include/`, sources in `src/`. Include a header from any `.cpp`
> with `#include "server.hpp"` — the build already knows to look in `include/`.
> Unit tests (`tests/`) come back at rung 1.

---

## The roadmap (rungs)

| Rung | Goal | Status |
|---|---|---|
| 0 | `epoll` TCP echo server | ✅ (you are here) |
| 1 | RESP protocol parser + `SET`/`GET` over a hash table | ⬜ |
| 2 | `DEL`, `EXISTS`, TTL / expiry | ⬜ |
| 3 | LRU eviction + memory cap | ⬜ |
| 4 | benchmark with real `redis-benchmark` | ⬜ |
| 5 | swap `epoll` → `io_uring`, measure the delta | ⬜ |
| 6 | persistence: append-only log + snapshot + recovery | ⬜ |
| 7 | thread-per-core / sharded, lock-free hot path | ⬜ |

See `docs/PRIMER.md §8` for what each rung teaches. Stop-anywhere: rung 4 is
already a benchmarkable portfolio piece.
