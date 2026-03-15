#ifndef RTSYSTEM_CONFIG_H
#define RTSYSTEM_CONFIG_H

// Thread scheduling priorities (SCHED_FIFO, higher number means higher priority, should not exceed 50)
#define PRIORITY_MAIN     50
#define PRIORITY_PRIMARY  45
#define PRIORITY_BACKUP   45
#define PRIORITY_LOG_TASK 10

// Task array capacities (make sure it is larger than the amount of created tasks)
#define SYSTEM_TASKS_ARRAY_CAPACITY      3
#define APPLICATION_TASKS_ARRAY_CAPACITY 6
#define LOG_QUEUE_SIZE                   64

// Shutdown timeouts (allowed time for tasks to perform clean shutdown/graceful exit before being forced)
#define SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS 3000
#define LOG_TASK_SHUTDOWN_TIMEOUT_MS    3000
#define APP_TASK_SHUTDOWN_TIMEOUT_MS    2000

// Heartbeats used for inter process communication
#define PROCESS_PAIR_HEARTBEAT_MS 25
#define PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS 1000

// Ports and IP used for sockets (elevator_hardware and process pair uses TCP)
#define ELEVATOR_HARDWARE_IP   "localhost"
#define ELEVATOR_HARDWARE_PORT "15657"
#define PROCESS_PAIR_PORT       8081 

// Elevator layout (we currently only use 4 and 3, might treat changes later)
#define N_FLOORS   4
#define N_BUTTONS  3
#define N_ELEVATORS 3

// Per-module log levels from log_helper.h or async_log_helper.h (only used for ease of development)
#define LOG_LEVEL_MAIN              LOG_LEVEL_DEBUG
#define LOG_LEVEL_PRIMARY_TASK      LOG_LEVEL_DEBUG
#define LOG_LEVEL_BACKUP_TASK       LOG_LEVEL_DEBUG
#define LOG_LEVEL_ELEVATOR_HARDWARE LOG_LEVEL_DEBUG
#define LOG_LEVEL_TASK_HELPER       LOG_LEVEL_DEBUG
#define LOG_LEVEL_LOG_TASK          LOG_LEVEL_DEBUG

#endif
