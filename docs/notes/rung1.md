# Rung 1 — Notes (RESP command server: PING/SET/GET/DEL/EXISTS)

> Study doc. Best used as a self-quiz: read a heading, answer it out loud, then
> check against the text.

## What this rung is
- **One sentence:** the echo server becomes a real Redis-protocol key-value
  server — it parses RESP requests, executes commands against an in-memory store,
  and replies in RESP, so real `redis-cli`/`redis-benchmark` talk to it.
- **Pieces:** a pure RESP **parser** → a per-connection **read buffer** → a
  **dispatcher** (command → reply) → a **store** (the data).

## The RESP protocol
**Requests** (client → server) are always a RESP **array of bulk strings**:
```
SET foo bar → *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
```
**Replies** (server → client), by 1-byte prefix:
| Type | Wire | Used for |
|---|---|---|
| Simple string | `+OK\r\n` | SET, PING (`+PONG`) |
| Error | `-ERR msg\r\n` | unknown cmd / bad arity |
| Integer | `:1\r\n` | DEL count, EXISTS 0/1 |
| Bulk string | `$3\r\nbar\r\n` | GET hit |
| Nil | `$-1\r\n` | GET miss |

**Why length-prefixed?** So bulk data is **binary-safe** — a value can contain
any bytes, including `\r\n`. That's why the parser **jumps `len` bytes** rather
than searching for the next `\r\n` (a bug I had early on).

## The parser design (the core ideas)
- **Pure & stateless.** `parse_command(string_view) -> {status, args, consumed}`.
  No sockets, no memory between calls — which is exactly what makes it
  unit-testable. State (the accumulating buffer, the position) lives in the
  *connection*, not the parser.
- **Tri-state, not exceptions.** `Ok` / `Incomplete` / `Error`:
  - `Ok` → a full command; `consumed` = its byte length.
  - `Incomplete` → ran out of bytes; **consume nothing**, wait for more. *Not an
    error.* (Empty input is `Incomplete`, not a failure.)
  - `Error` → a real protocol violation (missing `*`, non-numeric length).
  A network-loop parser should **return status, never throw** — one error
  channel, no try/catch, no exception cost in the hot path.
- **`consumed` is how position is tracked.** The parser doesn't remember where it
  is; it parses one command from byte 0 of the view it's given and reports how
  many bytes that took. The caller advances past `consumed` and calls again.

## The streaming buffer loop (in the server)
TCP is a byte *stream* — one `read()` may deliver half a command or two-and-a-half.
So each connection owns a `std::string inbuf`, and the loop is:
```
read() bytes → conn.inbuf.append(...)
offset = 0
loop:
    r = parse_command(view(inbuf).substr(offset))
    if Incomplete: break            # wait for more bytes
    if Error:      close connection
    dispatch(r.args) + send reply   # USE args now (before we touch inbuf)
    offset += r.consumed
inbuf.erase(0, offset)              # drop consumed; keep the leftover
```
Getting this right is the crux of the rung. Key subtleties:
- **Dispatch before erase.** `args` are `string_view`s into `inbuf`; erasing (or
  appending, which can reallocate) would dangle them. Use them first.
- **`erase(0, offset)` keeps the partial leftover** so the next `read()` completes it.
- **Edge-triggered** ⇒ drain the socket (`read` until `EAGAIN`) each wakeup.

## The copy vs view boundary (the design decision)
- **Parsing → `string_view`** (zero-copy, transient — valid only until `inbuf`
  changes).
- **Store → owned `std::string`** (durable). `SET` **copies** the key/value out
  of the transient views into the store, because `inbuf` gets erased right after.

That line — *views for parsing, owned copies at the store* — is the whole
lifetime story of the rung.

## Concurrency model
**One thread, one shared `Store`, no locks.** Commands execute one at a time on
the event-loop thread; concurrency is I/O multiplexing (epoll), not threads.
That's the real Redis model — and why the keyspace needs no locking.

## Bugs I hit and what they taught me
- **`elementSize` reset inside the loop** → parser threw on every non-empty bulk
  string. → *Watch variable scope across loop iterations; a value needed next
  iteration must live outside the loop.*
- **`Incomplete` vs `Error` conflation.** Empty input threw (should be
  `Incomplete`); a malformed number returned `Incomplete` (should be `Error`,
  else the connection hangs forever waiting for bytes that can't fix it). → *Model
  "need more" and "malformed" as genuinely different outcomes.*
- **Two error channels.** I had both `throw` *and* a `ParseStatus::Error` that was
  never used. → *Pick one. For a network loop, return status; don't throw.*
- **Anonymous namespace swallowed `parse_command`.** Wrapping the whole file in
  `namespace { }` gave the *public* function internal linkage → link error from
  `server.cpp`. → *Anonymous namespace = internal linkage = file-private. Put
  private helpers there, never the public API (which must have external linkage).*
- **Incomplete rename.** Renamed `EchoServer→Server` but missed `main.cpp`. →
  *After renaming a symbol, `grep -rn OldName .` the whole tree.*

## Tools & techniques
- **GoogleTest:** `TEST(Suite,Case)`, `TEST_F(Fixture,Case)` (shared setup like a
  fresh `Store`), `EXPECT_*` (keep going) vs `ASSERT_*` (stop this test). Assert
  the **exact reply bytes** for the dispatcher.
- **`nm -C lib.a`** to debug link errors: **`t`** = defined, internal linkage;
  **`T`** = defined, external; **`U`** = referenced, undefined. `t parse_command`
  next to `U parse_command` = "it's here but hidden."
- **`redis-cli` / `redis-benchmark`** as real clients (can't use `nc` anymore —
  it sends inline text, not RESP arrays).
- **ASan under `redis-benchmark` load** — proved the `string_view`/buffer path is
  memory-safe under thousands of pipelined commands. *Tests pass ≠ correct; run
  the real thing under a sanitizer.*

## Interview one-liners
- **Streaming parser:** "The parser is a pure `bytes → (command, consumed)`
  function; the connection owns the buffer and advances by `consumed`, so
  partial and pipelined commands both just work."
- **Why no locks:** "Single-threaded command execution; concurrency is epoll I/O
  multiplexing, so the shared keyspace needs no synchronization."
- **Lifetime discipline:** "Parse with `string_view` for zero copies, but the
  store owns `std::string` — I copy at the boundary because the parse views point
  into a buffer I erase."

## Open questions / deferred to future rungs
- **Write path is fire-and-forget.** `send()` is unchecked; with edge-triggered
  sockets a full send buffer means a partial/`EAGAIN` write silently drops reply
  bytes. Real fix: a per-connection **write buffer + `EPOLLOUT`**. (Fine for small
  replies; matters for large `GET` under load.)
- **Store allocates per lookup.** `find(std::string(key))` heap-allocates a temp
  on every GET/DEL/EXISTS. Rung 4 (perf) fixes it with C++20 **heterogeneous
  lookup** (transparent hash/equal) to look up by `string_view`, zero-alloc.
- **No expiry/eviction yet** — that's rung 2 (TTL) and rung 3 (LRU).
