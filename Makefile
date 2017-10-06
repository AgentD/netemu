CFLAGS := -std=c99 -pedantic -Wall -Wextra -D_GNU_SOURCE -ggdb3
LDFLAGS += -ggdb3

VPATH = driver/

.PHONY: all
all: nettool testtool

nettool: nettool.o cfg.o netns.o cfg_token.o cfg_parse.o node.o switch.o \
	cable.o driver.o graph.o
	$(CC) $^ -lm -o $@

testtool: testtool.o netns.o daemon.o
	$(CC) $^ -o $@

cfg_parse.o: cfg_parse.c cfg.h
cfg_token.o: cfg_token.c cfg.h
testtool.o: testtool.c netns.h
nettool.o: nettool.c cfg.h daemon.h driver.h
switch.o: switch.c cfg.h netns.h driver.h driver/switch.h driver/node.h
daemon.o: daemon.c daemon.h netns.h
driver.o: driver.c driver.h
cable.o: cable.c cfg.h netns.h driver.h driver/cable.h driver/node.h
netns.o: netns.c netns.h
graph.o: graph.c driver.h
node.o: node.c netns.h cfg.h driver/node.h driver.h
cfg.o: cfg.c cfg.h

.PHONY: clean
clean:
	$(RM) *.o nettool testtool

