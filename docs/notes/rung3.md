# Rung 3 — Notes (bounded store with LRU eviction)

> Study doc. Self-quiz: read a heading, answer it out loud, then check.

## What this rung is
- **One sentence:** the store has a capacity; when it's full, a new `SET` evicts
  the **least-recently-used** key to make room.
- **Extras:** `--maxkeys N` (configure the cap), `DBSIZE` (observe the count).

## The core data structure: O(1) LRU = list + hash map
```cpp
std::list<std::pair<std::string, Entry>> lru_;              // front = MRU, back = LRU
std::unordered_map<std::string, LruIterator>  index_;      // key -> node in the list
```
- **Lookup** by key → O(1) via `index_`.
- **Recency reorder** (mark most-recently-used) → move the node to the front.
- **Evict** → drop the back node.

## Why `std::list` specifically (the crux — expect this in an interview)
- **`splice` moves a node in O(1)** — `lru_.splice(lru_.begin(), lru_, it)` relinks
  the node to the front without copying it.
- **`std::list` iterators stay valid** across insert/splice/erase of *other*
  elements. That's essential: `index_` stores iterators into the list, and they
  must not be invalidated when you reorder. (A `vector`-based list would
  invalidate them.)
That combination — O(1) move + stable iterators — is what makes the whole thing O(1).

## Why the key lives in the list node ("why the pair")
Eviction drops `lru_.back()`, but you must **also** erase that key from `index_`
(or it's left holding a dangling iterator → use-after-free). To erase from
`index_` you need the **key**, and the only place to get it from the node being
evicted is the node itself:
```cpp
index_.erase(lru_.back().first);   // node -> key reverse lookup
lru_.pop_back();
```
So the key is deliberately stored **twice** (map key + node's `.first`): forward
lookup (key→node) *and* reverse lookup (node→key). No LRU avoids this.

## Capacity & eviction
- Cap by **key count** (`--maxkeys`). Real Redis caps by **bytes** (`maxmemory`) —
  counting bytes is the harder, later refinement.
- Guard: `if (max_capacity_ != 0 && lru_.size() >= max_capacity_) evict;`
  - `!= 0` so **0 = unlimited** (else `back()` on an empty list is UB).
  - `>=` (not `==`) so it self-corrects if size ever overshoots.
- Policy: **allkeys-LRU** (evict the LRU of any key). Variants exist
  (`volatile-lru` = only keys with a TTL) — didn't build them.

## Peek vs. access (a real API-design decision)
`getEntry(key, now, update_lru)`:
- **Promote** (`true`): `GET`/`SET` — a real use, so bump recency.
- **Peek** (`false`): `TTL`/`EXPIRE`/`DEL`/`EXISTS` — a query shouldn't silently
  reorder the cache it's inspecting. (Lazy expiration still fires on a peek — an
  expired key is cleaned up even when only checked.)
- Lesson: **a query shouldn't mutate what it queries** unless that's an explicit
  contract. (Before the split, `exists()` reordered recency, which made the LRU
  test have to account for the observer perturbing the observed — a smell.)
- Style: the `bool` is a mild "boolean trap"; an `enum class Access {Peek,
  Promote}` reads better at call sites.

## Bug I hit and what it taught me
- **`ttl()` condition inverted** — I dropped the `!`, so a key *with* an expiry
  returned `-1`, and a key *without* one dereferenced an empty `optional` (UB). →
  *Tiny logic typos in the happy path are exactly what unit tests catch — write
  the test and it fails instantly.* (It also passed the compile, because it's a
  logic error, not a type error.)

## Interview one-liners
- **O(1) LRU:** "Hash map for O(1) key→node lookup, doubly-linked list for O(1)
  recency reorder; `std::list::splice` moves a node in O(1) and keeps the map's
  stored iterators valid."
- **Node→key:** "The key is in the list node too, because eviction needs to map
  the evicted node back to its key to clean up the index."
- **Query purity:** "TTL/EXISTS peek without touching recency — a read shouldn't
  reorder the cache."

## Open questions / deferred
- **Cap by bytes** (`maxmemory`), not just key count.
- **Approximate LRU** — real Redis samples a few random keys and evicts the oldest
  of the sample, avoiding exact per-access bookkeeping at scale.
- **Eviction policies** (`volatile-lru`, `allkeys-lfu`, …).
