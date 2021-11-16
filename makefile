CC=gcc
CXX=g++

ifndef window
window = 75
endif

CFLAGS = -g -c -Wall -pedantic
CPPFLAGS = -std=c++17 -L/include

clean:
	rm *.o
	rm mcast

%.o:    %.c
	$(CC) $(CFLAGS) $*.c

%.o:	%.cpp
	$(CXX)  $(CFLAGS) $*.cpp

%.o:	%.hpp
	$(CXX) $(CPPFLAGS) $(CFLAGS) $*.hpp