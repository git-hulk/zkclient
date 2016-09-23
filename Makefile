CC = gcc
CFLAGS = -g -Wall
CLIBS= -lpthread
PROG = zkclient 

all: $(PROG)
.PHONY: all

OBJS= zkclient.o util.o conn.o recordio.o zookeeper.jute.o request.o main.o cJSON/cJSON.o linenoise/linenoise.o
$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS) $(CLIBS)

conn.o: conn.c conn.h zkclient.h zookeeper.jute.h recordio.h
main.o: main.c util.h request.h zkclient.h zookeeper.jute.h recordio.h \
  cJSON/cJSON.h linenoise/linenoise.h
recordio.o: recordio.c recordio.h
request.o: request.c request.h zkclient.h zookeeper.jute.h recordio.h \
  util.h conn.h
util.o: util.c util.h
zkclient.o: zkclient.c conn.h request.h zkclient.h zookeeper.jute.h \
  recordio.h
zookeeper.jute.o: zookeeper.jute.c zookeeper.jute.h recordio.h

clean:
	-rm -f *.o $(PROG)
	- cd cJSON && make clean && cd ..
	- cd linenoise && make clean && cd ..
