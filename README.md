<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++17">
  <img src="https://img.shields.io/badge/CMake-3.16+-064F8C?style=for-the-badge&logo=cmake&logoColor=white" alt="CMake">
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey?style=for-the-badge" alt="Platform">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="License">
</p>

<h1 align="center">⚡ ApexMatch</h1>

<p align="center">
  <strong>A High-Concurrency Order Matching Engine built in C++17</strong>
</p>

<p align="center">
  Demonstrating expertise in <em>multi-threading</em>, <em>granular synchronization</em>,<br/>
  <em>complex data structures</em> (Heaps / Priority Queues), and <em>systems-level design</em>.
</p>

---

## 🏗️ Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│                      TCP Clients                             │
│              (Telnet / Netcat / Trading Bots)                │
└──────────────┬───────────────────────────────┬───────────────┘
               │          TCP/IP               │
┌──────────────▼───────────────────────────────▼───────────────┐
│                    TCP Server (Port 8080)                     │
│              Accept Loop → Thread Pool Dispatch              │
├──────────────────────────────────────────────────────────────┤
│                   ┌─────────────────────┐                    │
│                   │   Connection Pool   │                    │
│                   │  (Fixed Thread Pool)│                    │
│                   │  ┌──┐ ┌──┐ ┌──┐ ┌──┐│                   │
│                   │  │W1│ │W2│ │W3│ │W4││                   │
│                   │  └──┘ └──┘ └──┘ └──┘│                   │
│                   └─────────┬───────────┘                    │
├─────────────────────────────▼────────────────────────────────┤
│                                                              │
│                  ┌───────────────────┐                        │
│   Submit ───────>│  Incoming Queue   │                        │
│                  │  (mutex + cv)     │                        │
│                  └─────────┬─────────┘                        │
│                            │ drain                            │
│                  ┌─────────▼─────────┐                        │
│                  │  Matching Engine   │  ◄── Dedicated Thread │
│                  │   (Background)     │                        │
│                  ├───────────────────┤                        │
│                  │ ┌──────┐ ┌──────┐ │                        │
│                  │ │ BIDS │ │ ASKS │ │                        │
│                  │ │(Max  │ │(Min  │ │                        │
│                  │ │Heap) │ │Heap) │ │                        │
│                  │ └──────┘ └──────┘ │                        │
│                  └───────────────────┘                        │
│                                                              │
│                      Limit Order Book                        │
└──────────────────────────────────────────────────────────────┘
```

## ✨ Key Features

| Feature | Implementation |
|---------|---------------|
| **Price-Time Priority Matching** | Orders are matched by best price first; ties broken by earliest arrival timestamp |
| **Thread-Safe Order Book** | Granular locking with `std::mutex` and `std::condition_variable` — no global locks |
| **Dedicated Matching Engine Thread** | Background thread consumes an incoming queue, eliminating contention on core book structures |
| **Custom Thread Pool** | Fixed-size pool handles concurrent TCP connections without per-connection thread spawning |
| **Partial Fill Support** | Large orders are partially filled and remain in the book with preserved time priority |
| **Cross-Platform Networking** | POSIX sockets on Linux/macOS, Winsock2 on Windows — zero external dependencies |
| **Graceful Shutdown** | Signal handlers (`SIGINT`/`SIGTERM`) trigger clean resource teardown |

## 📁 Project Structure

```
ApexMatch/
├── CMakeLists.txt          # Build configuration (C++17, cross-platform)
├── README.md               # This file
├── include/
│   ├── Order.h             # Core types: Order, Trade, Side, Comparators
│   ├── OrderBook.h         # Thread-safe Limit Order Book interface
│   ├── ThreadPool.h        # Fixed-size Thread Pool (header + templates)
│   └── Server.h            # TCP Server with cross-platform socket layer
└── src/
    ├── OrderBook.cpp        # Matching engine, Price-Time Priority logic
    ├── ThreadPool.cpp       # Worker lifecycle, task queue management
    ├── Server.cpp           # Accept loop, command parser, client handler
    └── main.cpp             # Entry point, signal handling, initialization
```

## 🔧 Build Instructions

### Prerequisites

- **C++17 compatible compiler** (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake 3.16+**

### Build Steps

```bash
# Clone the repository
git clone https://github.com/yourusername/ApexMatch.git
cd ApexMatch

# Create build directory
mkdir build && cd build

# Configure and build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Windows (Visual Studio)

```powershell
mkdir build; cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Windows (MinGW)

```powershell
mkdir build; cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## 🚀 Usage

### Start the Server

```bash
./ApexMatch          # Linux / macOS
.\Release\ApexMatch  # Windows (Visual Studio build)
```

The engine starts on **port 8080** and displays the ASCII banner:

```
   █████╗ ██████╗ ███████╗██╗  ██╗
  ██╔══██╗██╔══██╗██╔════╝╚██╗██╔╝
  ███████║██████╔╝█████╗   ╚███╔╝
  ██╔══██║██╔═══╝ ██╔══╝   ██╔██╗
  ██║  ██║██║     ███████╗██╔╝ ██╗
  ╚═╝  ╚═╝╚═╝     ╚══════╝╚═╝  ╚═╝
```

### Connect via TCP Client

Use **Telnet**, **Netcat (ncat)**, or any TCP client:

