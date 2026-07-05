# Rung 0 — Notes (epoll multi-client echo server)

> Study doc. Best used as a self-quiz: read a heading, answer it out loud, then
> check against the text.

## What this rung is
- **One sentence:** a single-threaded, non-blocking TCP echo server that uses an
  `epoll` event loop to serve many concurrent clients, echoing back whatever each
  one sends.
- **Deliberately deferred to rung 1+:** no protocol/parsing, no key-value store,
  no per-connection buffering, no RAII fd wrapper, no write-buffering (`EPOLLOUT`),
  no unit tests.

## Concepts

**File descriptor / socket.** A file descriptor (fd) is a small non-negative
integer the kernel hands you to refer to an open I/O resource. A *socket* is an
fd representing a network endpoint — you `read()`/`write()` it like a file, but
it speaks TCP/IP under the hood.

**Blocking vs non-blocking I/O.** By default `read()`/`accept()` **block** — the
thread sleeps until data or a connection arrives. With `O_NONBLOCK` they return
immediately: `-1` with `errno == EAGAIN` if nothing's ready. This matters because
a single thread can't serve many clients if it falls asleep inside one client's
`read()`.

**The event loop (reactor pattern).** Register your fds with the kernel, then
loop: ask "which are ready right now?" (`epoll_wait`) and handle each — accept a
new connection if the *listener* is ready, or read+echo if a *client* is ready.
You react to readiness instead of waiting on any single fd.

**epoll.** Solves "watch thousands of fds efficiently."
- `epoll_create1` — create an epoll instance (itself an fd).
- `epoll_ctl` — add / modify / remove fds in the interest set.
- `epoll_wait` — block until ≥1 registered fd is ready; returns the ready set.
Scales far better than `select`/`poll`, which rescan the whole fd list every call.

**Level-triggered vs edge-triggered (`EPOLLET`).**
- **Level-triggered** (default): epoll keeps notifying you as long as unread data
  remains — forgiving, one read per event is fine.
- **Edge-triggered**: notifies only on the *transition* to ready (once, when new
  data arrives). You **must drain** — `read()` in a loop until `EAGAIN` — or you'll
  strand data and never be told again.
- I used ET for clients, which is why the read loop runs until `EAGAIN`.

**EAGAIN / EWOULDBLOCK.** On a non-blocking socket this means "nothing available
right now" — the normal signal that you've drained all ready data, **not** an
error. Only *other* `errno` values are real failures.

**C10K.** The challenge of handling ~10,000+ concurrent connections on one box.
Thread-per-connection collapses under memory + context-switch cost. I felt it in
Stage 2: with one blocking thread stuck in `read()` on client #1, client #2
froze. The fix is non-blocking sockets + epoll — one thread, many clients.

## Bugs I hit and what they taught me

**Second client froze (Stage 2).** `accept()` ran once, then the thread blocked
forever in `read()` on client #1 and never returned to accept client #2. One
thread + blocking calls = one client at a time. → *Blocking I/O doesn't scale;
you need the event loop.*

**Stack buffer overflow.** I read into `char buffer[1024]` with
`read(fd, buffer, sizeof(buffer))` (up to 1024 bytes), then did
`buffer[bytes_read] = '\0'`. When `bytes_read == 1024` that writes `buffer[1024]`
— one past the end. Every manual test passed because I never typed a 1024-byte
line; the bug was latent and input-dependent. **AddressSanitizer caught it
instantly** on a 4000-byte payload. Fix: stop null-terminating — echo/log with an
explicit length (`std::cout.write(buffer, bytes_read)`). → *`read()` returns a
byte count, not a C-string. And "tests pass" ≠ correct — sanitizers find latent
UB.*

**SIGPIPE.** Writing to a socket whose peer has already closed raises `SIGPIPE`,
whose **default action terminates the process** — silently. Fatal for a server.
Fix: `send(..., MSG_NOSIGNAL)` (or `signal(SIGPIPE, SIG_IGN)` once at startup).
→ *A client disconnecting must never be able to kill the server.*

**SO_REUSEADDR / TIME_WAIT.** After a server closes, the port lingers in
`TIME_WAIT` (~60s); a quick restart's `bind()` fails with `EADDRINUSE`. Setting
`SO_REUSEADDR` before `bind()` lets you rebind immediately. → *Standard server
startup hygiene.*

## Tools I learned this rung
- **Docker dev container** — a reproducible Linux build/run environment (epoll,
  io_uring, valgrind are Linux-only; I edit on macOS, build in the container).
- **CMake** — describes the project; generates the real build (Ninja/Make).
- **clangd** — Clang-based language server: completion, go-to-def, live
  diagnostics; reads `compile_commands.json`.
- **gdb** — debugger: breakpoints, stepping, backtraces, post-mortem core dumps.
- **AddressSanitizer** — compiler instrumentation that catches memory bugs
  (overflows, use-after-free, leaks) at runtime with a stack trace.

## Interview one-liners
- **Redis concurrency:** "Redis executes commands on a single thread; its
  concurrency is I/O multiplexing via epoll, not thread-level parallelism — which
  is why the keyspace needs no locks."
- **Why sanitizers:** "Passing tests only covers the paths you exercised — a
  latent 1024-byte off-by-one passed all my manual tests, and ASan caught it in
  one run. So I build with ASan/UBSan during development."
- **Edge-triggered epoll:** "ET fires once when a socket becomes ready, so you
  must drain it (read until EAGAIN) or you strand data; level-triggered keeps
  reminding you."

## Open questions / deferred to rung 1
- **RAII `Fd` wrapper** — ties `close()` to object lifetime, eliminating the whole
  class of "forgot to close an fd on this error path" leaks and double-closes.
- **Edge-triggered + partial `write()`** — with `EPOLLET`, if the kernel send
  buffer is full, `write()` sends fewer bytes (or `EAGAIN`) and you won't be
  re-notified, so the remainder is silently lost. Real fix: buffer unsent bytes
  per connection and register `EPOLLOUT` to finish sending when writable.
- **Per-connection read buffers** — TCP is a byte stream, so once there's a
  protocol, one `read()` may hold half a command or two commands. A single shared
  buffer can't track each client's partial command; each connection needs its own.

## Design decision (rung 1 preview)
**RESP parser: copy strings out, or return `string_view`s into the buffer?**
- `string_view` = zero-copy, fast, but the parsed command is only valid while the
  connection's buffer is untouched (lifetime hazard).
- `std::string` copies = safe and simple, but allocates per command.
- **My plan:** start with copies (correctness first, easy to reason about), then
  optimize the hot path to `string_view` once the buffer lifetimes are clear and
  measured. *(Confirm during rung 1.)*
