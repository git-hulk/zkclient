#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <errno.h>

#include "request.h"
#include "util.h"
#include "conn.h"
#include "zkclient.h"
#include "zookeeper.jute.h"

#define NOTIFY_OPCODE 0
#define CREATE_OPCODE 1
#define DELETE_OPCODE 2
#define EXISTS_OPCODE 3
#define GETDATA_OPCODE 4
#define SETDATA_OPCODE 5
#define GETACL_OPCODE 6
#define SETACL_OPCODE 7
#define GETCHILDREN_OPCODE 8
#define SYNC_OPCODE 9
#define PING_OPCODE 11
#define GETCHILDREN2_OPCODE 12
#define CHECK_OPCODE 13
#define MULTI_OPCODE 14
#define SETAUTH_OPCODE 100
#define SETWATCHES_OPCODE 101
#define CLOSE_OPCODE -11

#define PROTOCOL_VERSION 0
#define PERM_ALL 0x1f
struct ACL acls[] = {
    {PERM_ALL, {"world", "anyone"}}
};
struct ACL_vector default_acl = {1, acls};

static int32_t decode_int32(char *buf, int off) {
    int32_t i32 = 0;

    i32 |= (uint8_t)buf[off] << 24;
    i32 |= (uint8_t)buf[off+1] << 16;
    i32 |= (uint8_t)buf[off+2] << 8;
    i32 |= (uint8_t)buf[off+3] & 0xff;
    return i32;
}

static int read_socket(int fd, char *buf, int len) {
    int bytes, r_bytes = 0;

    while (r_bytes < len) {
        bytes = read(fd, buf + r_bytes, len - r_bytes);
        r_bytes += bytes; 
        if (bytes == -1 &&
            (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)) {
            continue;
        }
        if (bytes == 0) {
            // close
            close(fd);
            return ZK_ERROR;
        }
    }

    return ZK_OK;
}

static int send_request(zk_client *c, struct oarchive *oa) {
    int rc, len, w_bytes, bytes;
    char *buf;
    
    len = get_buffer_len(oa);
    buf = malloc(len + 4);
    if (!buf) {
        // out of memory
        return ZK_ERROR;
    }

    buf[0] = len >> 24;
    buf[1] = len >> 16;
    buf[2] = len >> 8;
    buf[3] = len & 0xff;
    memcpy(buf + 4, get_buffer(oa), len);

    rc = wait_socket(c->sock, c->write_timeout, CR_WRITE);
    if (rc != ZK_OK) {
        fprintf(stderr, "send data to server failed, %s.", strerror(errno));
        return ZK_ERROR;
    }
    w_bytes = 0;
    while(w_bytes < len + 4) {
        bytes = write(c->sock, buf + w_bytes, len + 4 - w_bytes);
        if (bytes == -1 && errno != EAGAIN && errno != EINTR) {
            goto cleanup;
        }
        w_bytes += bytes;
    }
    free(buf);
    return ZK_OK;

cleanup:
    free(buf);
    return ZK_ERROR;
}

static int add_request_header(struct oarchive *oa, int opcode) {
    int32_t xid;
    xid = PING_OPCODE == opcode ? -2 : get_xid();
    struct RequestHeader header = {xid, opcode};
    return serialize_RequestHeader(oa, "header", &header);
}

static int decode_reply_header(struct iarchive *ia) {
    int rc;
    struct ReplyHeader reply_header;

    rc = deserialize_ReplyHeader(ia, "header", &reply_header);
    if (rc < 0) return ZK_ERROR;
    return reply_header.err;
}

struct iarchive *recv_response(zk_client *c) {
    int rc, len;
    char buf[4], *recv_buf;
    
    rc = wait_socket(c->sock, c->read_timeout, CR_READ);
    if(rc != ZK_OK) {
        fprintf(stderr, "Connection Timeout, as %s.", strerror(errno));
        return NULL;
    }

    rc = read_socket(c->sock, buf, 4); 
    if (rc == ZK_ERROR) return NULL;

    len = decode_int32(buf, 0);
    recv_buf = malloc(len);
    rc = read_socket(c->sock, recv_buf, len);
    if (rc == ZK_ERROR) {
        free(recv_buf);
        return NULL;
    }

    return create_buffer_iarchive(recv_buf, len);
}

static void destory_archive(struct oarchive *oa, struct iarchive *ia) {
    if (oa) close_buffer_oarchive(&oa, 1); 
    if (ia) {
        free(((struct buffer*)(ia->priv))->buff);
        close_buffer_iarchive(&ia);
    }
}