```bash
# Linux / macOS
nc localhost 8080

# Windows (using ncat from Nmap)
ncat localhost 8080

# Or Telnet
telnet localhost 8080
```

### Trading Commands

| Command | Description | Example |
|---------|-------------|---------|
| `SUBMIT BUY <PRICE> <QTY>` | Submit a buy limit order | `SUBMIT BUY 150.50 100` |
| `SUBMIT SELL <PRICE> <QTY>` | Submit a sell limit order | `SUBMIT SELL 151.00 50` |
| `VIEW` | View top-of-book snapshot | `VIEW` |
| `HELP` | Show available commands | `HELP` |
| `QUIT` | Disconnect from server | `QUIT` |

### Example Session

```
apex> SUBMIT BUY 150.00 100
ACK: Order #1 accepted — BUY 100 @ 150.00

apex> SUBMIT BUY 151.00 50
ACK: Order #2 accepted — BUY 50 @ 151.00

apex> SUBMIT SELL 150.50 30
ACK: Order #3 accepted — SELL 30 @ 150.50

apex> VIEW
  ╔═══════════════════════════════════════╗
  ║       Order Book: AAPL               ║
  ╠═══════════════════════════════════════╣
  ║  Best Bid:      150.00 x    100      ║
  ║  Best Ask:        ---                ║
  ╠═══════════════════════════════════════╣
  ║  Last: 30 @ 150.50 (Trade #1)       ║
  ║  Total Trades:      1               ║
  ╚═══════════════════════════════════════╝

apex> QUIT
Goodbye! Connection closed.
```

### Multi-Client Stress Test

Open multiple terminals simultaneously to test concurrency:

```bash
# Terminal 1 — Buyer
for i in $(seq 1 100); do echo "SUBMIT BUY 150.00 10"; done | nc localhost 8080

# Terminal 2 — Seller
for i in $(seq 1 100); do echo "SUBMIT SELL 150.00 10"; done | nc localhost 8080

# Terminal 3 — Observer
echo "VIEW" | nc localhost 8080
```

## 🧵 Concurrency Model — Deep Dive

### Locking Mechanisms

The engine uses **three separate mutexes** for granular synchronization:

| Mutex | Protects | Held By |
|-------|----------|---------|
| `queueMutex_` | Incoming order queue | Client threads (push), Engine thread (drain) |
| `bookMutex_` | Bids & Asks containers | Engine thread (write), Client threads (read via `VIEW`) |
| `tradeMutex_` | Trade log | Engine thread (append), Client threads (read via `VIEW`) |

### Data Flow

```
Client Thread                     Matching Engine Thread
─────────────                     ──────────────────────
submitOrder()
  │
  ├─ lock(queueMutex_)
  │   └─ push to incomingQueue_
  │
  ├─ notify_one(queueCV_)         wait(queueCV_) ──────────┐
  │                                                        │
  └─ return orderId               lock(queueMutex_)        │
                                    └─ drain queue ◄───────┘
                                  unlock(queueMutex_)

                                  lock(bookMutex_)
                                    ├─ insert into bids_/asks_
                                    └─ tryMatch()
                                        ├─ compare best bid vs ask
                                        ├─ execute trade
                                        ├─ lock(tradeMutex_)
                                        │   └─ append to tradeLog_
                                        └─ update/remove orders
                                  unlock(bookMutex_)
```

### Thread Pool Architecture

```
                    ┌──────────────┐
                    │  Task Queue  │ ◄── enqueue() from accept loop
                    │  (FIFO)      │
                    └──────┬───────┘
                           │
            ┌──────────────┼──────────────┐
            ▼              ▼              ▼
      ┌──────────┐  ┌──────────┐  ┌──────────┐
      │ Worker 1 │  │ Worker 2 │  │ Worker N │
      │          │  │          │  │          │
      │ wait()   │  │ wait()   │  │ wait()   │
      │ dequeue  │  │ dequeue  │  │ dequeue  │
      │ execute  │  │ execute  │  │ execute  │
      └──────────┘  └──────────┘  └──────────┘
```

## 📊 Data Structures

### Limit Order Book

The order book uses **`std::set`** with custom comparators to emulate heap behavior while supporting efficient iteration and element removal:

- **Bids** — `std::set<Order, BidComparator>` (Max-Heap):  
  `begin()` → Highest price, earliest time → **Best Bid**

- **Asks** — `std::set<Order, AskComparator>` (Min-Heap):  
  `begin()` → Lowest price, earliest time → **Best Ask**

| Operation | Complexity |
|-----------|-----------|
| Insert order | O(log n) |
| Access best bid/ask | O(1) — `begin()` |
| Remove matched order | O(log n) |
| Match check | O(1) — compare two iterators |

## 🛡️ Memory Safety & Design

- **RAII throughout** — All resources (threads, sockets, WSA) are cleaned up in destructors
- **No raw `new`/`delete`** — `std::unique_ptr`, `std::shared_ptr`, stack allocation
- **Idempotent shutdown** — `shutdown()` / `stop()` are safe to call multiple times via `compare_exchange_strong`
- **No data races** — Every shared resource is protected by a dedicated mutex
- **Graceful signal handling** — `SIGINT`/`SIGTERM` trigger orderly teardown

## 📝 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

## 👤 Author

**Abhijeet Senapati**

---

<p align="center">
  <em>Built with precision. Engineered for concurrency.</em>
</p>
