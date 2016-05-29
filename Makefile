CC = g++
CXXFLAGS = -Wall -std=c++11 -O2
LFLAGS = -Wall
TARGETS = player master

all: $(TARGETS)

player: player.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex

master: master.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex

clean:
	rm -f $(TARGETS) *.o *~ *.bak

.PHONY: all clean
