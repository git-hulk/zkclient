CC = gcc
CFLAGS = -g -Wall
CLIBS=
PROG = zkclient 

all: $(PROG)
.PHONY: all

OBJS= zkclient.o util.o conn.o recordio.o zookeeper.jute.o request.o
$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS) $(CLIBS)

conn.o: conn.c conn.h zkclient.h
recordio.o: recordio.c recordio.h
request.o: request.c request.h zkclient.h util.h zookeeper.jute.h \
  recordio.h
util.o: util.c util.h
zkclient.o: zkclient.c conn.h zkclient.h request.h
zookeeper.jute.o: zookeeper.jute.c zookeeper.jute.h recordio.h

clean:
	-rm -f *.o $(PROG)
