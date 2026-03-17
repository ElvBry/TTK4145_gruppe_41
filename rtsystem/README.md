# rtsystem

Real-time multiple elevator control system for TTK4145

## Project Structure

```
rtsystem/
├── include/rtsystem/
│   ├── rtsystem_config.h             # Adjustable compile-time constants
│   ├── messages.h                    # Shared structs and crc32 used for messages and elevator control
│   ├── log_helper.h                  # Synchronous logging macros without async log task (Old)
│   ├── async_log_helper.h            # Async logging macros used with log_task (just for visual appeal)
│   ├── core/
│   │   ├── task_helper.h             # Helpers to standardize usage of posix threads and resources to reduce conflicts
│   │   ├── elevator_hardware.h       # TCP hardware driver from TTK4145 (rewritten for logging and disconnect robustness)
│   │   ├── elevator_network.h        # UDP inter-elevator messaging
│   │   ├── elevator_control_helper.h # FSM helper for elevator control and cost function (rewritten in C from TTK4145 D implementation)
│   │   └── fifo_queue.h              # FIFO queue used with async log_task to allow threads to log without waiting on system (mostly for fun)
│   └── tasks/
│       ├── elevator_task.h             # Access to shared elevator state with my_elevator and config
│       ├── elevator_fsm.h              # FSM update, state snapshot, door timer, log for dumping state to terminal
│       ├── elevator_roles.h            # Role enum (DISCONNECTED/SLAVE/MASTER), per-role logic
│       ├── process_pair_primary_task.h # Config to call in main
│       ├── process_pair_backup_task.h  # Config to call in main
│       └── log_task.h                  # Functions to create custom async log task (not used with task_helper to allow for logging during shutdown)
├── src/
│   ├── main/main.c                    # Entry point, process pair creation, signal handling and backup promotion
│   ├── core/
│   │   ├── task_helper.c              # Task and task array management for clean shutdown and eventfd signaling to tasks
│   │   ├── elevator_hardware.c        # TCP socket driver for elevator hardware server/unit
│   │   ├── elevator_network.c         # UDP send/recv sockets, peer connection tracking
│   │   ├── elevator_control_helper.c  # Pure FSM logic
│   │   └── fifo_queue.c
│   └── tasks/
│       ├── elevator_task.c             # Elevator system control loop, startup sequence, and hw reconnect. Main logic of system
│       ├── elevator_fsm.c              # FSM tick, snapshot commit, door timer, log formatting used by elevator_task
│       ├── elevator_roles.c            # DISCONNECTED/SLAVE/MASTER role logic called by elevator_task
│       ├── process_pair_primary_task.c # Process pair task that handles elevator_task initialization, backup respawning and transfer of elevator state to backup
│       ├── process_pair_backup_task.c  # Process pair task that stores elevator state, promotes to primary when it stops responding (only one is active)
│       └── log_task.c                  # Async log task (only used when ASYNC_LOG=ON)
└── CMakeLists.txt
```

## Architecture

### Process pairs (fault tolerance)
Each physical elevator runs two OS processes: a **primary** and a **backup**.
System assumes it is impossible to have more than one connected network partition at once, which is only possible for 4 or more elevators.
- Process starts as backup, tries to binds the process-pair TCP port. If primary does not connect, becomes primary and spawns new backup process.
- Primary connects to backup and sends periodic heartbeats containing the elevator state and worldview. Backup stores most recent elevator state and worldview.
- If primary dies, backup detects missed heartbeats, promotes to primary (spawning a fresh backup) and restores most recent committed state.
- If backup dies, primary detects missed heartbeats, spawns a fresh backup and continues as normal.
- Graceful shutdown: If primary receives SIGINT: sends `PP_MSG_SHUTDOWN` over channel, both processes exiting gracefully

### Inter-elevator networking
Elevators exchange state and hall-call assignments over **UDP unicast**. Port assignment decided by --id option. Elevators move between roles during elevator_task heartbeats. Roles create a robust framework for 2-3 elevators, never dropping accepted hall calls during testing. For 4 or more elevators, worldview handling needs to change as system assumes multiple temporarily connected network partitons at the same time are impossible.

Roles:
- **DISCONNECTED**: no peers visible. serves only cab calls and tries to reconnect to network as either master or slave.
- **SLAVE**:        Connected to peer with higher ID. Sends elevator state, cab calls and hall requests, receives back assigned halls and worldview from master
- **MASTER**:       Highest-ID of connected elevators. Computes and distributes hall call assignments and updates worldview based on hall requests

## Build

```bash
# Configure (async logging optional, but more stylish)
cmake -B build -DASYNC_LOG=ON

# Compile and grant SCHED_FIFO permission for thread priority levels
make -C build setcap
```

## Running

Start elevator 0
```bash
./build/src/main/rtsystem --id 0
```

To run all three elevators on simulator
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