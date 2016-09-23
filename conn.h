#ifndef __CONN_H_
#define __CONN_H_

typedef enum {
    CR_READ = 1,
    CR_WRITE = 2,
    CR_RW = 3
} RW_MODE;

int wait_socket(int fd, int timeout, RW_MODE rw); 
int connect_server(const char *host, int port, int timeout);
#endif
