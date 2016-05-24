CC = g++
CFLAGS = -Wall -O2
CXXFLAGS = -Wall -O2
LFLAGS = -Wall
TARGETS = player

all: $(TARGETS)

err.o: err.c err.h

player.o: player.cpp err.h

player: player.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex

clean:
	rm -f $(TARGETS) *.o *~ *.bak

.PHONY: all clean
