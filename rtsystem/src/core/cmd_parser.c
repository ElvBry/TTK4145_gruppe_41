#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <wordexp.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/core/cmd_parser.h>
#include <rtsystem/async_log_helper.h>
#include <rtsystem/tasks/dispatcher_task.h>

const static char *TAG = "cmd_parser";
static char *CMD_HELP_MESSAGE = "possible commands: \n\
                                    socket <to be added>\n\
                                    echo -m <message> -h <this message>\n\
                                    help <this message>";

int tokenize(char *input, char **argv) {
    wordexp_t p;
    int ret = wordexp(input, &p, WRDE_NOCMD);
    if (ret != 0) {
        argv[0] = NULL;
        return 0;
    }

    int argc = 0;
    while (argc < (int)p.we_wordc && argc < MAX_ARGS - 1) {
        argv[argc] = strdup(p.we_wordv[argc]);
        argc++;
    }
    argv[argc] = NULL;

    wordfree(&p);
    return argc;
}

int set_cmd_type(cmd_t *result) {
    const char *first_token = result->argv[0];
    if (first_token == NULL) {
        LOGW(TAG, "argv not initialized, should not happen");
        return -1;
    }

    result->cmd_type = NIL;
    if        (strcmp(first_token, "socket") == 0) {
        result->cmd_type = SOCKET;
    } else if (strcmp(first_token, "echo") == 0) {
        result->cmd_type = ECHO;
    } else if (strcmp(first_token, "help") == 0) {
        result->cmd_type = HELP;
    }
    return 0;
}

int parse_socket(cmd_t command) {
    LOGD(TAG, "in socket");
    return 0;
}

int parse_echo(cmd_t command, char **message) {
    LOGD(TAG, "in echo");
    optind = 0;  // Reset getopt state for new parsing
    opterr = 0;  // Suppress getopt error messages
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

int parse_help(cmd_t command, char **message) {
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