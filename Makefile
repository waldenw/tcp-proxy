CFLAGS = -I. -std=c99 -g -D_GNU_SOURCE -Wall

SRCS = tcp-proxy.c

OBJS = $(SRCS:.c=.o)
LIBS = 
BIN = tcp-proxy

CC=gcc

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) -o $(BIN) $(OBJS)

clean:
	$(RM) $(BIN) $(OBJS)

