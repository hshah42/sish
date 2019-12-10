#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>

#include "sish.h"

void
handle_sig_int(__attribute__((unused)) int signal) {
	/* Do nothing when we get interrupt signal */
    return;
}

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
        print_error("Could not allocate memory", 1);
        return 1;
    }

    while ((case_identifier = getopt(argc, argv, "xc:")) != -1) {
        switch (case_identifier) {
        case 'c':
            input_flags.c_flag = 1;
            if ((input_command = strdup(optarg)) == NULL) {
                print_error("Could not allocate memory", 1);
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
        if (signal(SIGINT, handle_sig_int) == SIG_ERR) {
            print_error("Could not register signal", 1);
		    return 1;
	    }

        while (exit == 0) {
            fprintf(stdout, "%s$ ", getprogname());
            if (getline(&input_command, &input_size_max, stdin) == -1) {
                print_error("Could not get input", 1);
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

/**
 * strip_new_line will remove the '\n' at the end of the string. getline includes
 * '\n' which is not the user command but due to pressing enter to execute the command.
 * Hence we need to strip it before performing further processing on the input.
 **/
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
    int token_count, index, status, command_length;
    
    index = 0;

    if (strlen(command) < 1 && command[0] == '\0') {
        return;
    }

    command_length = strlen(command);

    if ((token_count = get_token_count(command)) < 0) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    if ((tokens = malloc(token_count * sizeof(char *))) == NULL) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    if ((command_copy = strdup(command)) == NULL) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    token = strtok_r(command_copy, " ", &last);

    while (token != NULL) {
        if ((tokens[index] = strdup(token)) == NULL) {
            print_error("Could not allocate memory", 1);
            previous_exit_code = 127;
            return;
        }
        token = strtok_r(NULL, " ", &last);
        index++;
    }

    if (strcmp(tokens[0], "cd") == 0) {
        if (token_count == 1) {
            status = perform_directory_change(NULL);
        } else {
            status = perform_directory_change(tokens[1]);
        }
        previous_exit_code = status;
    } else if (strcmp(tokens[0], "echo") == 0) {
        status = perform_echo(tokens, token_count, command_length);
        previous_exit_code = status;
    }

    (void) free(tokens);
    (void) free(command_copy);
}

/**
 * get_token_count gets the number of tokens
 * present in the input that are seperated by the
 * space delimiter.
 **/
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

/**
 * Changes the directory based on the input. This will return the status based on
 * success of failure which can be used for '$' inputs in echo command. If null is
 * passed then the home directory is fetched of the user and then chdir into the 
 * home directory.
 **/
int
perform_directory_change(char *directory) {
    uid_t current_user;
    struct passwd *user_info;

    if (directory == NULL) {
        current_user = geteuid();
        if ((user_info = getpwuid(current_user)) == NULL) {
            print_error("Could not determine home directory", 1);
            return 127;
        }

        if ((directory = strdup(user_info->pw_dir)) == NULL) {
            print_error("Could not allocate memory", 1);
            return 127;
        }
    }

    if (chdir(directory) < 0) {
        print_error("cd: Could not change directory", 0);
        return errno;
    }    

    return 0;
}

/**
 * echo like feature is performed on the tokens [1..n-1]
 * $$ and $? are replaces with pid and previous_exit_code respectively.
 **/
int
perform_echo(char **tokens, int token_count, int command_length) {
    char *echo_string, *pid_string, *exit_status_string;
    int index, total_output_length, temp_length;
    pid_t current_pid;

    total_output_length = command_length;
    current_pid = getpid();

    for (index = 1; index < token_count; index++) {
        temp_length = 0;
        if (strcmp(tokens[index], "$$") == 0) {
            total_output_length -= 2;
            temp_length = get_number_of_digits(current_pid) + 1;
            
            if ((pid_string = malloc(temp_length)) == NULL) {
                print_error("cd: Could not allocate memory", 0);
                return 127;
            }

            if (snprintf(pid_string, temp_length, "%i", current_pid) < 0) {
                print_error("cd: ", 0);
                return 127;
            }
        } else if (strcmp(tokens[index], "$?") == 0) {
            total_output_length -= 2;
            temp_length = get_number_of_digits(previous_exit_code) + 1;

             if ((exit_status_string = malloc(temp_length)) == NULL) {
                print_error("cd: Could not allocate memory", 0);
                return 127;
            }

            if (snprintf(exit_status_string, temp_length, "%i", previous_exit_code) < 0) {
                print_error("cd: ", 0);
                return 127;
            }
        }
        total_output_length += temp_length;
    }

    if ((echo_string = malloc(total_output_length + 1)) == NULL) {
        print_error("cd: Could not allocate memory", 0);
        return 127;
    }

    echo_string[0] = '\0';

    for (index = 1; index < token_count; index++) {
        if (index > 0) {
            if (strcat(echo_string, " ") == NULL) {
                print_error("cd: Internal error: ", 0);
            }
        }

        if (strcmp(tokens[index], "$$") == 0) {
            if (strcat(echo_string, pid_string) == NULL) {
                print_error("cd: Internal error: ", 0);
            }
        } else if (strcmp(tokens[index], "$?") == 0) {
            if (strcat(echo_string, exit_status_string) == NULL) {
                print_error("cd: Internal error: ", 0);
            }
        } else {
            if (strcat(echo_string, tokens[index]) == NULL) {
                print_error("cd: Internal error: ", 0);
            }
        }
    }

    fprintf(stdout, "%s\n", echo_string);
    return 0;   
}

void
print_error(char *message, int include_prog_name) {
    if (include_prog_name) {
        fprintf(stderr, "%s: %s: %s\n", getprogname(), message, strerror(errno));
    } else {
        fprintf(stderr, "%s: %s\n", message, strerror(errno));
    }
}

/**
 * Calculates the number of digits present in the number.
 * This will not work with negative numbers.
 **/
unsigned int
get_number_of_digits(int number) {
    unsigned int count;
    if (number == 0) {
        return 1;
    }
    count = 0;
    while (number != 0) {
        number = number / 10;
        count++;
    }

    return count;
}