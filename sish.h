struct flags {
    int c_flag;
    int x_flag;
};

struct result {
    char *output;
    char *error;
    int   type;
};

void print_usage();
void strip_new_line(char *input);
void execute_command(char *command);
void print_error(char *message);

int get_token_count(char *command);