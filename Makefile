CC = gcc
CFLAGS = -g -Wall
CLIBS= -lpthread -lm
PROG = zkclient 

ifeq ($(UNAME), Darwin)
	LDFLAGS=-Wl,-flat_namespace,-undefined,dynamic_lookup
endif

INSTALL=/usr/bin/install
INSTALLDIR=/usr/local
BINDIR=$(INSTALLDIR)/bin

all: $(PROG)
.PHONY: all

OBJS= zkclient.o util.o conn.o recordio.o zookeeper.jute.o request.o main.o cJSON/cJSON.o linenoise/linenoise.o
$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS) $(CLIBS) $(LDFLAGS)

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

install:
	mkdir -p $(BINDIR)
	$(INSTALL) $(PROG) $(BINDIR)

clean:
	-rm -f *.o $(PROG)
	- cd cJSON && make clean && cd ..
	- cd linenoise && make clean && cd ..
