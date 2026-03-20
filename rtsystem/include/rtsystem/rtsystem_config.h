#ifndef RTSYSTEM_CONFIG_H
#define RTSYSTEM_CONFIG_H

// Thread scheduling priorities for SCHED_FIFO (1 = lowest real-time, 99 = highest)
// Should not exceed 50 to avoid starving actual system threads.
#define PRIORITY_MAIN      50
#define PRIORITY_PRIMARY   35
#define PRIORITY_BACKUP    30
#define PRIORITY_ELEVATOR  25
#define PRIORITY_LOG_TASK  10

// Task array capacities (make sure it is larger than the amount of created tasks)
#define SYSTEM_TASKS_ARRAY_CAPACITY      3
#define APPLICATION_TASKS_ARRAY_CAPACITY 1
#define LOG_QUEUE_SIZE                   64

// Shutdown timeouts (allowed time for tasks to perform clean shutdown before being force-cancelled)
// Mostly needed for profiling code for memory leaks during shutdown
#define SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS 8000
#define LOG_TASK_SHUTDOWN_TIMEOUT_MS    3000
#define APP_TASK_SHUTDOWN_TIMEOUT_MS    2000

// Heartbeats used for inter process communication
// Higher heartbeat uses more resources but tolerates higher packet loss
#define PROCESS_PAIR_HEARTBEAT_MS 25
#define PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS 1000

#define ELEVATOR_TASK_HEARTBEAT_MS 50
#define ELEVATOR_DOOR_OPEN_HEARTBEATS (3000 / ELEVATOR_TASK_HEARTBEAT_MS) // 3 seconds
#define ELEVATOR_STARTUP_HEARTBEATS_BETWEEN_FLOORS (3000/ ELEVATOR_TASK_HEARTBEAT_MS) // 3 seconds
#define ELEVATOR_STARTUP_MAX_CYCLES 2
#define ELEVATOR_HEARTBEATS_BEFORE_MOTORSTOP (3500 / ELEVATOR_TASK_HEARTBEAT_MS) // 3.5 seconds without hitting a floor sensor -> motor considered stuck
#define ELEVATOR_HEARTBEATS_PER_LOGGED_STATE 10

// Ports and IP used for sockets (elevator_hardware and process pair uses TCP)
// Elevator 0: hw=15657, pp=8081 | Elevator 1: hw=15658, pp=8082 | etc.
#define ELEVATOR_HARDWARE_IP    "localhost"
#define ELEVATOR_HW_BASE_PORT   15657
#define PROCESS_PAIR_BASE_PORT  8081

// Elevator layout
#define N_FLOORS    4
#define N_BUTTONS   3
#define N_ELEVATORS 3

// With 4+ elevators a network partition can create two masters with independent worldview of hall lights
// When PARTITION_POSSIBLE, worldviews from different masters are combined on reconnect
// not done for N<=3 as we avoid re-performing an already served call on recconect
#define PARTITION_POSSIBLE (N_ELEVATORS >= 4)

// UDP inter-elevator networking.
//
// Port assignment: PORT(sender_id, receiver_id) = BASE + sender_id * N_ELEVATORS + receiver_id
// Each elevator binds one recv and send socket per peer 
// Example with N_ELEVATORS=3, BASE=20000:
//   0->1: 20001   0->2: 20002
//   1->0: 20003   1->2: 20005
//   2->0: 20006   2->1: 20007
#define ELEVATOR_NET_BASE_PORT         24330 // 20000 was rather busy from other traffic
//#define ELEVATOR_NET_IP_LIST           { "127.0.0.1", "127.0.0.1", "127.0.0.1" } // for testing on own machine
//#define ELEVATOR_NET_IP_LIST           { "100.10.23.1", "100.10.23.2", "100.10.23.3" } // for testing on lab if able to test address
#define ELEVATOR_NET_IP_LIST           { "255.255.255.255", "255.255.255.255", "255.255.255.255" } // broadcast
#define ELEVATOR_NET_MAX_LOSSES        8     // consecutive missed heartbeats before disconnect

// Per-module log levels from log_helper.h or async_log_helper.h (only used for ease of development)
#define LOG_LEVEL_MAIN              LOG_LEVEL_DEBUG
#define LOG_LEVEL_PRIMARY_TASK      LOG_LEVEL_DEBUG
#define LOG_LEVEL_BACKUP_TASK       LOG_LEVEL_DEBUG
#define LOG_LEVEL_ELEVATOR_HARDWARE LOG_LEVEL_DEBUG
#define LOG_LEVEL_TASK_HELPER       LOG_LEVEL_INFO
#define LOG_LEVEL_LOG_TASK          LOG_LEVEL_DEBUG
#define LOG_LEVEL_ELEVATOR_NETWORK  LOG_LEVEL_ERROR

#endif
