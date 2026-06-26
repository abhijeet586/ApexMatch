# ApexMatch

A high-concurrency order matching engine built in C++17.

## Architecture Overview

```text
┌──────────────────────────────────────────────────────────────┐
│                      TCP Clients                             │
│              (Telnet / Netcat / Trading Bots)                │
└──────────────┬───────────────────────────────┬───────────────┘
               │          TCP/IP               │
┌──────────────▼───────────────────────────────▼───────────────┐
│                    TCP Server (Port 8080)                    │
│              Accept Loop -> Thread Pool Dispatch             │
├──────────────────────────────────────────────────────────────┤
│                   ┌─────────────────────┐                    │
│                   │   Connection Pool   │                    │
│                   │  (Fixed Thread Pool)│                    │
│                   │  ┌──┐ ┌──┐ ┌──┐ ┌──┐│                    │
│                   │  │W1│ │W2│ │W3│ │W4││                    │
│                   │  └──┘ └──┘ └──┘ └──┘│                    │
│                   └─────────┬───────────┘                    │
├─────────────────────────────▼────────────────────────────────┤
│                                                              │
│                  ┌───────────────────┐                       │
│   Submit ───────>│  Incoming Queue   │                       │
│                  │  (mutex + cv)     │                       │
│                  └─────────┬─────────┘                       │
│                            │ drain                           │
│                  ┌─────────▼─────────┐                       │
│                  │  Matching Engine  │  <── Dedicated Thread │
│                  │   (Background)    │                       │
│                  ├───────────────────┤                       │
│                  │ ┌──────┐ ┌──────┐ │                       │
│                  │ │ BIDS │ │ ASKS │ │                       │
│                  │ │(Max  │ │(Min  │ │                       │
│                  │ │Heap) │ │Heap) │ │                       │
│                  │ └──────┘ └──────┘ │                       │
│                  └───────────────────┘                       │
│                                                              │
│                      Limit Order Book                        │
└──────────────────────────────────────────────────────────────┘
```

## Key Features

- **Price-Time Priority Matching:** Orders are matched by best price first; ties broken by earliest arrival timestamp.
- **Thread-Safe Order Book:** Granular locking with `std::mutex` and `std::condition_variable` without global locks.
- **Dedicated Matching Engine Thread:** Background thread consumes an incoming queue, eliminating contention on core book structures.
- **Custom Thread Pool:** Fixed-size pool handles concurrent TCP connections.
- **Partial Fill Support:** Large orders are partially filled and remain in the book with preserved time priority.
- **Cross-Platform Networking:** POSIX sockets on Linux/macOS, Winsock2 on Windows.
- **Graceful Shutdown:** Signal handlers (`SIGINT`/`SIGTERM`) trigger clean resource teardown.

## Project Structure

```text
ApexMatch/
├── CMakeLists.txt          # Build configuration
├── README.md               # Documentation
├── include/
│   ├── Order.h             # Core types
│   ├── OrderBook.h         # Limit Order Book interface
│   ├── ThreadPool.h        # Fixed-size Thread Pool
│   └── Server.h            # TCP Server
└── src/
    ├── OrderBook.cpp       # Matching engine logic
    ├── ThreadPool.cpp      # Thread pool implementation
    ├── Server.cpp          # TCP server and command parser
    └── main.cpp            # Entry point
```

## Build Instructions

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.16+

### Unix (Linux / macOS)

```bash
git clone https://github.com/abhijeet586/ApexMatch.git
cd ApexMatch
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Windows (Visual Studio)

```powershell
git clone https://github.com/abhijeet586/ApexMatch.git
cd ApexMatch
mkdir build; cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## Usage

### Start the Server

```bash
./ApexMatch          # Linux / macOS
.\Release\ApexMatch  # Windows
```

The server starts on port 8080.

### Connect via TCP Client

Use Telnet or Netcat:

```bash
nc localhost 8080
# or
telnet localhost 8080
```

### Trading Commands

- `SUBMIT BUY <PRICE> <QTY>`: Submit a buy limit order (e.g., `SUBMIT BUY 150.50 100`)
- `SUBMIT SELL <PRICE> <QTY>`: Submit a sell limit order (e.g., `SUBMIT SELL 151.00 50`)
- `VIEW`: View top-of-book snapshot
- `HELP`: Show available commands
- `QUIT`: Disconnect from server

### Example Session

```text
apex> SUBMIT BUY 150.00 100
ACK: Order #1 accepted -- BUY 100 @ 150.00

apex> SUBMIT BUY 151.00 50
ACK: Order #2 accepted -- BUY 50 @ 151.00

apex> SUBMIT SELL 150.50 30
ACK: Order #3 accepted -- SELL 30 @ 150.50

apex> VIEW
  +=======================================+
  |       Order Book: AAPL                |
  +=======================================+
  |  Best Bid:      150.00 x    100      |
  |  Best Ask:        ---                |
  +---------------------------------------+
  |  Last: 30 @ 150.50 (Trade #1)        |
  |  Total Trades:      1                |
  +=======================================+

apex> QUIT
Goodbye! Connection closed.
```

## Concurrency Model

### Locking Mechanisms

The engine uses three separate mutexes:

- `queueMutex_`: Protects the incoming order queue. Accessed by client threads (push) and the engine thread (drain).
- `bookMutex_`: Protects the bids and asks containers. Accessed by the engine thread (write) and client threads (read via `VIEW`).
- `tradeMutex_`: Protects the trade log. Accessed by the engine thread (append) and client threads (read via `VIEW`).

### Limit Order Book

The order book uses `std::set` with custom comparators to emulate heap behavior while supporting efficient iteration and element removal:

- **Bids:** `std::set<Order, BidComparator>` (Max-Heap)
- **Asks:** `std::set<Order, AskComparator>` (Min-Heap)

## License

MIT License.

## Author

Abhijeet Senapati
