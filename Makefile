CC = gcc
CFLAGS = -g -Wall -DTHREADED -I./hashtable
CLIBS = -lpthread -lm

PROG1 = cli 
PROG2 = kafka_inspect
PROG3 = zkclient

INSTALL=/usr/bin/install
INSTALLDIR=/usr/local
BINDIR=$(INSTALLDIR)/bin

all: $(PROG1) $(PROG2) $(PROG3) 
.PHONY: all

OBJS= recordio.o mt_adaptor.o winport.o zk_hashtable.o zk_log.o zookeeper.o zookeeper.jute.o hashtable/hashtable.o hashtable/hashtable_itr.o 
CLI_OBJS= cli.o $(OBJS)
KAFKA_INSPECT_OBJS= cJSON/cJSON.o kafka_inspect.o $(OBJS)
ZKCLIENT_OBJS= zkclient.o cJSON/cJSON.o linenoise/linenoise.o $(OBJS)

$(PROG1): $(CLI_OBJS)
	$(CC) $(CFLAGS) $(CINCLUDES) -o $(PROG1) $(CLI_OBJS) $(CLIBS)
$(PROG2): $(KAFKA_INSPECT_OBJS)
	$(CC) $(CFLAGS) $(CINCLUDES) -o $(PROG2) $(KAFKA_INSPECT_OBJS) $(CLIBS)
$(PROG3): $(ZKCLIENT_OBJS)
	$(CC) $(CFLAGS) $(CINCLUDES) -o $(PROG3) $(ZKCLIENT_OBJS) $(CLIBS)

cli.o: cli.c zookeeper.h proto.h zookeeper_version.h recordio.h \
  zookeeper.jute.h
kafka_inspect.o: kafka_inspect.c zookeeper.h proto.h zookeeper_version.h \
  recordio.h zookeeper.jute.h cJSON/cJSON.h
load_gen.o: load_gen.c zookeeper.h proto.h zookeeper_version.h recordio.h \
  zookeeper.jute.h zookeeper_log.h
mt_adaptor.o: mt_adaptor.c zk_adaptor.h zookeeper.jute.h recordio.h \
  zookeeper.h proto.h zookeeper_version.h zk_hashtable.h zookeeper_log.h
recordio.o: recordio.c recordio.h
st_adaptor.o: st_adaptor.c zk_adaptor.h zookeeper.jute.h recordio.h \
  zookeeper.h proto.h zookeeper_version.h zk_hashtable.h
winport.o: winport.c
zk_hashtable.o: zk_hashtable.c zk_hashtable.h zookeeper.h proto.h \
  zookeeper_version.h recordio.h zookeeper.jute.h zk_adaptor.h \
  hashtable/hashtable.h hashtable/hashtable_itr.h \
  hashtable/hashtable_private.h
zk_log.o: zk_log.c zookeeper_log.h zookeeper.h proto.h \
  zookeeper_version.h recordio.h zookeeper.jute.h
zkclient.o: zkclient.c zookeeper.h proto.h zookeeper_version.h recordio.h \
  zookeeper.jute.h cJSON/cJSON.h linenoise/linenoise.h
zookeeper.o: zookeeper.c zookeeper.h proto.h zookeeper_version.h \
  recordio.h zookeeper.jute.h zk_adaptor.h zk_hashtable.h \
  zookeeper_log.h config.h
zookeeper.jute.o: zookeeper.jute.c zookeeper.jute.h recordio.h

clean:
	- rm -rf *.o hashtable/*.o $(PROG1) $(PROG2) $(PROG3)
	- cd cJSON && make clean && cd ..
	- cd linenoise && make clean && cd ..

