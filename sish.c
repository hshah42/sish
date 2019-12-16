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
    int case_identifier, exit, status;
    size_t input_size_max;
    char *input_command;

    (void) setprogname(argv[0]);
    exit = 0;
    input_size_max = ARG_MAX;

    input_flags.c_flag = 0;
    input_flags.x_flag = 0;

    /* Need backup of stdout/in as we need to restore in case
       of redirection */
    if ((default_standard_input = dup(STDIN_FILENO)) < 0) {
        print_error("Could not duplicate file descriptor", 1);
        return 1;
    }
    
    if ((default_standard_output = dup(STDOUT_FILENO)) < 0) {
        print_error("Could not duplicate file descriptor", 1);
        return 1;
    }

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
        if (strchr(input_command, '&')) {
            (void) execute_backgroud_process(input_command);
        } else {
            if (strchr(input_command, '|')) {
                (void) pipleline_input_commands(input_command);
            } else {
                (void) execute_command(input_command);
            }
        }
    } else {
        if (signal(SIGINT, handle_sig_int) == SIG_ERR) {
            print_error("Could not register signal", 1);
		    return 1;
	    }
        while (exit == 0) {
            if (waitpid(-1, &status, WNOHANG) > 0) {
                fprintf(stdout, "Done\n");
                if (WIFEXITED(status)) {
                    previous_exit_code = WEXITSTATUS(status);
                }
            }
            errno = 0;
            (void) setjmp(JumpBuffer);
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

            if (strchr(input_command, '&')) {
                (void) execute_backgroud_process(input_command);
            } else {
                if (strchr(input_command, '|')) {
                    (void) pipleline_input_commands(input_command);
                } else {
                    (void) execute_command(input_command);
                }
            }
        }
    }

    (void) free(input_command);

    return 0;
}

void
execute_backgroud_process(char *input_command) {
    char *last, *input_command_copy, *command;
    pid_t child;

    if ((input_command_copy = strdup(input_command)) == NULL) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    command = strtok_r(input_command_copy, "&", &last);

    while (command != NULL) {
        if ((child = fork()) < 0) {
            print_error("Could not allocate memory", 1);
            previous_exit_code = 127;
            return;
        } else if (child == 0) {
            if (strchr(input_command, '|')) {
                (void) pipleline_input_commands(command);
            } else {
                (void) execute_command(command);
            }

            exit(-1);
        }

        command = strtok_r(NULL, "&", &last);
    }
}

void
pipleline_input_commands(char *input_command) {
    int index, stdout_pipe[2], stdin_fd, stdout_fd;
    int command_count;
    pid_t child;
    int status;
    char *last, *command, *input_command_copy;

    index = 0;

    if ((command_count = get_pipe_estimate(input_command)) < 0) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    if ((input_command_copy = strdup(input_command)) == NULL) {
        print_error("Could not allocate memory", 1);
        previous_exit_code = 127;
        return;
    }

    if ((stdin_fd = dup(STDIN_FILENO)) < 1) {
        print_error("Could not duplicate file descriptor", 1);
        previous_exit_code = 127;
        return;
    }

    command = strtok_r(input_command_copy, "|", &last);

    while (command != NULL) {
        if (pipe(stdout_pipe)) {
            print_error("Could not create a pipe", 1);
            previous_exit_code = 127;
            return;
        }

        if ((command_count - index) == 1) {
            if ((stdout_fd = dup(STDOUT_FILENO)) < 0) {
                print_error("Could not duplicate file descriptor", 1);
                previous_exit_code = 127;
                return;
            }
            (void) close(stdout_pipe[1]);
        } else {
            stdout_fd = stdout_pipe[1];
        }

        if ((child = fork()) < 0) {
            print_error("Could not fork a child", 1);
            previous_exit_code = 127;
            return;
        } else if (child == 0) {
            if (dup2(stdin_fd, STDIN_FILENO) != STDIN_FILENO) {
                fprintf(stderr, "Could not duplicate fd: %s \n", strerror(errno));
                exit(127);
            }

            if (dup2(stdout_fd, STDOUT_FILENO) != STDOUT_FILENO) {
                fprintf(stderr, "Could not duplicate fd: %s \n", strerror(errno));
                exit(127);
            }

            (void) execute_command(command);
            exit(127);
        } else {
            (void) close(stdout_fd);
            (void) close(stdin_fd);
            (void) waitpid(child, &status, 0);

            if (WIFEXITED(status)) {
                previous_exit_code = WEXITSTATUS(status);
            }

            /*TODO check for status when exec is success*/

            stdin_fd = stdout_pipe[0];
        }
        
        command = strtok_r(NULL, "|", &last);
        index++;
    }

    (void) close(stdin_fd);
    (void) close(stdout_fd);
}

