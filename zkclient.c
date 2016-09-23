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
}


int main(int argc, char **argv) {
     struct Stat stat;
    struct String_vector children;
    zk_client *c = new_client("127.0.0.1", 2181, 60);
    set_connect_timeout(c, 2000);
    set_socket_timeout(c, 2000);

    int status = zk_exists(c, "/", &stat);
    //int status = zk_create(c, "/abc/test", "abc", 3, 0);
    //int status = zk_del(c, "/abc/test");
    printf("%d\n", status);
    sleep(100);
    destroy_client(c);
    return 0;
}
