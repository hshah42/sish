#define BUFFERSIZE 4294967296

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
int default_standard_output;
int default_standard_input;

void print_usage();
void strip_new_line(char *input);
void execute_command(char *command);
void print_error(char *message, int include_prog_name);
void handle_sig_int(int signal);
void reset_file_descriptors();

int get_token_count(char *command);
int perform_directory_change(char *directory);
int perform_echo(char **tokens, int token_count, int command_length);
int perform_exec(char **tokens);
int append_char(char *string, char character);
int redirect_file_descriptors(char **tokens, int *token_count);

unsigned int get_number_of_digits(int number);

char * create_string_from_index(char *input, int index);