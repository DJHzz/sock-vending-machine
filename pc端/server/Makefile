CC = gcc
TARGET = server
SRCS = server.c cJSON.c
CFLAGS = -lsqlite3 -lm -lpthread

all:
	$(CC) $(SRCS) -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET)