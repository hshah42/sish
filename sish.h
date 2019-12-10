struct flags {
    int c_flag;
    int x_flag;
};

void print_usage();
void strip_new_line(char *input);
void execute_command(char *command);

int get_token_count(char *command);