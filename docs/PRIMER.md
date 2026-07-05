# micro-redis — Redis Internals for Builders

> Read this **before** writing code. Goal: replace "Redis is a scary black box"
> with a clear mental model you could whiteboard. You do **not** need prior Redis
> knowledge — this doc gives you everything to build rungs 0–3.

---

## 1. The one-sentence mental model

> **Redis is a single TCP server that keeps a big hash table in RAM, speaks a
> tiny text protocol, and never blocks a thread waiting for the network.**

Everything else — expiry, eviction, persistence, replication — is a feature
bolted onto that core. If you internalize this sentence, you understand 80% of it.

```
                    ┌─────────────────────────────────────┐
   client ──TCP──►  │  event loop (epoll)                 │
   (redis-cli)      │     │                                │
                    │     ▼                                │
                    │  read bytes ─► parse RESP ─► execute │
                    │                                │     │
                    │                                ▼     │
                    │                   ┌──────────────────┐│
                    │                   │  hash table      ││   ← the "database"
                    │                   │  key → value     ││      is just a map
                    │                   └──────────────────┘│
                    │                                │     │
                    │     ◄──────── encode RESP ◄────┘     │
                    └─────────────────────────────────────┘
```

---

## 2. The three parts (you already know two of them)

**Part 1 — A network server.**
Accepts TCP connections, reads bytes off sockets. You did this in your Java
project (raw TCP → MQTT). Same concept in C++, using `epoll` to watch many
sockets at once without a thread per client.

**Part 2 — A protocol.**
The client sends `SET foo bar`; the server replies `OK`. Redis's wire format
(**RESP** — REdis Serialization Protocol) is a length-prefixed text encoding.
It's *simpler* than the Protobuf length-prefix framing you already implemented.
(Full spec in §5.)

**Part 3 — A hash table.**
`SET foo bar` is `map["foo"] = "bar"`. `GET foo` is `return map["foo"]`.
Start with `std::unordered_map<std::string, std::string>`. That's the database.

---

## 3. The counterintuitive core: Redis is (mostly) single-threaded

This is the fact that makes the project *approachable* — and it's interview gold.

**Real Redis executes commands on ONE thread.** No locks, no mutexes, no data
races on the hash table. So how does it serve 100k+ clients at once?

> It's not concurrency via threads — it's concurrency via **I/O multiplexing**.
> One thread loops: "which of my 10,000 sockets have data ready? Handle each,
> fast, in turn, then loop again." Because RAM operations take nanoseconds, one
> CPU core keeps up with enormous throughput. The bottleneck is the network, not
> the CPU — so adding threads wouldn't help the common case, and would add
> locking cost.

The engine of that loop is **`epoll`** (Linux): you hand the kernel a set of file
descriptors and ask "tell me which are ready to read/write." This is the
**reactor pattern**.

**What this means for you:** rungs 0–6 need **zero locking** — a huge
simplification. You only introduce threads/lock-free structures at rung 7, as a
deliberate "scale beyond one core" extension (that's your P3 concurrency flex,
done on purpose, not by accident).

Interview one-liner to bank: *"Redis is single-threaded for command execution;
its concurrency is I/O multiplexing via epoll, not thread-level parallelism —
which is why it needs no locks on the keyspace."*

---

## 4. The event loop, concretely

```
create listening socket, set non-blocking, bind, listen
epoll_create → epfd
epoll_ctl(ADD, listen_fd)          # watch the listener

loop forever:
    n = epoll_wait(epfd, events)   # BLOCKS until ≥1 fd is ready (this is fine)
    for each ready event:
        if fd == listen_fd:
            client_fd = accept()           # new connection
            set non-blocking
            epoll_ctl(ADD, client_fd)      # now watch this client too
        else:
            data = read(fd)                # never blocks (fd is non-blocking)
            if data closed: epoll_ctl(DEL, fd); close(fd)
            else:
                command = parse_resp(buffer)   # may need >1 read; buffer partial
                reply   = execute(command)     # touch the hash table
                write(fd, reply)
```

Two subtleties you'll hit (and learn from):
- **Partial reads:** TCP is a byte *stream*, not messages. One `read()` may give
  you half a command or two commands. You buffer per-connection and parse what's
  complete. (You met this exact "message boundary problem" in your Java TCP work.)
- **Non-blocking everything:** a blocking `read()`/`write()` on one slow client
  would freeze all others. Every socket is non-blocking; you react to readiness.

---

## 5. The RESP protocol (enough to implement rungs 1–3)

RESP encodes types by a **1-byte prefix**, and terminates lines with `\r\n` (CRLF).

| Type | Wire format | Example |
|---|---|---|
| Simple string | `+<text>\r\n` | `+OK\r\n` |
| Error | `-<msg>\r\n` | `-ERR unknown command\r\n` |
| Integer | `:<n>\r\n` | `:1000\r\n` |
| Bulk string | `$<len>\r\n<bytes>\r\n` | `$5\r\nhello\r\n` |
| Null bulk string | `$-1\r\n` | (used for GET on a missing key) |
| Array | `*<count>\r\n<elements...>` | see below |