int authenticate(zk_client *c) {
    int rc;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;
    struct ConnectResponse resp;

    struct ConnectRequest req = {
        PROTOCOL_VERSION,
        c->last_zxid,
        c->session_timeout,
        c->session_id,
        c->passwd
    };
    oa = create_buffer_oarchive();
    rc = serialize_ConnectRequest(oa, "auth", &req);
    rc = rc < 0 ? rc : send_request(c, oa);
    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        goto END;
    }

    rc = deserialize_ConnectResponse(ia, "auth", &resp);
    c->session_id = resp.sessionId;
    c->session_timeout = resp.timeOut;
    if (c->passwd.len != resp.passwd.len || memcmp(c->passwd.buff, resp.passwd.buff, c->passwd.len)) {
        free(c->passwd.buff);
        c->passwd.buff = malloc(resp.passwd.len);
        memcpy(c->passwd.buff, resp.passwd.buff, c->passwd.len);
        c->passwd.len = resp.passwd.len;
    }
    deallocate_ConnectResponse(&resp);

END:
    destory_archive(oa, ia);
    return rc < 0 ? ZK_ERROR : ZK_OK;
}

int zk_create(zk_client *c, char *path, char *data, int size, int flags) {
    int rc, err;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;;
    struct buffer value;
    struct CreateResponse resp;

    if (!c || !path) return ZK_ERROR;
    c->last_err = 0;
    oa = create_buffer_oarchive();
    rc = add_request_header(oa, CREATE_OPCODE);
    value.len = 0;
    if (data && size > 0) {
        value.buff = data; 
        value.len = size;
    }
    struct CreateRequest req = {path, value, default_acl, flags};
    rc = rc < 0 ? rc : serialize_CreateRequest(oa, "req", &req);

    pthread_mutex_lock(&c->lock);
    rc = rc < 0 ? rc : send_request(c, oa);
    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        err = ZK_ERROR;
        goto ERROR;
    }
    pthread_mutex_unlock(&c->lock);

    if ((err = decode_reply_header(ia))) {
        goto ERROR;
    }
    rc = deserialize_CreateResponse(ia, "resp", &resp);
    deallocate_CreateResponse(&resp);
    destory_archive(oa, ia);
    return rc < 0 ? ZK_ERROR : ZK_OK;

ERROR:
    pthread_mutex_unlock(&c->lock);
    destory_archive(oa, ia);
    return err; 
}

int zk_exists(zk_client *c, char *path, struct Stat *stat) {
    int rc, err, result;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;
    struct ExistsResponse resp;

    if (!c || !path) return ZK_ERROR;
    c->last_err = 0;
    // send exist request
    oa = create_buffer_oarchive();
    struct ExistsRequest req = {path, 0};
    rc = add_request_header(oa, EXISTS_OPCODE);
    rc = rc < 0 ? rc : serialize_ExistsRequest(oa, "req", &req);

    pthread_mutex_lock(&c->lock);
    rc = rc < 0 ? rc : send_request(c, oa);

    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        err = ZK_ERROR;
        goto ERROR;
    }
    pthread_mutex_unlock(&c->lock);

    if ((err = decode_reply_header(ia)) && err != ZNONODE) {
        goto ERROR;
    }
    result = err == ZNONODE ? 0 : 1;
    if (result) {
        deserialize_ExistsResponse(ia, "resp", &resp);
        if (stat) {
            *stat = resp.stat;
        }
        deallocate_ExistsResponse(&resp);
    }
    destory_archive(oa, ia);
    return result;

ERROR:
    pthread_mutex_unlock(&c->lock);
    destory_archive(oa, ia);
    c->last_err = err;
    return err;
}

int zk_stat(zk_client *c, char *path, struct Stat *stat) {
    int rc; 

    if (!c || !path) return ZK_ERROR;
    rc = zk_exists(c, path, stat); 
    return rc == 1 ? ZK_OK : rc;
}

int zk_get(zk_client *c, char *path,  struct buffer *data) {
    int rc, err;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;
    struct GetDataResponse resp;

    if (!c || !path || !data) {
        return ZK_ERROR;
    }
    c->last_err = 0;
    oa = create_buffer_oarchive();
    struct GetDataRequest req = {path, 0};
    rc = add_request_header(oa, GETDATA_OPCODE);
    rc = rc < 0 ? rc : serialize_GetDataRequest(oa, "req", &req);

    pthread_mutex_lock(&c->lock);
    rc = rc < 0 ? rc : send_request(c, oa);
    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        err = ZK_ERROR;
        goto ERROR;
    }
    pthread_mutex_unlock(&c->lock);

    if ((err = decode_reply_header(ia))) {
        goto ERROR;
    }
    rc = deserialize_GetDataResponse(ia, "resp", &resp);
    if (rc < 0) goto ERROR;
    *data = resp.data;
    destory_archive(oa, ia);
    return  ZK_OK;

