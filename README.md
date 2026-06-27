# mini-redis

A Redis-inspired in-memory key-value store built from scratch in C++17.

Implements a TCP server that accepts client connections and processes commands — the same architecture used by real Redis. Built to understand how caching systems work at the systems level.

---

## Features

- **TCP client-server architecture** — server and client are separate programs communicating over sockets
- **Core commands** — `SET`, `GET`, `DEL`, `EXISTS`, `EXPIRE`, `PING`, `DBSIZE`
- **TTL / key expiry** — keys auto-delete after a set time (`SET key value EX 30`); uses both lazy expiry (on access) and active expiry (background thread every 100ms)
- **LRU eviction** — when the store hits max capacity, the least recently used key is automatically evicted (O(1) using hash map + doubly linked list)
- **Persistence** — store is snapshotted to `dump.rdb` on shutdown and reloaded on startup so data survives restarts

---

## Architecture

```
┌─────────────────────┐        TCP (port 6379)       ┌─────────────────────┐
│   mini-redis-client │  ──── "SET name rahul\n" ──→  │   mini-redis-server │
│   (client.cpp)      │  ←──── "+OK\r\n" ──────────   │   (server.cpp)      │
└─────────────────────┘                               └──────────┬──────────┘
                                                                 │
                                          ┌──────────────────────┼───────────────────┐
                                          │                      │                   │
                                   ┌──────▼──────┐    ┌─────────▼──────┐   ┌────────▼───────┐
                                   │  parser.cpp │    │   store.cpp    │   │persistence.cpp │
                                   │  splits raw │    │ hash map + LRU │   │ save/load      │
                                   │  text into  │    │ doubly linked  │   │ dump.rdb       │
                                   │  commands   │    │ list + TTL     │   └────────────────┘
                                   └─────────────┘    └────────────────┘
```

**Request flow:**
1. Client sends a text command over TCP (`SET name rahul\n`)
2. Server reads it, parser splits it into command + args
3. Command is executed against the in-memory store
4. Response is sent back (`+OK\r\n`)

---

## Project Structure

```
mini-redis/
├── include/
│   ├── store.h         → Store class declaration (hash map + LRU structures)
│   ├── parser.h        → Command struct + parse() declaration
│   └── persistence.h   → save_snapshot() + load_snapshot() declarations
├── src/
│   ├── server.cpp      → TCP server, accept loop, command execution
│   ├── store.cpp       → In-memory store: hash map, TTL, LRU eviction
│   ├── parser.cpp      → Parses raw text commands into Command structs
│   ├── persistence.cpp → Snapshot store to disk, reload on startup
│   └── client.cpp      → Interactive CLI client
└── Makefile
```

---

## Build & Run

**Requirements:** g++ (C++17), make, Linux or WSL2

```bash
# Clone and build
git clone https://github.com/YOUR_USERNAME/mini-redis.git
cd mini-redis
make
```

**Terminal 1 — start the server:**
```bash
./mini-redis-server
```

**Terminal 2 — connect the client:**
```bash
./mini-redis-client
```

---

## Usage

```
mini-redis> SET name rahul
OK

mini-redis> GET name
"rahul"

mini-redis> EXISTS name
(integer) 1

mini-redis> SET session abc EX 10
OK
# key 'session' will auto-delete after 10 seconds

mini-redis> DEL name
(integer) 1

mini-redis> GET name
(nil)

mini-redis> DBSIZE
(integer) 1

mini-redis> PING
PONG

mini-redis> QUIT
BYE
```

Press `Ctrl+C` on the server to save a snapshot and exit cleanly.

---

## Key Design Decisions

### LRU Eviction — O(1) with hash map + doubly linked list

Evicting the least recently used key requires two things simultaneously:
- O(1) lookup: "is key X in the store?" → `unordered_map`
- O(1) reorder: "move key X to front of usage list" → `std::list` (doubly linked list)

A singly linked list would require O(n) to find a node's predecessor for removal. By storing an iterator (pointer to node) in a separate hash map, we can jump directly to any node and detach it in O(1).

```
Hash map:          Doubly linked list (front = most recently used):
"D" → iter ──→    [D] ↔ [A] ↔ [C]   ← B was evicted (was at back)
"A" → iter ──→         ↑
"C" → iter         stored iterator = O(1) access to any node
```

Every `GET` and `SET` calls `touch(key)` which moves that key to the front.
When the store exceeds capacity, the key at the back is evicted.

### Lazy TTL Expiry

Instead of only checking expiry when a key is accessed (lazy expiry), the store also runs a **background thread** (`active_expiry_loop`) that sweeps all keys every 100ms and deletes expired ones. This is the same two-pronged approach used by real Redis:

- **Lazy expiry** — checked on every GET/EXISTS/EXPIRE call. O(1), zero overhead.
- **Active expiry** — background thread sweeps every 100ms. Cleans up expired keys that are never accessed again, preventing memory waste.

The background thread holds a `std::mutex` lock during the sweep so it never conflicts with concurrent SET/GET operations. An `std::atomic<bool> running_` flag signals the thread to stop cleanly when the store is destroyed.

### Persistence Format

On shutdown (`Ctrl+C`), the store is written to `dump.rdb` as plain text:

```
KEY name rahul
EXPKEY session abc 1718000000000
```

On startup, this file is read back in. Keys with expired timestamps are skipped during load.

---

## Commands Reference

| Command | Description | Example |
|---|---|---|
| `SET key value` | Store a value | `SET name rahul` |
| `SET key value EX n` | Store with expiry in seconds | `SET token abc EX 60` |
| `GET key` | Retrieve a value | `GET name` |
| `DEL key` | Delete a key | `DEL name` |
| `EXISTS key` | Check if key exists (1/0) | `EXISTS name` |
| `EXPIRE key n` | Set TTL on existing key | `EXPIRE name 30` |
| `DBSIZE` | Number of keys in store | `DBSIZE` |
| `PING` | Health check | `PING` |
| `QUIT` | Disconnect client | `QUIT` |

---

## Future Improvements

- Multi-client concurrency (thread per connection or epoll-based event loop)
- Append-only file (AOF) logging for stronger durability guarantees
- `INCR` / `DECR` commands for atomic integer operations
- Benchmarking tool to measure throughput (ops/sec)
- Probabilistic active expiry (sample 20 random keys per sweep instead of full O(n) scan)

---

## What I Learned

- How TCP sockets work at the system call level (`socket`, `bind`, `listen`, `accept`, `recv`, `send`)
- The client-server architecture pattern underlying all networked systems
- How caches handle bounded memory — LRU is the industry standard eviction policy
- Why O(1) LRU requires two data structures, not one
- How real databases achieve durability through snapshots and write-ahead logs