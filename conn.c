#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include "conn.h"
#include "zkclient.h"

static int set_sock(int fd, int flag) {
    long flags;

    flags = fcntl(fd, F_GETFL, NULL);
    if (flags < 0) {
        return ZK_ERROR;
    }
    flags |= flag;     
    fcntl(fd, F_SETFL,flags);

    return ZK_OK;
}

static int set_socket_nodelay(int fd) {
    int rc, enable;

    enable = 1;
    rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    return rc < 0 ? ZK_ERROR : ZK_OK;
}


const char *host2IP(const char *host) {
    int i, n, isIP = 1;
    
    n = strlen(host);
    for (i = 0; i < n; i++) {
        if (host[i] != '.' || host[i] < '0' || host[i] > '9') {
            isIP = 0;
            break;
        }
    }
    if (isIP) return host;
    struct hostent * hp = gethostbyname(host);
    if (!hp || hp->h_length <= 0) return NULL;
    switch(hp->h_addrtype) {
        case AF_INET:
        case AF_INET6:
            return inet_ntoa(*(struct in_addr*)hp->h_addr_list[0]);
    }
    return NULL;
}

int connect_server(const char *host, int port, int timeout) {
    int sock, rc;
    const char *ip;
    struct sockaddr_in srv_addr;

    if (!host || port <= 0) return -1;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // make socket error
        return -1;
    }
    ip = host2IP(host);
    if (!ip) {
        return -1;
    }
    memset(&srv_addr, 0, sizeof(struct sockaddr_in));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port); 
    if (inet_pton(AF_INET, ip, &srv_addr.sin_addr) <= 0) goto cleanup;
    
    // O_NDELAY is the same with O_NONBLOCK in System V
    set_sock(sock, O_NDELAY);
    set_sock(sock, O_NONBLOCK);
    set_socket_nodelay(sock);
    rc = connect(sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
    if ((rc == -1) && (errno != EINPROGRESS)) goto cleanup; 
    rc = wait_socket(sock, timeout, CR_WRITE);
    if (rc == ZK_ERROR) goto cleanup;
    return sock;

cleanup:
    close(sock);
    return ZK_ERROR;
}

int do_poll(int fd, int timeout, int events) {
    int n;
    struct pollfd p;

    p.fd  = fd;
    p.events = events;
    p.revents = 0; // recevie event
    n = poll(&p, 1, timeout); 
    if (n > 0) {
        return p.revents;
    }
    return n;
}

int wait_socket(int fd, int timeout, RW_MODE rw) { 
    int rc, events = 0;

    if (rw & CR_READ) events |= POLLIN; 
    if (rw & CR_WRITE) events |= POLLOUT; 
    rc = do_poll(fd, timeout, events); 
    if (rc <= 0) return ZK_SOCKET_ERR;
    if ((rw & CR_READ) && (rc & POLLIN)) {
        return ZK_OK; 
    }
    if ((rw & CR_WRITE) && (rc & POLLOUT)) {
        return ZK_OK; 
    }
    return ZK_SOCKET_ERR;
}
