CC = cc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lsqlite3 -lreadline

%: %.c
	$(CC) $(CFLAGS) $< -o bin/$@ $(LDFLAGS)