ERROR:
    pthread_mutex_unlock(&c->lock);
    destory_archive(oa, ia);
    c->last_err = err;
    return err;
}

int zk_del(zk_client *c, char *path) {
    int rc, err;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;

    if (!c || !path) {
        return ZK_ERROR;
    }
    c->last_err = 0;
    oa = create_buffer_oarchive();
    rc = add_request_header(oa, DELETE_OPCODE);
    struct DeleteRequest req = {path, -1};
    rc = rc < 0 ? rc : serialize_DeleteRequest(oa, "req", &req);

    pthread_mutex_unlock(&c->lock);
    rc = rc < 0 ? rc : send_request(c, oa);
    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        err = ZK_ERROR;
        goto ERROR;
    }
    pthread_mutex_unlock(&c->lock);

    if ((err = decode_reply_header(ia))) {
        goto ERROR;
    }
    destory_archive(oa, ia);
    return ZK_OK;

ERROR:
    pthread_mutex_unlock(&c->lock);
    destory_archive(oa, ia);
    c->last_err = err;
    return err;
}


int zk_set(zk_client *c, char *path, struct buffer *data) {
    int rc, err;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;
    struct SetDataResponse resp;

    if (!c || !path) {
        return ZK_ERROR;
    }
    c->last_err = 0;
    oa = create_buffer_oarchive();
    rc = add_request_header(oa, SETDATA_OPCODE);
    struct SetDataRequest req = {path, *data, -1};
    rc = rc < 0 ? rc : serialize_SetDataRequest(oa, "req", &req);

    pthread_mutex_lock(&c->lock);
    rc = rc < 0 ? rc : send_request(c, oa);
    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        err = ZK_ERROR;
        goto ERROR;
    }
    pthread_mutex_unlock(&c->lock);

    if ((err = decode_reply_header(ia))) {
        goto ERROR;
    }
    rc = deserialize_SetDataResponse(ia, "resp", &resp);
    destory_archive(oa, ia);
    deallocate_SetDataResponse(&resp);
    return rc < 0 ? ZK_ERROR : ZK_OK;

ERROR:
    pthread_mutex_unlock(&c->lock);
    destory_archive(oa, ia);
    c->last_err = err;
    return err;
}

int zk_get_children(zk_client *c, char *path, struct String_vector *children) {
    int rc, err;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;
    struct GetChildrenResponse resp;

    if (!c || !path) {
        return ZK_ERROR;
    }
    c->last_err = 0;
    oa = create_buffer_oarchive();
    rc = add_request_header(oa, GETCHILDREN_OPCODE);
    struct GetChildrenRequest req = {path, 0};
    rc = rc < 0 ? rc : serialize_GetChildrenRequest(oa, "req", &req);

    pthread_mutex_lock(&c->lock);
    rc = rc < 0 ? rc : send_request(c, oa);
    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        err = ZK_ERROR;
        goto ERROR;
    }
    pthread_mutex_unlock(&c->lock);

    if ((err = decode_reply_header(ia))) {
        goto ERROR;
    }
    rc = deserialize_GetChildrenResponse(ia, "resp", &resp);
    if (rc < 0) goto ERROR;

    *children = resp.children;
    destory_archive(oa, ia);
    return ZK_OK;

ERROR:
    pthread_mutex_unlock(&c->lock);
    destory_archive(oa, ia);
    c->last_err = err;
    return err;
}

static int do_header_request(zk_client *c, int opcode) {
    int rc, err;
    struct oarchive *oa = NULL;
    struct iarchive *ia = NULL;

    if (!c) return ZK_ERROR;
    c->last_err = 0;
    oa = create_buffer_oarchive();
    rc = add_request_header(oa, opcode);

    rc = rc < 0 ? rc : send_request(c, oa);
    if (rc == ZK_ERROR || !(ia = recv_response(c))) {
        err = ZK_ERROR;
        goto ERROR;
    }

    if ((err = decode_reply_header(ia))) {
        goto ERROR;
    }
    return ZK_OK;

ERROR:
    destory_archive(oa, ia);
    c->last_err = err;
    return err;
}

int zk_ping(zk_client *c) {
    int rc;

    pthread_mutex_lock(&c->lock);
    rc = do_header_request(c, PING_OPCODE);
    pthread_mutex_unlock(&c->lock);

    return rc;
}

int zk_close(zk_client *c) {
    int rc;

    rc = do_header_request(c, CLOSE_OPCODE);
    close(c->sock);
    return rc;
}
