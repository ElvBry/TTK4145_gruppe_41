#ifndef PROCESS_PAIR_BACKUP_TASK_H
#define PROCESS_PAIR_BACKUP_TASK_H

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/util/task_helper.h>
#include <rtsystem/messages.h>

extern const task_config_t backup_task_config;

// Returns the last committed process_pair_message_t received from primary.
// Only call this after the backup task has finished.
process_pair_message_t process_pair_backup_get_last_committed(void);

#endif