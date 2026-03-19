#ifndef RTSYSTEM_CONFIG_H
#define RTSYSTEM_CONFIG_H

// Thread scheduling priorities for SCHED_FIFO (1 = lowest real-time, 99 = highest).
// The relative ordering matters: main > primary/backup tasks > log.
// Kept low (1-10) to avoid starving system threads. Grant permission with:
//   sudo setcap cap_sys_nice+ep <binary>   (once after each build, no sudo at runtime)
#define PRIORITY_MAIN     10
#define PRIORITY_PRIMARY   5
#define PRIORITY_BACKUP    5
#define PRIORITY_ELEVATOR  4
#define PRIORITY_LOG_TASK  1

// Task array capacities (make sure it is larger than the amount of created tasks)
#define SYSTEM_TASKS_ARRAY_CAPACITY      3
#define APPLICATION_TASKS_ARRAY_CAPACITY 1
#define LOG_QUEUE_SIZE                   64

// Shutdown timeouts (allowed time for tasks to perform clean shutdown before being force-cancelled).
//
// SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS must be large enough to cover the primary task's full
// shutdown sequence: wait for recv() to return (up to PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS),
// then wait for the backup process to fully exit (up to LOG_TASK_SHUTDOWN_TIMEOUT_MS).
// Rule: SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS > PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS + LOG_TASK_SHUTDOWN_TIMEOUT_MS
#define SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS 8000
#define LOG_TASK_SHUTDOWN_TIMEOUT_MS    3000
#define APP_TASK_SHUTDOWN_TIMEOUT_MS    2000

// Heartbeats used for inter process communication
#define PROCESS_PAIR_HEARTBEAT_MS 25
#define PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS 1000

#define ELEVATOR_TASK_HEARTBEAT_MS 50
#define ELEVATOR_DOOR_OPEN_TICKS (3000 / ELEVATOR_TASK_HEARTBEAT_MS) // 3 seconds
#define ELEVATOR_STARTUP_TICKS_BETWEEN_FLOORS_MS (3000/ ELEVATOR_TASK_HEARTBEAT_MS) // 3 seconds, approximate time to move between floors (still works if miscalculated)
#define ELEVATOR_STARTUP_MAX_CYCLES 2 // total down-travel window = MAX_CYCLES * TICKS_BETWEEN_FLOORS
#define TICKS_BEFORE_MOTORSTOP (4000 / ELEVATOR_TASK_HEARTBEAT_MS) // 4 seconds without hitting a floor sensor → motor considered stuck

// Ports and IP used for sockets (elevator_hardware and process pair uses TCP).
// Elevator 0: hw=15657, pp=8081 | Elevator 1: hw=15658, pp=8082 | etc.
#define ELEVATOR_HARDWARE_IP    "localhost"
#define ELEVATOR_HW_BASE_PORT   15657
#define PROCESS_PAIR_BASE_PORT  8081

// Elevator layout (we currently only use 4 and 3, might treat changes later)
#define N_FLOORS    4
#define N_BUTTONS   3
#define N_ELEVATORS 3

// With 4+ elevators a network partition can create two masters with independent counters.
// When PARTITION_POSSIBLE, worldviews from different masters are combined on reconnect,
// with the risk of re-performing an already served call.
#define PARTITION_POSSIBLE (N_ELEVATORS >= 4)

// UDP inter-elevator networking.
//
// Port assignment: PORT(sender, receiver) = BASE + sender * N_ELEVATORS + receiver
// Each elevator binds one recv and send socket per peer 
// Example with N_ELEVATORS=3, BASE=20000:
//   0->1: 20001   0->2: 20002
//   1->0: 20003   1->2: 20005
//   2->0: 20006   2->1: 20007
#define ELEVATOR_NET_BASE_PORT         24330 // 20000 was rather busy from other traffic
//#define ELEVATOR_NET_IP_LIST           { "127.0.0.1", "127.0.0.1", "127.0.0.1" } // for testing on own machine
//#define ELEVATOR_NET_IP_LIST           { "100.10.23.1", "100.10.23.2", "100.10.23.3" } // for testing on lab if able to test address
#define ELEVATOR_NET_IP_LIST           { "255.255.255.255", "255.255.255.255", "255.255.255.255" } // broadcast
#define ELEVATOR_NET_MAX_LOSSES        8     // consecutive missed ticks before disconnect

// Per-module log levels from log_helper.h or async_log_helper.h (only used for ease of development)
#define LOG_LEVEL_MAIN              LOG_LEVEL_DEBUG
#define LOG_LEVEL_PRIMARY_TASK      LOG_LEVEL_DEBUG
#define LOG_LEVEL_BACKUP_TASK       LOG_LEVEL_DEBUG
#define LOG_LEVEL_ELEVATOR_HARDWARE LOG_LEVEL_DEBUG
#define LOG_LEVEL_TASK_HELPER       LOG_LEVEL_INFO
#define LOG_LEVEL_LOG_TASK          LOG_LEVEL_DEBUG
#define LOG_LEVEL_ELEVATOR_NETWORK  LOG_LEVEL_DEBUG

#endif
