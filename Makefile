CFLAGS = -Wall -Wvla -pedantic-errors -std=c99 -D _XOPEN_SOURCE=500

SERVER_FILES = hashmap.c buffer.c protocol.c game.c server.c
SERVER_OBJECTS = $(SERVER_FILES:.c=.o)

CLIENT_FILES = buffer.c protocol.c client.c
CLIENT_OBJECTS = $(CLIENT_FILES:.c=.o)

all: server client

server: $(SERVER_OBJECTS)

client: $(CLIENT_OBJECTS)
client: LDLIBS = -lncurses

clean:
	        rm server $(SERVER_OBJECTS)
	        rm client $(CLIENT_OBJECTS)
