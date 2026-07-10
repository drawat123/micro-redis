# Rung 2 — Notes (TTL / key expiration)

> Study doc. Self-quiz: read a heading, answer it out loud, then check.

## What this rung is
- **One sentence:** keys can now expire — they carry an optional deadline, and
  the server removes them once it passes.
- **Commands:** `SET key val EX <secs>`, `TTL key`, `EXPIRE key secs`.

## The data model
Each value grows into an entry with an optional deadline:
```cpp
struct Entry {
    std::string value;
    std::optional<TimePoint> expires_at;   // nullopt = never expires
};
```

## `steady_clock` vs `system_clock` (know the difference)
- **`system_clock`** = wall-clock time. Can **jump** — backward on NTP sync, DST,
  a user setting the clock. Use it for "what time is it in the real world."
- **`steady_clock`** = monotonic; only ever moves forward at a steady rate. Use it
  for **deadlines, timeouts, durations** — anything where a jump would corrupt
  your logic. Expiry deadlines use `steady_clock`.
- Interview answer: *"I used steady_clock for TTLs because it's monotonic — an
  NTP adjustment or DST change can't make a key expire early or never."*

## Two expiration strategies (implement both, like Redis)
- **Lazy:** on access (`GET`/`EXISTS`/`TTL`/`DEL`), if `now >= expires_at`, treat
  the key as absent **and delete it** (`getEntry` does this). Cheap — you only pay
  for keys you touch. Downside: a key nobody ever reads lingers forever.
- **Active:** a periodic `sweep()` scans the map and drops expired keys, so memory
  doesn't leak from never-accessed dead keys.
- Together they cover both cases. The event loop runs `sweep` on a timer: give
  `epoll_wait` a finite timeout (200 ms) so it wakes periodically even with no I/O.

## 🔑 Injectable clock — the testability lesson
Instead of calling `steady_clock::now()` *inside* the store, **pass `now` in** as a
parameter (threaded `dispatch(args, store, now)` → `store.get(key, now)`). This is
**dependency injection**: push the nondeterministic dependency (the clock) to the
edge so the core logic is pure.

Payoff: expiry tests are **deterministic and instant** — no `sleep`:
```cpp
store.set("k", "v", 10s, now_);
EXPECT_NE(store.get("k", now_ + 9s),  nullptr);   // alive
EXPECT_EQ(store.get("k", now_ + 10s), nullptr);   // expired — fabricated future
```
Same instinct that made the parser pure. Strong interview signal: *"how would you
test time-based expiry without flaky sleeps?" → inject the clock.*

## TTL semantics (three distinct cases)
`TTL key` → **`-2`** (no such key) · **`-1`** (exists, no expiry) · **remaining
seconds** otherwise. A plain `SET` (no `EX`) **clears** any existing TTL (resets
`expires_at` to nullopt).

## Bugs I hit and what they taught me
- **`expire()` fell off the end of a non-void function** → `-Wreturn-type` warning
  → **undefined behavior** (returned garbage, so `EXPIRE` replied `:1`/`:0`
  randomly). → *Never ignore `-Wreturn-type`; it's almost always a real bug.
  Consider `-Werror=return-type` so it can't slip past a passing build.*
- **`EXISTS` ignored expiry.** It was still the rung-1 `const`, no-`now` version, so
  an expired-but-unswept key reported `1`. → *Every access path must go through the
  same lazy-expiration check (`getEntry`) — consistency across `get`/`exists`/`del`.*

## Idioms picked up
- **Erase-during-iteration:** `for (it = m.begin(); it != m.end();) { if (dead)
  it = m.erase(it); else ++it; }` — `erase` returns the next valid iterator; never
  `++it` on an erased one.
- **`std::optional<TimePoint>`** to model "maybe a deadline" cleanly.

## Interview one-liners
- **steady vs system clock** (see above).
- **Lazy + active:** *"Lazy expiration on access keeps reads correct; a periodic
  active sweep stops never-read dead keys from leaking memory."*
- **Injectable clock:** *"Pass `now` in so time logic is deterministically testable."*

## Open questions / deferred
- **`TTL` truncates** (`duration_cast` rounds toward zero); real Redis rounds up
  remaining seconds. Cosmetic.
- **Active expiration is a full O(N) scan** every tick. Real Redis samples a random
  subset to bound the work — a scalability improvement for later.
- **`PX`/`EXAT`/`PERSIST`/`SETEX`** not implemented — scope kept to `EX`/`TTL`/`EXPIRE`.
