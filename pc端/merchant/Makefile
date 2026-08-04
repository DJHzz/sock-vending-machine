CC = gcc
TARGET = merchant
SRCS = merchant_tool.c cJSON.c
CFLAGS = -lm -lpthread

all:
	$(CC) $(SRCS) -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET)