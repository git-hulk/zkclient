#ifndef __ZKCLIENT_H_
#define __ZKCLIENT_H_

#include "zookeeper.jute.h"
#include <pthread.h>

#define ZK_OK 0
#define ZK_ERROR -10000
#define ZK_TIMEOUT -10001
#define ZK_SOCKET_ERR -10002

#define ZK_STATE_INIT 0
#define ZK_STATE_CONNECTED 1
#define ZK_STATE_AUTHED 2
#define ZK_STATE_STOP 3

enum ZK_ERRORS {
  ZOK = 0, /*!< Everything is OK */

  /** System and server-side errors.
   * This is never thrown by the server, it shouldn't be used other than
   * to indicate a range. Specifically error codes greater than this
   * value, but lesser than {@link #ZAPIERROR}, are system errors. */
  ZSYSTEMERROR = -1,
  ZRUNTIMEINCONSISTENCY = -2, /*!< A runtime inconsistency was found */
  ZDATAINCONSISTENCY = -3, /*!< A data inconsistency was found */
  ZCONNECTIONLOSS = -4, /*!< Connection to the server has been lost */
  ZMARSHALLINGERROR = -5, /*!< Error while marshalling or unmarshalling data */
  ZUNIMPLEMENTED = -6, /*!< Operation is unimplemented */
  ZOPERATIONTIMEOUT = -7, /*!< Operation timeout */
  ZBADARGUMENTS = -8, /*!< Invalid arguments */
  ZINVALIDSTATE = -9, /*!< Invliad zhandle state */

  ZAPIERROR = -100,
  ZNONODE = -101, /*!< Node does not exist */
  ZNOAUTH = -102, /*!< Not authenticated */
  ZBADVERSION = -103, /*!< Version conflict */
  ZNOCHILDRENFOREPHEMERALS = -108, /*!< Ephemeral nodes may not have children */
  ZNODEEXISTS = -110, /*!< The node already exists */
  ZNOTEMPTY = -111, /*!< The node has children */
  ZSESSIONEXPIRED = -112, /*!< The session has been expired by the server */
  ZINVALIDCALLBACK = -113, /*!< Invalid callback specified */
  ZINVALIDACL = -114, /*!< Invalid ACL specified */
  ZAUTHFAILED = -115, /*!< Client authentication failed */
  ZCLOSING = -116, /*!< ZooKeeper is closing */
  ZNOTHING = -117, /*!< (not error) no server responses to process */
  ZSESSIONMOVED = -118 /*!<session moved to another server, so operation is ignored */
};

struct _zk_client {
    int sock;
    int nservers;
    char **servers;
    int last_zxid;
    int session_id;
    int session_timeout;
    int connect_timeout;
    int read_timeout;
    int write_timeout;
    int state;
    int32_t last_err;
    struct buffer passwd;
    int last_ping;
    pthread_t ping_tid;
    pthread_mutex_t lock;
};

typedef struct _zk_client zk_client;
zk_client *new_client(const char *zk_list, int session_timeout, int timeout); 
int do_connect(zk_client *c); 
void set_connect_timeout(zk_client *c, int timeout); 
void set_socket_timeout(zk_client *c, int timeout); 
void destroy_client(zk_client *c); 
void reset_zkclient(zk_client *c); 
const char *zk_error(zk_client *c);
#endif
