#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "util.h"
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
            //logger(DEBUG, "PING %s", rc == ZK_OK ? "success":"failed");
            c->last_ping = now;
        }
        usleep(200000);
    }
    return NULL;
}

static void start_ping_thread(zk_client *c) {
    int rc;
    rc = pthread_create(&c->ping_tid, NULL, do_ping_loop, c);
    if (rc != 0) {
        logger(ERROR, "start ping thread err, %s", strerror(errno));
        exit(1);
    }
}

void reset_zkclient(zk_client *c) {
    // prevent ping thread didn't unlock when call pthread_cancel, and case deadlock
    // so we trylock, and then unlock here
    pthread_mutex_trylock(&c->lock);
    pthread_mutex_unlock(&c->lock);
    // wait for ping thread
    if (c->ping_tid) {
        pthread_cancel(c->ping_tid);
    }
    if (c->sock) {
        close(c->sock);
    }
    c->sock = -1;
    c->state = ZK_STATE_INIT;
    c->last_ping = 0;
    c->session_id = 0;
    c->last_zxid = 0;
    c->passwd.len = 16;
    if (c->passwd.buff) {
        free(c->passwd.buff);
    }
    c->passwd.buff = malloc(c->passwd.len);
    memset(c->passwd.buff, 0, c->passwd.len);
}

int do_connect(zk_client *c) {
    int i, start, retries, sock, port;
    char host[512], *pos;

    start = rand() % c->nservers;
    retries = c->nservers;
    while(--retries >= 0){
        i = start++ % c->nservers; 
        pos = strrchr(c->servers[i], ':');
        if (!pos) continue;
        memcpy(host, c->servers[i], pos - c->servers[i]);
        host[pos - c->servers[i]] = '\0';
        port = atoi(pos + 1);
        TIME_START();
        sock = connect_server(host, port, c->connect_timeout);
        TIME_END();
        logger(DEBUG, "Connect to %s:%d %s, cost %d ms, after %d retries", host, port,
            sock == ZK_ERROR?"success":"failed", TIME_COST(), c->nservers - 1 - retries);
        if (sock == ZK_ERROR) continue;

        c->sock = sock;
        c->state = ZK_STATE_CONNECTED;
        if (authenticate(c) == ZK_OK) {
            start_ping_thread(c);
            c->state = ZK_STATE_AUTHED;
            return ZK_OK;
        }
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

zk_client *new_client(const char *zk_list, int session_timeout, int timeout) {
    int connect_timeout;
    if (!zk_list) return NULL;

    zk_client *c = malloc(sizeof(*c));
    if (!c) {
        // out of memory
        return NULL;
    }
    c->servers = sdssplitlen(zk_list, strlen(zk_list), ",", 1, &c->nservers);
    if(c->nservers == 0 || !c->servers) {
        return NULL;
    }
    c->state = ZK_STATE_INIT;
    c->session_id = 0;
    c->last_zxid = 0;
    c->session_timeout = session_timeout * 1000;
    if (timeout < 0) {
        connect_timeout = c->session_timeout / 3;
    } else {
        connect_timeout = timeout * 1000;
    }
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
        logger(ERROR, "Connect to zookeeper[%s] failed.", zk_list);
        exit(1);
    }

    return c;
}

void destroy_client(zk_client *c) {
    c->state = ZK_STATE_STOP;
    pthread_join(c->ping_tid, NULL);
    zk_close(c);
    sdsfreesplitres(c->servers, c->nservers);
    if (c->sock > 0) close(c->sock);
    if (c->passwd.buff) free(c->passwd.buff);
    free(c);
}

const char *zk_error(zk_client *c) {
    switch(c->last_err) {
        case 0: return "null";
        case ZK_SOCKET_ERR: return "Zkclient socket error";
        case ZK_ERROR: return "Inner error";
        case ZK_TIMEOUT: return "Connection timeout";
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
        case ZNONODE: return "Node does not exist";
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
