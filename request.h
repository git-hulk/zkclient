#ifndef __REQUEST_H_
#define __REQUEST_H_

#include "zkclient.h"

int authenticate(zk_client *c);
int zk_del(zk_client *c, char *path);
int zk_stat(zk_client *c, char *path, struct Stat *stat); 
int zk_exists(zk_client *c, char *path, struct Stat *stat);
int zk_set(zk_client *c, char *path, struct buffer *data); 
int zk_get(zk_client *c, char *path,  struct buffer* data); 
int zk_create(zk_client *c, char *path, char *data, int size, int flags); 
int zk_mkdir(zk_client *c, char *path); 
int zk_get_children(zk_client *c, char *path, struct String_vector *children); 
int zk_ping(zk_client *c);
int zk_close(zk_client *c);

#endif
