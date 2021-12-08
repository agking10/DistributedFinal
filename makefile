CC=gcc
CXX=g++

CFLAGS = -g -c -Wall -pedantic
CPPFLAGS = -std=c++17 -I include
SP_LIBRARY = include/libspread-core.a include/libspread-util.a

CLIENT_OBJS = client_main.o
SERVER_OBJS = server_main.o

all: client server
	sh install_dependencies.sh

client: $(CLIENT_OBJS)
	$(CXX) -o client $(CLIENT_OBJS) -I include -ldl $(SP_LIBRARY)

server: $(SERVER_OBJS)
	$(CXX) -o server $(SERVER_OBJS) -I include -ldl $(SP_LIBRARY)

clean:
	rm *.o
	rm client
	rm server

%.o:    %.c
	$(CC) $(CFLAGS) $*.c

%.o:	%.cpp
	$(CXX) $(CFLAGS) $(CPPFLAGS) $*.cpp