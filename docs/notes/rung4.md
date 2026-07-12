# Rung 4 — Notes (benchmark + performance)

> Study doc + the flagship perf writeup. Self-quiz on the headings; the numbers
> are the interview artifact.

## What this rung is
Not a feature — a **measure → diagnose → fix → measure** loop. Point
`redis-benchmark` at the server, find bottlenecks, fix them with evidence, and
report honestly.

**Environment:** a real Linux box (Debian VM), micro-redis and real Redis on the
same host. (Docker-on-Mac is *not* a benchmark box — virtualized network, no
PMU; use it only for correctness, never for numbers.)

---

## Headline result (Release build)

| Workload | micro-redis | real Redis | note |
|---|---|---|---|
| SET, no pipeline | 297k rps | 325k | ≈ match |
| GET, no pipeline | 280k rps | 346k | ≈ match |
| **SET, -P 16** | **3.70M rps** | 1.85M | 2× Redis |
| **GET, -P 16** | **4.00M rps** | 3.33M | 1.2× Redis |

**Honest framing (say this, don't skip it):** micro-redis exceeds Redis on the
*pipelined microbenchmark* because it does **far less per command** — no
persistence, no replication, one data type, simpler dispatch, no RESP3/ACLs.
It's *simpler*, not *better*. Where we do comparable work (non-pipelined) we're
both bound by the same syscall/round-trip physics and land in the same range.

---

## The optimization journey (pipelined `-P 16` SET, Debug builds)

Each fix was found by measuring, then diagnosing the bottleneck:

| Stage | -P 16 SET | What changed |
|---|---|---|
| Baseline | **18k** | pipelining was *slower* than single-command — a red flag |
| + `TCP_NODELAY` | 352k | disabled Nagle's algorithm (~19×) |
| + reply batching | 689k | one `send()` per read, not per command |
| + EPOLLOUT write buffer | 689k | correctness (no throughput change for small replies) |
| **Release build** | **3.70M** | `-O2` unlocked the CPU-bound pipelined path (~5×) |

### 1. Nagle's algorithm (`TCP_NODELAY`)
**Symptom:** `-P 16` was 19× *slower* than `-P 1` — impossible for a healthy
server. **Cause:** Nagle's algorithm holds a small TCP segment while a previous
one is unacknowledged (to avoid tiny packets). Pipelining sends many small
replies back-to-back → each stalls waiting for the client's (delayed) ACK.
Single request/reply never triggers it (no unacked data when you reply).
**Fix:** `setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ...)` per connection — what
real Redis does.

### 2. Reply batching
`send()` was called once *per command* → under `-P 16`, 16 syscalls per batch.
**Fix:** append each reply to a per-connection `outbuf`, `send()` once per
`read()`. Cut ~17 syscalls/batch to ~2. This is what makes pipelining actually
*boost* throughput instead of just breaking even.

### 3. EPOLLOUT write buffer (correctness, not speed)
A single `send()` can write **fewer bytes than asked** when the kernel send
buffer is full. The naive "send all, clear" dropped the remainder → data loss on
large replies. **Fix:** the EPOLLOUT state machine —
- send until the buffer empties (→ disarm EPOLLOUT) or `EAGAIN` (→ keep the
  remainder, **arm** EPOLLOUT);
- on the next `EPOLLOUT` wakeup, send the rest.
Watch EPOLLOUT *only* while there's queued output — a socket is almost always
writable, so leaving it armed would busy-loop. Verified: a 2.7 MB reply (300
pipelined large GETs) round-trips byte-for-byte.

### 4. Heterogeneous lookup (C++20)
`getEntry` did `index_.find(std::string(key))` — a temp allocation per lookup.
A **transparent** hash (`using is_transparent = void;`) + `std::equal_to<>`
lets `find(string_view)` work with no allocation. **Honest result:** the
standalone impact was **within run-to-run noise** on this rig — kept it for
correctness/idiom, did **not** claim a speedup I couldn't measure. (Heterogeneous
`erase(key)` is C++23; we erase by iterator, so we're fine.)

---

## The two big methodology lessons

**1. Benchmark an *optimized* build.** CMake defaults to `Debug` (`-g`, no `-O`).
Switching to `Release` changed pipelined throughput **5×** (714k → 3.7M). Every
Debug number was misleading. *Always* measure `-DCMAKE_BUILD_TYPE=Release`.

**2. Two performance regimes — know which you're in:**
- **Non-pipelined = latency / syscall-bound.** 2 syscalls + a round-trip per
  command dominate; CPU optimization (`-O2`) barely moves it → we sit near Redis.
- **Pipelined = CPU / throughput-bound.** Syscalls amortized across 16 commands,
  so the bottleneck is CPU work per command → `-O2` moved it 5×.
Being able to explain *why* an optimization helped one and not the other is the
senior signal.

**3. VM noise is large.** Single runs swung 10–30%. Run each benchmark 3–5×, take
the median/best; a lone before/after proves nothing.

---

## Tools
- **`redis-benchmark`** — the load generator. `-P N` = pipeline depth; `-d N` =
  value size; rps = throughput (higher better); p50/p99 = latency percentiles.
- **`perf`** — didn't work in this VMware VM (hardware PMU counters `<not
  supported>`), and must be run *under load*, not on an idle server. For real
  profiling you'd want bare metal or a VM with PMU passthrough.
- **`strace -c -f`** — count syscalls under load; good for proving the
  reply-batching win (writes-per-batch drops).

## Interview one-liners
- **Nagle:** "Pipelined throughput was 19× below single-command; I traced it to
  Nagle's algorithm stalling back-to-back small replies against delayed ACKs, and
  fixed it with `TCP_NODELAY`."
- **Regimes:** "Non-pipelined is syscall/round-trip-bound so `-O2` didn't help;
  pipelined is CPU-bound so it moved 5×."
- **Honesty:** "I beat Redis on the pipelined microbenchmark, but only because my
  server does a fraction of the work per command — it's simpler, not better."
- **Measure, don't assume:** "The heterogeneous-lookup optimization was within my
  noise floor, so I kept it for correctness without claiming a speedup."

## Known limitations (documented, not hidden)
- **10 KB value cap** — `kMaxBulkLength = 10000` rejects larger bulk strings and
  hard-closes the connection (RST). Real Redis allows up to 512 MB. Should raise
  it and/or return a graceful `-ERR`.
- **`outbuf.erase(0, n)` is O(n²)** across many partial sends of a large reply —
  a send *offset* would make it O(n).
- **Active expiration is an O(N) full scan** each tick (Redis samples randomly).

## Deferred
- **Rung 5: io_uring** — replace the epoll reactor with io_uring; compare.
- Further per-command allocation cuts (reply formatting into `outbuf`, avoid
  `to_upper` allocation, small-vector for parsed args).
