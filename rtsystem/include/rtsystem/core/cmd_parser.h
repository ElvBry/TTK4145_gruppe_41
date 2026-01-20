#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <rtsystem/tasks/dispatcher_task.h>

#define MAX_ARGS 32

// Tokenizes input for use together with getopt from unistd.h
// returns argc 
int tokenize(char *input, char **argv);

// Sets cmd_type_t member of cmd_t argument
// returns 0 on success, -1 on error
int set_cmd_type(cmd_t *result);

int parse_UDP(cmd_t command);

int parse_TCP(cmd_t command);

int parse_ECHO(cmd_t command, char **message);

int parse_HELP(cmd_t command, char **message);

int parse_NIL(cmd_t command);

// Frees dynamically allocated argv array and its strings
void cmd_free(cmd_t *cmd);

#endif