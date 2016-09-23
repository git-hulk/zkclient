#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "conn.h"
#include "request.h"
#include "zkclient.h"

static void* do_ping_loop(void *v) {
    int now;
    zk_client *c = v;

    while(c->state != ZK_STATE_STOP) {
        now = time(NULL);
        // session timeout is ms, so we need to div 6 *1000
        if(now - c->last_ping >= c->session_timeout/1000/6) {
            // send ping
            zk_ping(c);
            c->last_ping = now;
        }
        usleep(200000);
    }
    return NULL;
}

static void start_ping_thread(zk_client *c) {
    int rc;
    pthread_t tid;

    rc = pthread_create(&tid, NULL, do_ping_loop, c);
    if (rc != 0) {
        fprintf(stderr, "start ping thread err, %s", strerror(errno));
        exit(1);
    }
}

int do_connect(zk_client *c) {
    int sock;

    sock = connect_server(c->host, c->port, c->connect_timeout);
    if (sock == ZK_ERROR) return ZK_ERROR;
    c->sock = sock;
    c->state = ZK_STATE_CONNECTED;
    if (authenticate(c) == ZK_OK) {
        start_ping_thread(c);
        c->state = ZK_STATE_AUTHED;
        return ZK_OK;
    }
    return ZK_ERROR;
}

void set_connect_timeout(zk_client *c, int timeout) {
    if (!c || timeout < 0) {
        return;
    }
    c->connect_timeout = timeout;
}

void set_socket_timeout(zk_client *c, int timeout) {
    if (!c || timeout < 0) {
        return;
    }
    c->read_timeout = timeout;
    c->write_timeout = timeout;
}

zk_client *new_client(const char *host, int port, int session_timeout) {
    int connect_timeout;
    if (!host || port <= 0) return NULL;

    zk_client *c = malloc(sizeof(*c));
    if (!c) {
        // out of memory
        return NULL;
    }
    c->host = strdup(host);
    c->port = port;
    c->state = ZK_STATE_INIT;
    c->session_id = 0;
    c->last_zxid = 0;
    c->session_timeout = session_timeout * 1000;
    connect_timeout = c->session_timeout / 3;
    c->connect_timeout = connect_timeout;
    c->read_timeout = connect_timeout;
    c->write_timeout = connect_timeout;
    c->sock = -1; 
    c->passwd.len = 16;
    c->passwd.buff = malloc(c->passwd.len);
    memset(c->passwd.buff, 0, c->passwd.len);
    c->last_ping = time(NULL);
    pthread_mutex_init(&c->lock, NULL);

    if (do_connect(c) != ZK_OK) {
        fprintf(stderr, "Connect to zookeeper[%s:%d] failed.\n", host, port);
        exit(1);
    }

    return c;
}

void destroy_client(zk_client *c) {
    zk_close(c);
    if (c->host) free(c->host);
    if (c->sock > 0) close(c->sock);
    if (c->passwd.buff) free(c->passwd.buff);
    free(c);
    c->state = ZK_STATE_STOP;
}

const char *zk_error(zk_client *c) {
    switch(c->last_err) {
        case 0: return "null";
        case ZK_ERROR: return "Inner error";
        case ZSYSTEMERROR: return "System error";
        case ZRUNTIMEINCONSISTENCY: return "A runtime inconsistency was found";
        case ZDATAINCONSISTENCY: return "A data inconsistency was found";
        case ZCONNECTIONLOSS: return "Connection to the server has been lost";
        case ZMARSHALLINGERROR: return "Error while marshalling or unmarshalling data";
        case ZUNIMPLEMENTED: return "Operation is unimplemented";
        case ZOPERATIONTIMEOUT: return "Operation timeout";
        case ZBADARGUMENTS: return "Invalid arguments";
        case ZINVALIDSTATE: return "Invliad client state";
        case ZAPIERROR: return "Api error";
        case ZNONODE: return "Node does not exist ";
        case ZNOAUTH: return "Not authenticated";
        case ZBADVERSION: return "Version conflict";
        case ZNOCHILDRENFOREPHEMERALS: return "Ephemeral nodes may not have children";
        case ZNODEEXISTS: return "The node already exists";
        case ZNOTEMPTY: return "The node has children";
        case ZSESSIONEXPIRED: return "The session has been expired by the server";
        case ZINVALIDCALLBACK: return "Invalid callback specified";
        case ZINVALIDACL: return "Invalid ACL specified";
        case ZAUTHFAILED: return "Client authentication failed";
        case ZCLOSING: return "ZooKeeper is closing";
        case ZNOTHING: return "(not error) no server responses to process";
        case ZSESSIONMOVED: return "Session moved to another server, so operation is ignored";
        default: return "unknown error";
    }
}
