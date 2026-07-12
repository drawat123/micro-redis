# micro-redis — Study Q&A Log

> A running log of questions and explanations while studying the project.
> Newest entries at the bottom.

---

## Q1 — "I'm not clear on some epoll commands and a few other things."

### epoll in one paragraph
`epoll` is the kernel's answer to *"I have thousands of sockets — tell me which
ones are ready right now, efficiently."* You create an epoll instance, register
the fds you care about, then block on one call that hands back only the ready
ones. It scales far better than the older `select`/`poll`, which rescan every fd
on every call.

### The 3 syscalls (the whole API)

**1. `epoll_create1(0)`** → returns an **epoll instance** (itself an fd) that holds
your "interest list" of watched fds.
```cpp
Fd epoll(::epoll_create1(0));
```

**2. `epoll_ctl(epfd, op, fd, &event)`** — manage the interest list:

| op | meaning | in server.cpp |
|---|---|---|
| `EPOLL_CTL_ADD` | start watching `fd` | listener + each accepted client |
| `EPOLL_CTL_MOD` | change *what* you watch for an fd | `arm_write` toggling `EPOLLOUT` |
| `EPOLL_CTL_DEL` | stop watching `fd` | on connection close |

The `struct epoll_event` has two fields:
```cpp
epoll_event ev{};
ev.events  = EPOLLIN | EPOLLET;   // WHAT to watch for (a bitmask)
ev.data.fd = fd;                  // a tag YOU choose to identify this fd later
```

**3. `epoll_wait(epfd, events[], maxevents, timeout)`** → **blocks** until ≥1 fd is
ready (or `timeout` ms pass); fills `events[]`; returns the count.
```cpp
int n = epoll_wait(epoll.get(), ready.data(), 64, 200);
// timeout: -1 = forever, 0 = don't block, 200 = up to 200ms
```
We use 200 ms so the loop wakes ~5×/sec even with no traffic — to run
`store_.sweep()` (active TTL expiration).

### Event flags (what you watch / what you get back)

| Flag | Meaning |
|---|---|
| `EPOLLIN` | readable — data to read, a new connection (listener), or EOF |
| `EPOLLOUT` | writable — the send buffer has room |
| `EPOLLERR` | an error occurred (always reported) |
| `EPOLLHUP` | peer hung up / closed (always reported) |
| `EPOLLET` | **modifier**: edge-triggered mode |

After `epoll_wait`:
```cpp
int fd = ready[i].data.fd;                 // which fd fired (the tag we set)
if (ready[i].events & EPOLLIN)  { ... }    // readable
if (ready[i].events & EPOLLOUT) { ... }    // writable
```

### Level- vs edge-triggered (the concept to nail)
- **Level-triggered (default):** `epoll_wait` keeps reporting an fd ready *as long
  as the condition holds* (unread data still there). Forgiving. Our **listener**
  uses this.
- **Edge-triggered (`EPOLLET`):** reports an fd only when its state **changes**
  (new data arrives). If you don't fully drain it, you won't be told again — so
  you must loop `read`/`accept`/`send` **until `EAGAIN`**. Our **clients** use
  this, which is *why* the read loop and `flush_output` loop until `EAGAIN`.

---

## Q2 — "Walk me through the EPOLLOUT write-buffer state machine."

### The problem it solves
A non-blocking `send()` may write **fewer bytes than you asked** (or return `-1`
with `EAGAIN`) when the kernel's socket send buffer is full. If you assume it
sent everything and drop the rest, you **lose reply data**. So you must: keep the
unsent tail, and get told when the socket can take more.

### The key tension (why you can't just always watch `EPOLLOUT`)
A socket is **almost always writable** (the send buffer usually has room). So if
you kept `EPOLLOUT` armed permanently, `epoll_wait` would return "writable!" on
*every* call → a CPU-burning busy loop. **Rule: watch `EPOLLOUT` only while there
is unsent data.**

### The state machine — two states per connection