int
get_pipe_estimate(char *input_command) {
    int count;
    char *last, *input_command_copy, *command;

    count = 0;

    if ((input_command_copy = strdup(input_command)) == NULL) {
        return -1;
    }

    command = strtok_r(input_command_copy, "|", &last);

    while (command != NULL) {
        count++;
        command = strtok_r(NULL, "|", &last);
    }

    return count;
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
    char *last, *token, **tokens, *command_copy, *temp;
    int token_count, token_count_estimate, index, status, command_length;
    int redirection_status;
    int token_index, token_length;
    
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
        if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0 || strcmp(token, "<") == 0 ) {
            if ((tokens[index] = strdup(token)) == NULL) {
                print_error("Could not allocate memory", 1);
                previous_exit_code = 127;
                return;
            }
            token = strtok_r(NULL, " \t", &last);
            index++;
            token_count++;
            continue;
        }

        if (!(strchr(token, '<') || strchr(token, '>'))) {
            if ((tokens[index] = strdup(token)) == NULL) {
                print_error("Could not allocate memory", 1);
                previous_exit_code = 127;
                return;
            }
            index++;
            token_count++;
            token = strtok_r(NULL, " \t", &last);
            continue;
        }

        token_length = strlen(token);

        if ((temp = malloc(token_length)) == NULL) {
            print_error("Could not allocate memory", 1);
            previous_exit_code = 127;
            return;
        }

        temp[0] = '\0';

        for (token_index = 0; token_index < token_length; token_index++) {
            if (token[token_index] == '>') {
                if (strlen(temp) > 0) {
                    if ((tokens[index] = strdup(temp)) == NULL) {
                        print_error("Could not allocate memory", 1);
                        previous_exit_code = 127;
                        return;
                    }
                    temp[0] = '\0';
                    index++;
                    token_count++;
                }
                
                if ((strlen(token) - token_index) > 1 && token[token_index + 1] == '>') {
                    tokens[index] = ">>";
                    index++;
                    token_index++;
                } else {
                    tokens[index] = ">";
                    index++;
                }
                    
                token_count++;
            } else if (token[token_index] == '<') {
                if (strlen(temp) > 0) {
                    if ((tokens[index] = strdup(temp)) == NULL) {
                        print_error("Could not allocate memory", 1);
                        previous_exit_code = 127;
                        return;
                    }
                    temp[0] = '\0';
                    index++;
                    token_count++;
                }

                tokens[index] = "<";
                index++;
                token_count++;
            } else {
                (void) append_char(temp, token[token_index]);
            }
        }

        if (strlen(temp) > 0) {
            if ((tokens[index] = strdup(temp)) == NULL) {
                print_error("Could not allocate memory", 1);
                previous_exit_code = 127;
                return;
            }
            index++;
            token_count++;
        }

        token = strtok_r(NULL, " \t", &last);
        (void) free(temp);
    }

    tokens[index] = '\0';

    if (replace_dollars_in_tokens(tokens, token_count) != 0) {
        previous_exit_code = 127;
        return;
    }

    if ((redirection_status = redirect_file_descriptors(tokens, token_count)) != 0) {
        previous_exit_code = redirection_status;
        return;
    }

    if (tokens[0] == '\0') {
        (void) free(tokens);
        (void) free(command_copy);
        (void) reset_file_descriptors();
        return;
    }

    token_count = reiterate_token_count(tokens);

    if (input_flags.x_flag) {
        if (print_command(tokens, token_count) != 0) {
            return;
        }
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

    (void) reset_file_descriptors();
}

