#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "util.h"
#include "request.h"
#include "zkclient.h"
#include "cJSON/cJSON.h"
#include "linenoise/linenoise.h"

#define LS_CMD   "ls"
#define GET_CMD  "get"
#define CREATE_CMD "create"
#define SET_CMD  "set"
#define QUIT_CMD "quit"
#define EXIT_CMD "exit"
#define STAT_CMD "stat"
#define DEL_CMD  "del" 

#define PROMPT "zkclient> "
#define HISTORY_FILE_PATH "/tmp/.zkclient_history.txt"
#define STRING_EQUAL(a, b)  \
((strlen(a) == strlen(b)) && (!strncmp(a, b, strlen(a))))

zk_client *client;
static int quit;
const char * commands[] = {
    LS_CMD,
    CREATE_CMD,
    GET_CMD,
    SET_CMD,
    DEL_CMD,
    STAT_CMD,
    QUIT_CMD
};


//------------------------------ Completion -------------------------------------//
static void command_completion(const char *buf, int len, linenoiseCompletions *lc) {
    int i, nCommands;

    nCommands = sizeof(commands)/sizeof(commands[0]);
    for (i = 0; i < nCommands; i++) {
        if (strncmp(buf, commands[i], len) == 0) {
            linenoiseAddCompletion(lc, commands[i]); 
        }
    }
}

static void path_completion(zk_client *c, const char *buf, int len, linenoiseCompletions *lc) {
    int i, j, lastSpacePos, lastSlashPos, status, pathLen, dataLen;
    char *path = NULL;
    char *completion_buf;
    struct String_vector childrens;
    
    i = j = len - 1;
    // index of ' ' or '\t' from the end
    while (i >= 0 && buf[i] != ' ' &&  buf[i] != '\t') --i;
    if (i == len - 1) return;
    // index of '/' from the end
    while (j > 0 && buf[j] != '/')  j--;
    if (j - i <= 0) return;
    lastSpacePos = i;
    lastSlashPos = j;

    pathLen = lastSlashPos - lastSpacePos;
    path = malloc(pathLen + 1);
    if (!path) {
        fprintf(stderr, "Malloc memory failed.\n");
        exit(1);
    }
    memcpy(path, buf + lastSpacePos + 1, pathLen);
    path[pathLen] = '\0';
    // remove last '/' in path if exist, path like '/admin/' isn't allowed, we should trim it as /admin.
    if (pathLen > 1 && path[pathLen-1] == '/')  path[pathLen -1] = '\0';
    status = zk_get_children(c, path, &childrens);
    if ( status != ZOK) goto cleanup;

    for (i = 0; i < childrens.count; ++i) {
        if (strncmp(buf + lastSlashPos + 1, childrens.data[i], len - lastSlashPos -1) == 0) {
            linenoiseAddCompletion(lc, childrens.data[i]); 
        }
    }
    // tricks: if there's only one element, we should copy user input buffer to completion.
    // It's not a good idea to modify lc->cvec outside linenoise, but we do it here.
    if (lc->len == 1) {
        dataLen = strlen(lc->cvec[0]);
        completion_buf = malloc(dataLen + lastSlashPos + 2);
        memcpy(completion_buf, buf, lastSlashPos + 1);
        memcpy(completion_buf + lastSlashPos + 1, lc->cvec[0], dataLen);
        completion_buf[dataLen + lastSlashPos + 1] = '\0';
        lc->cvec[0] = strdup(completion_buf);
        free(completion_buf);
    }

 cleanup:
    free(path);
    deallocate_String_vector(&childrens);
}

void completion(const char *buf, linenoiseCompletions *lc) {
    int i = 0, len;
    char *pos;

    while (buf[i] == ' ' || buf[i] == '\t') ++i;
    len = strlen(buf);
    if (len - i > 0) {
        if ((pos = memchr(buf + i, ' ', len - i)) == NULL) {
            command_completion(buf + i, len - i, lc);
        } else {
            path_completion(client, buf + i, len - i, lc);
        }
    }
}

static void getCommand(zk_client *c, char *path) {
    struct Stat stat;
    int status;
    char *jsonStr;
    const char *msg;
    cJSON *cjson;
    struct buffer data;

    if((status = zk_exists(c, path, &stat)) != 1) {
        goto ERR;
    }
    if(!stat.dataLength) {
        msg = "{}";
        goto ERR;
    }
    
    if ((status = zk_get(c, path, &data)) != ZOK) {
        goto ERR;
    }
    cjson = cJSON_Parse(data.buff);
    jsonStr = cJSON_Print(cjson);
    if (jsonStr) {
        printf("%s\n", jsonStr);
        free(jsonStr);
    } else {
        printf("%s\n", data.buff);
    }
    deallocate_Buffer(&data);
    cJSON_Delete(cjson);
    return;

ERR:
    printf("get %s failed, %s.\n", path, zk_error(c));
}

static void createCommand(zk_client *c, char *path, char *buf, int len) {
    if(zk_create(c, path, buf, len, 0) == ZK_OK) {
        printf("create %s success.\n", path);
    } else {
        printf("create %s failed, %s.\n", path, zk_error(c));
    }
}