**Key insight: clients send every command as an *array of bulk strings*.**

So `SET foo bar` arrives on the wire as:
```
*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
 │      │       │       │
 │      │       │       └─ 3rd element: "bar"
 │      │       └─ 2nd element: "foo"
 │      └─ 1st element: "SET"
 └─ array of 3 elements
```

Your parser's job: read `*N`, then read N bulk strings, yielding
`["SET","foo","bar"]`. Then `execute` switches on element 0.

Server replies:
- `SET` → `+OK\r\n`
- `GET foo` (hit) → `$3\r\nbar\r\n`
- `GET foo` (miss) → `$-1\r\n`
- `DEL foo` → `:1\r\n` (count deleted)
- unknown command → `-ERR unknown command\r\n`

That's the entire protocol surface for a working key-value server. Because it's
*real* RESP, `redis-cli` and `redis-benchmark` talk to your server unmodified.

---

## 6. Redis concepts → systems concepts (glossary)

| Redis term | What it really is |
|---|---|
| Keyspace | a hash table in RAM |
| RESP | a length-prefixed wire protocol |
| Event loop / `ae` | a reactor over `epoll` |
| TTL / expiry | a timestamp stored with the value + lazy/active deletion |
| Eviction (LRU/LFU) | a bounded cache replacement policy |
| AOF (append-only file) | a write-ahead log of commands |
| RDB snapshot | a serialized dump of the hash table |
| Replication | ship the command log to a follower |
| Cluster / sharding | partition keys across N instances by hash slot |

Notice how each maps to a general systems primitive you're studying in the
roadmap — that's not a coincidence, and it's why this project teaches the pillars.

---

## 7. Expiry & eviction (rungs 2–3) — the first "real" features

**TTL / expiry.** `SET foo bar EX 10` = "this key dies in 10s." Store an
expiry timestamp alongside the value. Two deletion strategies (Redis uses both):
- **Lazy:** on `GET`, if `now > expiry`, treat as missing and delete. Cheap, but
  dead keys linger if never read.
- **Active:** a periodic sweep samples random keys and evicts expired ones.

**Eviction (bounded memory).** When you cap memory, a full `SET` must evict
something. **LRU** (least-recently-used) is the classic: track access order,
drop the oldest. Real Redis approximates LRU (samples K keys, evicts the oldest
of the sample) to avoid the bookkeeping cost of exact LRU — a great tradeoff to
be able to explain.

---

## 8. The build ladder (recap — each rung is a complete program)

| Rung | Build | Redis concept learned | Roadmap pillar |
|---|---|---|---|
| 0 | `epoll` TCP echo server | sockets, event loop, non-blocking I/O | P4 |
| 1 | parse RESP, `SET`/`GET` over `unordered_map` | the protocol, request/response | P4, P1 |
| 2 | `DEL`, `EXISTS`, TTL/expiry | key expiration, lazy vs active delete | P1 |
| 3 | LRU eviction + memory cap | cache replacement policies | P7 |
| 4 | run **`redis-benchmark`** against it | ← your numbers story begins | P5 |
| 5 | swap `epoll` → **`io_uring`**, measure delta | modern async I/O | P4, P5 |
| 6 | AOF log + snapshot + crash recovery | durability, WAL | P2 |
| 7 | thread-per-core / sharded, lock-free hot path | concurrency at scale | P3 |

**Stop-anywhere rule:** rung 4 is already a benchmarkable flagship. Rungs 5–7 are
the L5 depth. Don't build ahead — learn each concept when its rung needs it.

---

## 9. What "done well" looks like (so you build for the interview)

- Every rung compiles clean under `-Wall -Wextra` and runs clean under **ASan/UBSan**
  (and **TSan** once threads appear at rung 7).
- Modern C++: RAII wrappers for fds/sockets (no raw `close()` scattered around),
  smart pointers where ownership is real, `std::string_view` to avoid copies in
  the parser, no `new`/`delete`.
- A `benchmarks/` folder with real numbers (throughput, p50/p99/p999) and a short
  writeup per milestone — *this* is what turns "I learned epoll" into "I measured it."
- A design doc (this file grows) recording each tradeoff and its rejected alternatives.

---

## 10. Before you type: environment

This project uses `epoll`, later `io_uring`, `/proc`, `perf`, and `valgrind` —
**all Linux-only**. You're on macOS/Apple Silicon, so you'll build inside a Linux
container (Docker, already installed). See **`TOOLCHAIN.md`** for the full
Visual-Studio → Linux-CLI crosswalk and the Docker dev setup. The scaffold step
sets this up for you.

---

*Living document — append tradeoffs and diagrams as the project grows.*
