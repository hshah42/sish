#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>
#include <setjmp.h>
#include <fcntl.h>

#include "sish.h"

jmp_buf  JumpBuffer;

void
handle_sig_int(__attribute__((unused)) int signal) {
	/* Do nothing when we get interrupt signal */
    fprintf(stdout, "\n");
    (void) longjmp(JumpBuffer, 1);
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

    /* TODO: return check */
    default_standard_input = dup(STDIN_FILENO);
    default_standard_output = dup(STDOUT_FILENO);

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
        (void) setjmp(JumpBuffer);
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
    int token_count, token_count_estimate, index, status, command_length;
    int redirection_status;
    
    index = 0;
    token_count = 0;

    if (strlen(command) < 1 && command[0] == '\0') {
        return;
    }

    command_length = strlen(command);

    if ((token_count_estimate = get_token_count(command)) < 0) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    if ((tokens = malloc((token_count_estimate * sizeof(char *)) + 1)) == NULL) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    if ((command_copy = strdup(command)) == NULL) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    token = strtok_r(command_copy, " \t", &last);

    while (token != NULL) {
        token_count++;
        if ((tokens[index] = strdup(token)) == NULL) {
            print_error("Could not allocate memory", 1);
            previous_exit_code = 127;
            return;
        }
        token = strtok_r(NULL, " \t", &last);
        index++;
    }

    tokens[index] = '\0';

    if ((redirection_status = redirect_file_descriptors(tokens, &token_count)) != 0) {
        previous_exit_code = redirection_status;
        return;
    }

    if (strcmp(tokens[0], "cd") == 0) {
        if (token_count == 1) {
            status = perform_directory_change(NULL);
        } else {
            status = perform_directory_change(tokens[1]);
        }
    } else if (strcmp(tokens[0], "echo") == 0) {
        status = perform_echo(tokens, token_count, command_length);
    } else {
        status = perform_exec(tokens);
    }

    previous_exit_code = status;

    (void) free(tokens);
    (void) free(command_copy);

    reset_file_descriptors();
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

    token = strtok_r(command_copy, " \t<>", &last);

    while (token != NULL) {
        token_count++;
        token = strtok_r(NULL, " \t<>", &last);
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
    int index, total_output_length, temp_length, free_pid, free_exit;
    pid_t current_pid;

    total_output_length = command_length;
    current_pid = getpid();
    free_pid = 0;
    free_exit = 0;

    /* Determining the size of memory to be allocated since we need to
       resolve $$ and $? */
    for (index = 1; index < token_count; index++) {
        temp_length = 0;
        if (strcmp(tokens[index], "$$") == 0) {
            total_output_length -= 2;
            temp_length = get_number_of_digits(current_pid) + 1;
            
            if ((pid_string = malloc(temp_length)) == NULL) {
                print_error("cd: Could not allocate memory", 0);
                return 127;
            }

            free_pid = 1;

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

            free_exit = 1;

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

    if (free_pid) {
        (void) free(pid_string);
    }

    if (free_exit) {
        (void) free(exit_status_string);
    }

    fprintf(stdout, "%s\n", echo_string);
    return 0;   
}

/**
 * perform_exec executes the command which should be at the 0
 * position of the tokens array. It passes tokens as the args as 
 * it is.
 **/
int
perform_exec(char **tokens) {
    int status;
    pid_t child_pid;
  
    if ((child_pid = fork()) < 0) {
        print_error("Could not create new process: ", 1);
        return 127;
    } else if (child_pid == 0) {
        execvp(tokens[0], tokens);
        
        if (errno == ENOENT) {
            fprintf(stderr, "%s: not found\n", tokens[0]);
        } else {
            fprintf(stderr, "%s: %s\n", tokens[0], strerror(errno));
        }

        exit(127);
    }

    (void) waitpid(child_pid, &status, 0);

    if (WIFEXITED(status)) {
        status = WEXITSTATUS(status);
    }

    return status;
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

/**
 * redirect_file_descriptors will parse the tokens generated to see for
 * <, >> and > operators to determine where we should redirect the output
 * to and from where we should take in the input for executing the command.
 **/
int
redirect_file_descriptors(char **tokens, int *token_count) {
    int input_file_descriptor, output_file_descriptor, index, mode;
    int redirected, new_token_count;
    char *file_name;

    input_file_descriptor = STDIN_FILENO;
    output_file_descriptor = STDOUT_FILENO;

    redirected = 0;
    mode = 0;

    new_token_count = 0;

    for (index = 0; index < *token_count; index++) {
        mode = 0;
        if ((*token_count - index) == 1) {
            if (strcmp(tokens[index], ">") == 0 || strcmp(tokens[index], ">>") == 0
                || strcmp(tokens[index], "<") == 0) {
                print_error("Syntax error", 1);
                return 127;
            }
        }

        if (strcmp(tokens[index], ">") == 0) {
            if ((file_name = strdup(tokens[index + 1])) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }
            mode = 1;
        } else if (strcmp(tokens[index], ">>") == 0) {
            if ((file_name = strdup(tokens[index + 1])) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }
            mode = 2;
        } else if (strcmp(tokens[index], "<") == 0) {
             if ((file_name = strdup(tokens[index + 1])) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }
            mode = 3;
        } else {
            if (strlen(tokens[index]) > 1 ) {
                if (tokens[index][0] == '>' && tokens[index][1] == '>') {
                    if ((file_name = create_string_from_index(tokens[index], 2)) == NULL) {
                        print_error("Could not allocate memory", 1);
                        return 127; 
                    }
                    mode = 2;
                } else if (tokens[index][0] == '>') {
                    if ((file_name = create_string_from_index(tokens[index], 1)) == NULL) {
                        print_error("Could not allocate memory", 1);
                        return 127; 
                    }
                    mode = 1;
                } else if (tokens[index][0] == '<') {
                    if ((file_name = create_string_from_index(tokens[index], 1)) == NULL) {
                        print_error("Could not allocate memory", 1);
                        return 127; 
                    }
                    mode = 3;
                } else {
                    /* Manipulate the tokens since we only execute the command with args before
                        encountering a redirection operator */
                    if (!redirected) {
                        new_token_count++;
                    } else {
                        tokens[index] = '\0';
                    }
                    continue;
                }
            }
        }

        switch (mode) {
            case 1:
                if ((output_file_descriptor =  open(file_name, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0) {
                    print_error("Could not open file for writing", 1);
                    return 127;
                }
                redirected = 1;
                tokens[index] = '\0';
                break;
            case 2:
                 if ((output_file_descriptor =  open(file_name, O_CREAT | O_WRONLY | O_APPEND, 0644)) < 0) {
                    print_error("Could not open file for writing", 1);
                    return 127;
                }
                redirected = 1;
                tokens[index] = '\0';
                break;
            case 3:
                if ((input_file_descriptor =  open(file_name, O_RDONLY, 0644)) < 0) {
                    print_error("Could not open file for writing", 1);
                    return 127;
                }
                redirected = 1;
                tokens[index] = '\0';
                break;
            default:
                break;
        }
    }

    if (input_file_descriptor != STDIN_FILENO) {
        if (dup2(input_file_descriptor, STDIN_FILENO) != STDIN_FILENO) {
            print_error("Could duplicate file descriptor", 1);
            return 127;
        }
        (void) close(input_file_descriptor);
    }

    if (output_file_descriptor != STDOUT_FILENO) {
        if (dup2(output_file_descriptor, STDOUT_FILENO) != STDOUT_FILENO) {
            print_error("Could duplicate file descriptor", 1);
            return 127;
        }
        (void) close(output_file_descriptor);
    }

    *token_count = new_token_count;

    return 0;
}

/**
 * create_string_from_index takes a string and an index.
 * It will use the index as the start point and end as the length of the
 * string and then create a new string.
 **/
char *
create_string_from_index(char *input, int index) {
    int length, start;
    char *result;

    length = strlen(input);

    if ((result = malloc(length + 1)) == NULL) {
        return NULL;
    }

    result[0] = '\0';

    if ((length - index) < 2) {
        return NULL;
    }

    for (start = index; start < length; start++) {
        if (append_char(result, input[start]) != 0) {
            return NULL;
        }
    }

    return result;
}

int
append_char(char *string, char character) {
    char *temp;
    if ((temp = malloc(2)) == NULL) {
         return 1;
    }
                
    temp[0] = character;
    temp[1] = '\0';

    if (strcat(string, temp) == NULL) {
        return 1;
    }

    (void) free(temp);

    return 0;
}

/**
 * Resetting the file descriptors to the orignal file descriptors
 * in case we redirected them due to encountering redirection 
 * operators.
 **/
void
reset_file_descriptors() {
    (void) close(STDOUT_FILENO);
    (void) close(STDIN_FILENO);

    if (dup2(default_standard_output, STDOUT_FILENO) != STDOUT_FILENO) {
        print_error("Could duplicate file descriptor", 1);
        exit(127);
    }

    if (dup2(default_standard_input, STDIN_FILENO) != STDIN_FILENO) {
        print_error("Could duplicate file descriptor", 1);
        exit(127);
    }
}