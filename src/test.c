#include <fcntl.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char* line;
    while ((line = readline("budget> ")) != NULL) {
        if (*line) {
            add_history(line);
        }
        if (strcmp(line, "exit") == 0) {
            free(line);
            break;
        }
        printf("Received: %s\n", line);
        free(line);
    }

    return 0;
}