```
        outbuf empty                         outbuf has unsent bytes
   ┌────────────────────┐                  ┌───────────────────────────┐
   │   STATE A          │   partial send   │   STATE B                 │
   │   EPOLLOUT OFF     │ ───────────────► │   EPOLLOUT ON             │
   │   (EPOLLIN|ET)     │                  │   (EPOLLIN|ET|EPOLLOUT)   │
   │                    │ ◄─────────────── │                           │
   └────────────────────┘   fully drained  └───────────────────────────┘
```
`Connection.watching_write` is the bool that remembers which state we're in (so
we don't issue a redundant `epoll_ctl` when nothing changed).

### The two helpers

**`arm_write(epoll_fd, fd, conn)`** — sync the EPOLLOUT bit to reality:
```cpp
bool want = !conn.outbuf.empty();          // do we still have data to send?
if (want == conn.watching_write) return;   // already in the right state — no syscall
ev.events = EPOLLIN | EPOLLET | (want ? EPOLLOUT : 0u);
epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
conn.watching_write = want;
```

**`flush_output(epoll_fd, fd, conn)`** — send as much as the kernel will take:
```cpp
while (!conn.outbuf.empty()) {
    ssize_t n = send(fd, outbuf.data(), outbuf.size(), MSG_NOSIGNAL);
    if (n > 0)                     { outbuf.erase(0, n); continue; } // sent some, keep going
    if (n < 0 && errno == EINTR)   { continue; }                    // retry
    if (n < 0 && errno == EAGAIN)  { break; }                       // buffer full → stop
    return false;                                                   // hard error → close
}
arm_write(epoll_fd, fd, conn);   // empty → EPOLLOUT off; leftover → EPOLLOUT on
return true;
```

### The lifecycle (transitions)
1. During `EPOLLIN` handling, replies are appended to `conn.outbuf`.
2. We call `flush_output`:
   - **all sent** → `outbuf` empty → `arm_write` turns `EPOLLOUT` **off** (State A).
   - **partial / `EAGAIN`** → keep the remainder → `arm_write` turns `EPOLLOUT`
     **on** (State B).
3. Later the client reads, the send buffer drains → the socket transitions to
   writable → (edge-triggered) `epoll_wait` returns **`EPOLLOUT`** for that fd.
4. On that `EPOLLOUT` wakeup we call `flush_output` again → send more → either
   drain (→ State A, `EPOLLOUT` off) or hit `EAGAIN` again (stay in State B).

### Edge-triggered subtlety
With `EPOLLET`, `EPOLLOUT` fires **once** on the transition to writable. That's
why `flush_output` sends in a loop until `EAGAIN` — it fully consumes the current
writable "edge." If it can't finish, it stops at `EAGAIN` and stays armed; the
*next* time the buffer drains, a new edge produces a new `EPOLLOUT`.

### Concrete trace — a 2.7 MB reply (300 pipelined large GETs)
1. Client sends 300 pipelined `GET`s → `EPOLLIN`.
2. Server reads, dispatches, appends ~2.7 MB of replies to `outbuf`.
3. `flush_output`: `send()` writes ~one send-buffer's worth (say ~200 KB), then
   `EAGAIN`. ~2.5 MB remains → `arm_write` → **EPOLLOUT on**.
4. Client reads the 200 KB → send buffer drains → writable edge → `EPOLLOUT`.
5. On `EPOLLOUT`: `flush_output` sends another chunk, `EAGAIN`, still armed.
6. Steps 4–5 repeat (~13×) until `outbuf` is empty → **EPOLLOUT off**.
7. All 2,702,700 bytes arrive intact. ✅ (Verified in testing.)

### Why this matters
Before this, the code did `send(...); outbuf.clear();` — assuming the whole
buffer went out. That silently dropped bytes whenever a reply exceeded the send
buffer (large `GET`s, deep pipelines) → clients hang waiting for missing data.
The state machine makes the write path **correct under backpressure** — which is
exactly what real servers must do, and what a lot of toy servers skip.
