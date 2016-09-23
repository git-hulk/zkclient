#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
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

int connect_server(const char *host, int port, int timeout) {
    int sock, rc;
    const char *ip;
    struct sockaddr_in srv_addr;

    if (!host || port <= 0) return -1;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // make socket error
        return -1;
    }
    // TODO: check host type, ip or domain
    ip = host;
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

int wait_socket(int fd, int timeout, RW_MODE rw) { 
    int n;
    struct pollfd p;

    p.fd  = fd;
    p.events = 0;
    if (rw & CR_READ) p.events |= POLLIN; 
    if (rw & CR_WRITE) p.events |= POLLOUT; 
    p.revents = 0; // recevie event
    n = poll(&p, 1, timeout); 
    if (n <= 0) return ZK_ERROR;
    if ((rw & CR_READ) && (p.revents & POLLIN)) {
        return ZK_OK; 
    }
    if ((rw & CR_WRITE) && (p.revents & POLLOUT)) {
        return ZK_OK; 
    }
    return ZK_ERROR;
}