static void lsCommand(zk_client *c, char *path) {
    int i, status;
    struct String_vector childs;
    
    if ((status = zk_get_children(c, path, &childs)) != ZK_OK) {
        printf("ls %s failed, %s.\n", path, zk_error(c));
        return;
    }

    for (i = 0; i < childs.count; i++) {
        printf("%s\t", childs.data[i]);
    }
    if(childs.count > 0) printf("\n");
    deallocate_String_vector(&childs);
}

static inline void cJSON_AddLonglongToObject(cJSON *cjson, const char *name, long long v) {
    char *buf;

    // value > int32, should convert to string.
    if (v <= 2147483647) {
        cJSON_AddNumberToObject(cjson, name, v);
    } else {
        buf = ll2string(v); 
        cJSON_AddStringToObject(cjson, name, buf);
        free(buf);
    }
}

static void statCommand(zk_client *c, char *path) {
    struct Stat stat;
    char *jsonStr;
    cJSON  *cjson;

    if(zk_exists(c, path, &stat) != 1) {
        return;
    }
    cjson = cJSON_CreateObject();
    cJSON_AddNumberToObject(cjson, "version", stat.version);
    cJSON_AddNumberToObject(cjson, "cversion", stat.cversion);
    cJSON_AddNumberToObject(cjson, "aversion", stat.aversion);
    cJSON_AddNumberToObject(cjson, "dataLength", stat.dataLength);
    cJSON_AddNumberToObject(cjson, "numChildren", stat.numChildren);
    cJSON_AddLonglongToObject(cjson, "czxid", stat.czxid); 
    cJSON_AddLonglongToObject(cjson, "mzxid", stat.mzxid); 
    cJSON_AddLonglongToObject(cjson, "pzcxid", stat.pzxid); 
    cJSON_AddLonglongToObject(cjson, "ctime", stat.ctime); 
    cJSON_AddLonglongToObject(cjson, "mtime", stat.mtime); 
    cJSON_AddLonglongToObject(cjson, "ephemeralOwner", stat.ephemeralOwner); 

    jsonStr = cJSON_Print(cjson);
    printf("%s\n", jsonStr);

    cJSON_Delete(cjson);
    free(jsonStr);
}

static void setCommand(zk_client *c, char *path,
        char *buf, int len, int version) {
    int status;
    struct buffer data;
    
    data.buff = buf;
    data.len = len;
    if((status = zk_set(c, path, &data)) != ZK_OK) {
        printf("set %s failed, %s.\n", path, zk_error(c));
    } else {
        printf("set %s success.\n", path);
    }
}

static void delCommand(zk_client *c, char *path, int version) {
    int status;

    if((status = zk_del(c, path)) != ZK_OK) {
        printf("del %s failed, %s.\n", path, zk_error(c));
    } else {
        printf("success.\n");
    }
}

static void quitCommand() {
    quit = 1;
}

static void processCommand(zk_client *c, char **args, int narg) {
    int version = -1;
    char *cmd, *path;

    cmd = args[0];
    if (narg >= 2) path = args[1];
    if (STRING_EQUAL(cmd, GET_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        getCommand(c, path);
    } else if (STRING_EQUAL(cmd, CREATE_CMD)) {
        int len = 0;
        char *buf = NULL;
        if (narg < 2) goto ARGN_ERR;
        if (narg >= 3) {
            buf = args[2];
            len = strlen(buf);
        }
        createCommand(c, path, buf, len);
    } else if (STRING_EQUAL(cmd, LS_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        lsCommand(c, path);
    } else if (STRING_EQUAL(cmd, STAT_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        statCommand(c, path);
    } else if (STRING_EQUAL(cmd, SET_CMD)) {
        // don't support empty node value.
        if (narg < 3) goto ARGN_ERR;
        if (narg >= 4) version = atoi(args[3]);
        setCommand(c, path, args[2], strlen(args[2]), version);
    } else if (STRING_EQUAL(cmd, DEL_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        if (narg >= 3) version = atoi(args[2]);
        delCommand(c, path, version);
    } else if (STRING_EQUAL(cmd, QUIT_CMD) || STRING_EQUAL(cmd, EXIT_CMD)) {
        quitCommand();
    } else {
        printf("%s\n", "Unkonwn command.");
    }
    return;

ARGN_ERR:
    printf("Error num of arguments.\n");
}

void usage(const char *prog) {
    printf("Usage: %s host1:port1,host2:port2...\n", prog);
    exit(0);
}

int main(int argc, char **argv) {
    char *line, **args;
    int narg;

    if (argc < 2) usage(argv[0]);

    quit = 0;
    client= new_client("127.0.0.1", 2181, 60);
    set_connect_timeout(client, 2000);
    set_socket_timeout(client, 2000);

    linenoiseHistoryLoad(HISTORY_FILE_PATH);
    linenoiseSetCompletionCallback(completion);
    while(!quit && (line = linenoise(PROMPT)) != NULL) {
        linenoiseHistoryAdd(line);
        linenoiseHistorySave(HISTORY_FILE_PATH);
        args = sdssplitlen(line, strlen(line), " ", 1, &narg);
        if (narg >= 1) {
            processCommand(client, args, narg);
        }
        free(line);
        sdsfreesplitres(args, narg);
    }

    destroy_client(client);
    return 0;
}
