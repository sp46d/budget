CC = cc
CFLAGS = -Wall -Wextra -g record.c
LDFLAGS = -lsqlite3 -lreadline

%: %.c
	$(CC) $(CFLAGS) $< -o bin/$@ $(LDFLAGS)
