#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "sish.h"

int
main (int argc, char **argv) {
    extern char *optarg;
    int case_identifier, exit;
    struct flags input_flags;
    size_t input_size_max;
    char *input_command;

    (void) setprogname(argv[0]);
    exit = 0;
    input_size_max = ARG_MAX;

    input_flags.c_flag = 0;
    input_flags.x_flag = 0;

    if ((input_command = malloc(ARG_MAX)) == NULL) {
        print_error("Could not allocate memory");
        return 1;
    }

    while ((case_identifier = getopt(argc, argv, "xc:")) != -1) {
        switch (case_identifier) {
        case 'c':
            input_flags.c_flag = 1;
            if ((input_command = strdup(optarg)) == NULL) {
                print_error("Could not allocate memory");
                return 1;
            }
            break;
        case 'x':
            input_flags.x_flag = 1;
            break;
        case '?':
            print_usage();
            return 1;
        default:
            break;
        }
    }

    if (input_flags.c_flag) {
        (void) execute_command(input_command);
    } else {
        while (exit == 0) {
            fprintf(stdout, "%s$ ", getprogname());
            if (getline(&input_command, &input_size_max, stdin) == -1) {
                print_error("Could not get input");
                return 1;
            }

            /* getline also includes the '\n' at the end, hence we replace it by null*/
            (void) strip_new_line(input_command);

            if (strcmp(input_command, "exit") == 0) {
                break;
            }

            (void) execute_command(input_command);
        }
    }

    (void) free(input_command);

    return 0;
}

void
print_usage() {
    fprintf(stderr, "%s: Usage: sish [-c command] [-x]\n", getprogname());
}

void
strip_new_line(char *input) {
    int input_length;

    input_length = strlen(input);
    if (input[input_length - 1] == '\n') {
        input[input_length - 1] = '\0';
    }
}

/**
 * This method is called after we have read the input from the terminal and 
 * split the input by the pipe
 **/
void
execute_command(char *command) {
    char *last, *token, **tokens, *command_copy;
    int token_count, index;

    index = 0;

    if ((token_count = get_token_count(command)) < 0) {
        print_error("Could not allocate memory");
        return;
    }

    if ((tokens = malloc(token_count * sizeof(char *))) == NULL) {
        print_error("Could not allocate memory");
        return;
    }

    if ((command_copy = strdup(command)) == NULL) {
        print_error("Could not allocate memory");
        return;
    }

    token = strtok_r(command_copy, " ", &last);

    while (token != NULL) {
        if ((tokens[index] = strdup(token)) == NULL) {
            print_error("Could not allocate memory");
            return;
        }
        token = strtok_r(NULL, " ", &last);
        index++;
    }

    for (index = 0; index < token_count; index++) {
        printf("Input: %s\n", tokens[index]);
    }

    (void) free(tokens);
    (void) free(command_copy);
}

int
get_token_count(char *command) {
    char *last, *token, *command_copy;
    int token_count;

    token_count = 0;

    /* String duplication is required as strtok modifies the orignal string */
    if ((command_copy = strdup(command)) == NULL) {
        return -1;
    }

    token = strtok_r(command_copy, " ", &last);

    while (token != NULL) {
        token_count++;
        token = strtok_r(NULL, " ", &last);
    }

    (void) free(command_copy);
    return token_count;
}

void
print_error(char *message) {
    fprintf(stderr, "%s: %s: %s", getprogname(), message, strerror(errno));
}