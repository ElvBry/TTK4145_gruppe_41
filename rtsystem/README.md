# rtsystem

Real-time multiple elevator control system for TTK4145

## Project Structure

```
rtsystem/
├── include/rtsystem/
│   ├── rtsystem_config.h             # Adjustable compile-time constants
│   ├── messages.h                    # Shared structs and crc32 used for messages and elevator state
│   ├── log_helper.h                  # Synchronous logging macros (alternative if ASYNC_LOG=OFF)
│   ├── async_log_helper.h            # Async logging macros used with log_task
│   ├── drivers/
│   │   ├── elevator_hardware.h       # TCP hardware driver from TTK4145 (slightly rewritten for logging and disconnect robustness)
│   │   └── elevator_network.h        # UDP inter-elevator messaging, peer tracking
│   ├── app/
│   │   ├── elevator_fsm.h            # Pure FSM primitives from TTK4145 (rewritten to fulfill system requirements)
│   │   ├── elevator_control.h        # Control update tick, state snapshot, door timer, log formatting
│   │   └── elevator_roles.h          # DISCONNECTED/SLAVE/MASTER role logic and worldview management
│   ├── util/
│   │   ├── task_helper.h             # POSIX thread lifecycle helpers for consistent interface and clean shutdown during development
│   │   └── fifo_queue.h              # Priority inheriting FIFO used by async log task
│   └── tasks/
│       ├── elevator_task.h
│       ├── process_pair_primary_task.h
│       ├── process_pair_backup_task.h
│       └── log_task.h                # Async log task (compiled if ASYNC_LOG=ON, not managed by task_helper to allow logging during shutdown)
├── src/
│   ├── main/main.c                   # Entry point, process pair creation, signal handling, backup promotion and shutdown
│   ├── drivers/
│   │   ├── elevator_hardware.c
│   │   └── elevator_network.c
│   ├── app/
│   │   ├── elevator_fsm.c
│   │   ├── elevator_control.c
│   │   └── elevator_roles.c
│   ├── util/
│   │   ├── task_helper.c
│   │   └── fifo_queue.c
│   └── tasks/
│       ├── elevator_task.c
│       ├── process_pair_primary_task.c
│       ├── process_pair_backup_task.c
│       └── log_task.c
└── CMakeLists.txt
```

## Architecture

### Process pairs (fault tolerance)
Each physical elevator runs two OS processes: a **primary** and a **backup**.

- The process starts as backup and tries to bind the process-pair TCP port. If the primary does not connect within the timeout, it promotes itself to primary and spawns a fresh backup.
- The primary sends periodic heartbeats to the backup containing the current elevator state and worldview. The backup stores the most recent committed state.
- If the primary dies, the backup detects missed heartbeats, promotes itself (spawning a fresh backup), and restores the last committed state.
- If the backup dies, the primary detects missed heartbeats, spawns a fresh backup, and continues as normal.
- Graceful shutdown: SIGINT on the primary sends `PP_MSG_SHUTDOWN` to the backup. Both exit gracefully.

### Inter-elevator networking

Elevators exchange state and hall-call assignments over **UDP broadcast** at a fixed heartbeat rate. Each elevator has a dedicated send and receive port per peer.

#### Roles

- **DISCONNECTED**: No peers visible. Serves only cab calls. Broadcasts own state and listens for peers each heartbeat. Transitions to SLAVE if a higher-ID peer responds, MASTER if only lower-ID peers respond.
- **SLAVE**: Connected to the highest-ID active peer (master). Sends own state, cab requests, and detected hall button presses each heartbeat. Receives assigned hall calls and the committed worldview back from master.
- **MASTER**: Highest-ID active elevator. Collects state from all slaves, runs hall call assignment, updates the worldview and replies to each slave that responded this heartbeat.

#### Two-way handshake

Slaves sends, but master only responds. Slaves send state, hall calls and worldview, Master responds with assigned halls and a new proposed worldview, removing serviced calls and adding the requestsed from all active elevators. Master only commits worldview and distributes assignments when at least one peer has responded during the heartbeat. This ensures the worldview counter only advances when the master is confirmed to be inside the network. Calls are only removed via peer acknowledgement.

#### Worldview

Worldview is the shared set of known active hall calls (cab calls and elevator state are local to each elevator)

- Master proposes a new worldview by OR-ing in detected hall calls from all peers.
- Calls are only cleared from worldview when the elevator serving them has confirmed door-open at the correct floor and has acknowledged the current worldview counter.
- When a master rejoins the network after being disconnected, if any slave holds a higher worldview counter, the master fully replaces its own worldview with the slave's. Ensuring that already served calls are not reintroduced.

#### Peer state on disconnect

When transitioning to DISCONNECTED, all knowledge about peers is cleared: last known state, last assignments, motor stop counters, and acknowledged worldview counters. Only the elevator's own state and worldview survive role transitions.

### Supported configurations

| N_ELEVATORS | Behaviour |
|-------------|-----------|
| 1           | Single-elevator mode: no networking, serves all hall and cab calls directly |
| 2–3         | Full master/slave/disconnected networking. Only possible to have one network, ensuring that hall calls are not reintroduced |
| 4+          | Sets `PARTITION_POSSIBLE` automatically in config. If two worldviews are from masters with different ID, a new worldview combines both

## Build

```bash
# Configure (async logging optional)
cmake -B build -DASYNC_LOG=ON

# Compile and grant SCHED_FIFO permission for thread priority levels and removes comitted state files from backup task
make -C build setcap fullclean
```

## Running
Check port configuration in rtsystem_config.h before connecting with elevatorserver or simulator

Start elevator 0:
```bash
./build/src/main/rtsystem --id 0
```

To run all three elevators on the simulator, change IP to localhost in rtsystem_config.h and run all on same computer:
```bash
./build/src/main/rtsystem --id 0
./build/src/main/rtsystem --id 1
./build/src/main/rtsystem --id 2
```

## Stopping

Press **Ctrl-C** in the primary's terminal for a graceful shutdown. The primary sends `PP_MSG_SHUTDOWN` to the backup, which then exits cleanly.

If the primary was killed with `kill -9` and a promoted backup is still running:
```bash
pkill -2 rtsystem
```
or
```bash
kill -2 <pid of primary>
```