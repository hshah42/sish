struct flags {
    int c_flag;
    int x_flag;
};

struct result {
    char *output;
    char *error;
    int   type;
};

int previous_exit_code = 0;

void print_usage();
void strip_new_line(char *input);
void execute_command(char *command);
void print_error(char *message, int include_prog_name);
void handle_sig_int(int signal);

int get_token_count(char *command);
int perform_directory_change(char *directory);
int perform_echo(char **tokens, int token_count, int command_length);

unsigned int get_number_of_digits(int number);