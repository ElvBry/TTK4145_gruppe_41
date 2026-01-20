#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>

// Compile-time DJB2 hash (up to 8 chars, extendable)
// Used to find first argument of task to correctly parse input
#define DJB2_INIT 5381u
#define DJB2_STEP(h, c) (((h) << 5) + (h) + (c))

#define HASH1(s)  ((s)[0] ? DJB2_STEP(DJB2_INIT, (s)[0]) : DJB2_INIT)
#define HASH2(s)  ((s)[1] ? DJB2_STEP(HASH1(s),  (s)[1]) : HASH1(s))
#define HASH3(s)  ((s)[2] ? DJB2_STEP(HASH2(s),  (s)[2]) : HASH2(s))
#define HASH4(s)  ((s)[3] ? DJB2_STEP(HASH3(s),  (s)[3]) : HASH3(s))
#define HASH5(s)  ((s)[4] ? DJB2_STEP(HASH4(s),  (s)[4]) : HASH4(s))
#define HASH6(s)  ((s)[5] ? DJB2_STEP(HASH5(s),  (s)[5]) : HASH5(s))
#define HASH7(s)  ((s)[6] ? DJB2_STEP(HASH6(s),  (s)[6]) : HASH6(s))
#define HASH8(s)  ((s)[7] ? DJB2_STEP(HASH7(s),  (s)[7]) : HASH7(s))
// Up to more characters by continuing pattern for HASH9, HASH10...

// Hashes of all commands for efficient use of switch case
#define TEMP_UDP  HASH3("UDP")
#define TEMP_TCP  HASH3("TCP")
#define TEMP_ECHO HASH4("echo")
#define TEMP_HELP HASH4("help")
// Needs to be allocated at runtime to make compiler happy
const static uint32_t HASH_UDP  = TEMP_UDP;
const static uint32_t HASH_TCP  = TEMP_TCP;
const static uint32_t HASH_ECHO = TEMP_ECHO;
const static uint32_t HASH_HELP = TEMP_HELP;

#define TEST_FOR_COLLISION

// Runtime hash for comparing input to precomputed hash
static uint32_t hash_command(const char *str) {
    uint32_t hash = DJB2_INIT;
    int c;
    while ((c = *str++))
        hash = DJB2_STEP(hash, c);
    return hash;
}

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/core/cmd_parser.h>
#include <rtsystem/async_log_helper.h>
#include <rtsystem/tasks/dispatcher_task.h>

const static char *TAG = "cmd_parser";
static char *CMD_HELP_MESSAGE = "possible commands: \n\
                                    UDP <to be added>\n\
                                    TCP <to be added>\n\
                                    echo -m <message> -h <this message>\n\
                                    help <this entire message>";

int tokenize(char *input, char **argv) {
    int argc = 0;
    char *token = strtok(input, " \t\n");

    while (token != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;
    return argc;
}

int set_cmd_type(cmd_t *result) {
    #ifdef TEST_FOR_COLLISION
    if (HASH_UDP  == HASH_TCP  ||
        HASH_TCP  == HASH_HELP || 
        HASH_HELP == HASH_ECHO ||
        HASH_ECHO == HASH_UDP) {
        LOGE(TAG, "ERROR: HASH COLLISION IN VARIABLE NAMES, CHANGE HASHING FUNCTION OR NAME");
    }
    #endif
    const char *first_token = result->argv[0];
    if (first_token == NULL) {
        LOGW(TAG, "argv not initialized, should not happen");
        return -1;
    }

    uint32_t hashed_arg = hash_command(first_token);
    result->cmd_type = NIL;
    if (hashed_arg == HASH_UDP) {
        result->cmd_type = UDP;
    } else if (hashed_arg == HASH_TCP) {
        result->cmd_type = TCP;
    } else if (hashed_arg == HASH_ECHO) {
        result->cmd_type = ECHO;
    } else if (hashed_arg == HASH_HELP) {
        result->cmd_type = HELP;
    }
    return 0;
}

int parse_UDP(cmd_t command) {
    LOGD(TAG, "in UDP");
    return 0;
}

int parse_TCP(cmd_t command) {
    LOGD(TAG, "in TCP");
    return 0;
}

int parse_ECHO(cmd_t command, char **message) {
    LOGD(TAG, "in echo");
    optind = 1;  // Reset getopt state for new parsing
    int opt;
    while ((opt = getopt(command.argc, command.argv, "m:h")) != -1) {
        switch (opt) {
            case 'm':
                *message = optarg;
                break;
            case 'h':
                *message = "echo -m <message> -h <this message>";
                return 0;
            default:
                LOGW(TAG, "unknown option");
                break;
        }
    }
    return 0;
}

int parse_HELP(cmd_t command, char **message) {
    LOGD(TAG, "in help");
    *message = CMD_HELP_MESSAGE;
    return 0;
}

int parse_NIL(cmd_t command) {
    LOGD(TAG, "in NIL");
    return 0;
}

void cmd_free(cmd_t *cmd) {
    if (cmd == NULL || cmd->argv == NULL) {
        return;
    }
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    cmd->argv = NULL;
    cmd->argc = 0;
}