int
print_command(char **tokens, int token_count) {
    char *command_print;
    int index, total_output_length;

    total_output_length = token_count + 1;

    if ((command_print = malloc(total_output_length)) == NULL) {
        print_error("cd: Could not allocate memory", 0);
        return 127;
    }

    command_print[0] = '\0';

    for (index = 0; index < token_count; index++) {
        if (index > 0) {
            if (strcat(command_print, " ") == NULL) {
                print_error("cd: Internal error: ", 0);
            }
        }

        if (strcat(command_print, tokens[index]) == NULL) {
            print_error("cd: Internal error: ", 0);
        }
    }

    fprintf(stderr, "+ %s\n", command_print);

    (void) free(command_print);

    return 0;   
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


int
replace_dollars_in_tokens(char **tokens, int token_count) {
    int index, j_index, is_prevous_dollar, token_length, pid_length;
    int new_token_length, prev_exit_code_len, modified;
    pid_t pid;
    char *temp_token, *pid_string, *exit_code_str;

    pid = getpid();
    is_prevous_dollar = 0;
    pid_length = get_number_of_digits(pid) + 1;
    prev_exit_code_len = get_number_of_digits(previous_exit_code) + 1;
    new_token_length = -1;

    if ((pid_string = malloc(pid_length + 1)) == NULL) {
        print_error("Could not allocate memory", 1);
        return 127;        
    }

    if ((exit_code_str = malloc(prev_exit_code_len + 1)) == NULL) {
        print_error("Could not allocate memory", 1);
        return 127;        
    }

    if (snprintf(pid_string, pid_length, "%i", pid) < 0) {
        print_error("echo: ", 0);
        return 127;
    }

    if (snprintf(exit_code_str, prev_exit_code_len, "%i", previous_exit_code) < 0) {
        print_error("echo: ", 0);
        return 127;
    }

    for (index = 0; index < token_count; index++) {
        token_length = strlen(tokens[index]);
        new_token_length = token_length;
        modified = 0;
        
        for (j_index = 0; j_index < token_length; j_index++) {
            if (tokens[index][j_index] == '$') {
                if (is_prevous_dollar) {
                    new_token_length = new_token_length + pid_length - 2;
                    is_prevous_dollar = 0;
                    modified = 1;
                } else {
                    is_prevous_dollar = 1;
                }
            } else if (tokens[index][j_index] == '?') {
                if (is_prevous_dollar) {
                    new_token_length = new_token_length + prev_exit_code_len - 2;
                    is_prevous_dollar = 0;
                    modified = 1;
                }
            } else {
                new_token_length++;
                is_prevous_dollar = 0;
            }
        }

        if (is_prevous_dollar) {
            new_token_length++;
        }

        if (!modified) {
            continue;
        } else {
            if ((temp_token = malloc(new_token_length + 1)) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }

            temp_token[0] = '\0';
            is_prevous_dollar = 0;

            for (j_index = 0; j_index < token_length; j_index++) {
                if (tokens[index][j_index] == '$') {
                    if (is_prevous_dollar) {
                        if (strcat(temp_token, pid_string) == NULL) {
                            print_error("Internal error: ", 1);
                            return 127;
                        }
                        is_prevous_dollar = 0;
                    } else {
                        is_prevous_dollar = 1;
                    }
                } else if (tokens[index][j_index] == '?') {
                    if (is_prevous_dollar) {
                        if (strcat(temp_token, exit_code_str) == NULL) {
                            print_error("Internal error: ", 1);
                            return 127;
                        }
                        modified = 1;
                        is_prevous_dollar = 0;
                    } else {
                        if (append_char(temp_token, '?') != 0) {
                            print_error("Internal error: ", 1);
                            return 127;
                        }
                    }
                } else {
                    if (is_prevous_dollar) {
                        if (append_char(temp_token, '$') != 0) {
                            print_error("Internal error: ", 1);
                            return 127;
                        }
                    }

                    if (append_char(temp_token, tokens[index][j_index]) != 0) {
                        print_error("Internal error: ", 1);
                        return 127;
                    }
                    is_prevous_dollar = 0;
                }
            }

            if (is_prevous_dollar) {
                 if (append_char(temp_token, '$') != 0) {
                    print_error("Internal error: ", 1);
                    return 127;
                }
            }

            tokens[index] = '\0';
            
            if ((tokens[index] = strdup(temp_token)) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }

            (void) free(temp_token);
        }
    }

    (void) free(pid_string);
    (void) free(exit_code_str);

    return 0;
}


/**
 * echo like feature is performed on the tokens [1..n-1]
 **/
int
perform_echo(char **tokens, int token_count, int command_length) {
    char *echo_string;
    int index, total_output_length;

    total_output_length = command_length;

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

        if (strcat(echo_string, tokens[index]) == NULL) {
            print_error("cd: Internal error: ", 0);
        }
    }

    fprintf(stdout, "%s\n", echo_string);

    (void) free(echo_string);

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
        if (errno == 0) {
            fprintf(stderr, "%s: %s\n", getprogname(), message);
            return;
        }
        fprintf(stderr, "%s: %s: %s\n", getprogname(), message, strerror(errno));
    } else {
        if (errno == 0) {
            fprintf(stderr, "%s\n", message);
            return;
        }
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
redirect_file_descriptors(char **tokens, int token_count) {
    int input_file_descriptor, output_file_descriptor, index, mode;
    char *file_name, **tokens_copy;
    int count, offset;

    input_file_descriptor = STDIN_FILENO;
    output_file_descriptor = STDOUT_FILENO;

    count = token_count;
    offset = 0;

    if ((tokens_copy = malloc(token_count * sizeof(char *))) == NULL) {
        print_error("Could not allocate memory", 1);
        return 127;
    }

    for (index = 0; index < token_count; index++) {
        if ((tokens_copy[index] = strdup(tokens[index])) == NULL) {
            print_error("Could not allocate memory", 1);
            return 127;
        }
    }

    mode = 0;

    for (index = 0; index < token_count; index++) {
        mode = 0;
        if ((token_count - index) == 1) {
            if (strcmp(tokens_copy[index], ">") == 0 || 
                strcmp(tokens_copy[index], ">>") == 0|| 
                strcmp(tokens_copy[index], "<") == 0) {
                print_error("Syntax error", 1);
                return 127;
            }
        }

        if (strcmp(tokens_copy[index], ">") == 0) {
            if ((file_name = strdup(tokens_copy[index + 1])) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }
            mode = 1;
        } else if (strcmp(tokens_copy[index], ">>") == 0) {
            if ((file_name = strdup(tokens_copy[index + 1])) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }
            mode = 2;
        } else if (strcmp(tokens_copy[index], "<") == 0) {
             if ((file_name = strdup(tokens_copy[index + 1])) == NULL) {
                print_error("Could not allocate memory", 1);
                return 127;
            }
            mode = 3;
        } else {
            continue;
        }

        (void) remove_element(tokens, (index - offset), count);
        (void) remove_element(tokens, (index - offset), count);
        index++;
        offset += 2;

        if (strchr(file_name, '>') != NULL || strchr(file_name, '<') != NULL ) {
            print_error("Syntax error: redirection unexpected", 1);
            return 127;
        }

        switch (mode) {
            case 1:
                if ((output_file_descriptor = 
                        open(file_name, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0) {
                    print_error("Could not open file for writing", 1);
                    return 127;
                }
                break;
            case 2:
                 if ((output_file_descriptor = 
                        open(file_name, O_CREAT | O_WRONLY | O_APPEND, 0644)) < 0) {
                    print_error("Could not open file for writing", 1);
                    return 127;
                }
                break;
            case 3:
                if ((input_file_descriptor = 
                        open(file_name, O_RDONLY, 0644)) < 0) {
                    print_error("Could not open file for writing", 1);
                    return 127;
                }
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

    (void) free(tokens_copy);

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

void
remove_element(char **tokens, int position, int token_count) {
    int index;

    for (index = position; index < token_count; index++) {
        if ((token_count - index) == 1) {
            tokens[index] = '\0';
        } else {
            tokens[index] = tokens[index + 1];
        }
    }
}

int
reiterate_token_count(char **tokens) {
    int index;
    index = 0;
    
    while(tokens[index] != '\0') {
        index++;
    }

    return index;
}