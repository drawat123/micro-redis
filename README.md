# micro-redis

A from-scratch, **Redis-protocol-compatible in-memory key–value server** written
in modern **C++20**. Real `redis-cli` and `redis-benchmark` talk to it unmodified.

Single-threaded **epoll** event loop · **RESP** protocol · **TTL** expiration ·
bounded **LRU** eviction · pipelining · robust non-blocking I/O.

> A systems-engineering learning project, built rung by rung — each a complete,
> working program with documented design reasoning (`docs/notes/`).

---

## Highlights

- **Epoll reactor**, edge-triggered and non-blocking — one thread serves many
  concurrent clients (no thread-per-connection).
- **RESP protocol**: a pure, unit-tested, binary-safe parser + a command
  dispatcher — `PING SET GET DEL EXISTS TTL EXPIRE DBSIZE`.
- **Key expiration (TTL)** — lazy (on access) *and* active (periodic sweep),
  using the monotonic `steady_clock`.
- **Bounded LRU eviction** — O(1) via an intrusive doubly-linked list + hash map;
  capacity set with `--maxkeys`.
- **Pipelining** with batched replies and a full **`EPOLLOUT` write buffer** — no
  data loss when replies exceed the socket send buffer.
- **Tested** (GoogleTest) and **AddressSanitizer-clean** under load.

## Performance

`redis-benchmark` vs. real Redis on the same Linux host (Release build):

| Workload | micro-redis | Redis |
|---|---|---|
| SET / GET, no pipeline | 297k / 280k rps | 325k / 346k |
| SET / GET, `-P 16` (pipelined) | **3.70M / 4.00M rps** | 1.85M / 3.33M |

> **Honest framing:** micro-redis exceeds Redis on the *pipelined microbenchmark*
> because it does **far less per command** (no persistence, replication, RESP3,
> or multiple data types) — it's *simpler*, not *better*. Non-pipelined, both are
> bound by the same syscall/round-trip costs and land in the same range.

The write-up in [`docs/notes/rung4.md`](docs/notes/rung4.md) covers the
optimization journey (a 19× Nagle/`TCP_NODELAY` fix → reply batching →
`EPOLLOUT`), the Debug-vs-Release **5×** lesson, and the two performance regimes
(syscall-bound vs. CPU-bound).

## Architecture

```
 client ──TCP──►  ┌──────────────────────────────────────────┐
                  │  epoll event loop  (single thread)        │
                  │    per-connection read buffer             │
                  │        │                                  │
                  │        ▼                                  │
                  │  RESP parser ─► dispatcher ─► store        │
                  │        ▲                     ├─ hash map + LRU list (O(1))
                  │        │                     └─ TTL: lazy + active sweep
                  │  batched replies ◄───────────┘            │
                  │  (EPOLLOUT write buffer)                  │
                  └──────────────────────────────────────────┘
```

## Build & run

Requires **Linux** (uses `epoll`). A Docker dev environment is included:

```bash
docker build -t microredis-dev .
docker run --rm -it -v "$PWD":/work -p 6380:6380 microredis-dev

# inside the container:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build                     # unit tests
./build/micro-redis --maxkeys 100000       # run (listens on :6380)
```

Then, from any Redis client:

```bash
redis-cli -p 6380 SET foo bar
redis-cli -p 6380 GET foo                  # "bar"
redis-cli -p 6380 SET k v EX 10            # 10-second TTL
redis-cli -p 6380 TTL k
redis-benchmark -p 6380 -t set,get -P 16 -q
```

## What it demonstrates

Non-blocking I/O and the reactor pattern · edge-triggered `epoll` · TCP internals
(Nagle / `TCP_NODELAY`, backpressure, `EPOLLOUT` write-buffering) · a hand-written
binary-safe protocol parser · an O(1) LRU cache · monotonic-clock expiration with
**dependency-injected time** for deterministic tests · RAII resource ownership
(move-only fd wrapper) · and **evidence-driven performance engineering**
(profile → fix → measure).

## Design notes

Each rung's reasoning, bugs, and lessons are documented:

- [`docs/PRIMER.md`](docs/PRIMER.md) — how Redis actually works
- [`docs/notes/`](docs/notes/) — per-rung design notes (rung 0–4)
- [`docs/DEBUGGING.md`](docs/DEBUGGING.md) — terminal gdb reference

## Roadmap

| Rung | Focus | |
|---|---|---|
| 0 | `epoll` TCP echo server | ✅ |
| 1 | RESP parser + `SET`/`GET` | ✅ |
| 2 | TTL / key expiration | ✅ |
| 3 | LRU eviction + memory cap | ✅ |
| 4 | benchmark + performance | ✅ |
| 5 | `io_uring` | ⬜ |
| 6 | AOF / snapshot persistence | ⬜ |
| 7 | sharded / lock-free hot path | ⬜ |

## Known limitations

Single-threaded; 10 KB max value size; no persistence yet (rungs 6–7). Details in
[`docs/notes/rung4.md`](docs/notes/rung4.md).

---

*Built in C++20. Toolchain: CMake, Ninja, GoogleTest, AddressSanitizer, gdb,
`redis-benchmark`.*
