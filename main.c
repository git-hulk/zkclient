#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
#define MKDIR_CMD  "mkdir" 

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
    MKDIR_CMD,
    QUIT_CMD
};

static int check_reconnect(zk_client *c) {
    switch (c->last_err) {
        case ZSYSTEMERROR:
        case ZOPERATIONTIMEOUT:
        case ZINVALIDSTATE:
        case ZSESSIONEXPIRED:
        case ZINVALIDACL:
        case ZCLOSING:
        case ZAUTHFAILED:
        case ZSESSIONMOVED:
            return 1;
    }
    return 0;
}

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
    struct String_vector childrens= {0, NULL};
    
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
        logger(ERROR, "Malloc memory failed.");
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

static int getCommand(zk_client *c, char *path) {
    struct Stat stat;
    int status;
    char *jsonStr;
    cJSON *cjson;
    struct buffer data = {0, NULL};

    if((status = zk_exists(c, path, &stat)) != 1) {
        goto ERR;
    }
    if(!stat.dataLength) {
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
        data.buff[data.len - 1] = '\0';
        printf("%s\n", data.buff);
    }
    deallocate_Buffer(&data);
    cJSON_Delete(cjson);
    return ZK_OK;

ERR:
    printf("get %s failed, %s.\n", path, zk_error(c));
    deallocate_Buffer(&data);
    return c->last_err;
}

static int createCommand(zk_client *c, char *path, char *buf, int len) {
    if(zk_create(c, path, buf, len, 0) == ZK_OK) {
        printf("create %s success.\n", path);
        return ZK_OK;
    } else {
        printf("create %s failed, %s.\n", path, zk_error(c));
        return c->last_err;
    }
}


static int mkdirCommand(zk_client *c, char *path) {
    if (zk_mkdir(c, path) == ZK_OK) {
        printf("mkdir %s success.\n", path);
        return ZK_OK;
    } else {
        printf("mkdir %s failed, %s.\n", path, zk_error(c));
        return c->last_err;
    }
}

static int lsCommand(zk_client *c, char *path) {
    int i, status;
    struct String_vector childs;
    
    if ((status = zk_get_children(c, path, &childs)) != ZK_OK) {
        printf("ls %s failed, %s.\n", path, zk_error(c));
        return c->last_err;
    }

    for (i = 0; i < childs.count; i++) {
        printf("%s\t", childs.data[i]);
    }
    if(childs.count > 0) printf("\n");
    deallocate_String_vector(&childs);
    return ZK_OK;
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

static int statCommand(zk_client *c, char *path) {
    struct Stat stat;
    char *jsonStr;
    cJSON  *cjson;

    if(zk_exists(c, path, &stat) != 1) {
        return c->last_err;
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
    return ZK_OK;
}

static int setCommand(zk_client *c, char *path,
        char *buf, int len, int version) {
    int status;
    struct buffer data;
    
    data.buff = buf;
    data.len = len;
    if((status = zk_set(c, path, &data)) != ZK_OK) {
        printf("set %s failed, %s.\n", path, zk_error(c));
        return c->last_err;
    } else {
        printf("set %s success.\n", path);
        return ZK_OK;
    }
}

static int delCommand(zk_client *c, char *path, int version) {
    int status;

    if((status = zk_del(c, path)) != ZK_OK) {
        printf("del %s failed, %s.\n", path, zk_error(c));
        return c->last_err;
    } else {
        printf("del %s success.\n", path);
        return ZK_OK;
    }
}

static void quitCommand() {
    quit = 1;
}

static void processCommand(zk_client *c, char **args, int narg) {
    int status = ZK_OK, version = -1, path_len;
    char *cmd, *path = NULL;

    cmd = args[0];
    if (narg >= 2) path = args[1];
    if (path) {
        // strip '/'
        path_len = strlen(path);
        while(path_len > 1 && path[path_len - 1] == '/') {
            path[path_len-1] = '\0';
            --path_len;
        }
    }

    TIME_START();
    logger(DEBUG, "Begin to process %s command.", cmd);
    if (STRING_EQUAL(cmd, GET_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        status = getCommand(c, path);
    } else if (STRING_EQUAL(cmd, CREATE_CMD)) {
        int len = 0;
        char *buf = NULL;
        if (narg < 2) goto ARGN_ERR;
        if (narg >= 3) {
            buf = args[2];
            len = strlen(buf);
        }
        status = createCommand(c, path, buf, len);
    } else if (STRING_EQUAL(cmd, MKDIR_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        status = mkdirCommand(c, path);
    } else if (STRING_EQUAL(cmd, LS_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        status = lsCommand(c, path);
    } else if (STRING_EQUAL(cmd, STAT_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        status = statCommand(c, path);
    } else if (STRING_EQUAL(cmd, SET_CMD)) {
        // don't support empty node value.
        if (narg < 3) goto ARGN_ERR;
        if (narg >= 4) version = atoi(args[3]);
        status = setCommand(c, path, args[2], strlen(args[2]), version);
    } else if (STRING_EQUAL(cmd, DEL_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        if (narg >= 3) version = atoi(args[2]);
        status = delCommand(c, path, version);
    } else if (STRING_EQUAL(cmd, QUIT_CMD) || STRING_EQUAL(cmd, EXIT_CMD)) {
        quitCommand();
    } else {
        printf("%s\n", "Unkonwn command.");
    }
    // reconnect
    if (status == ZK_SOCKET_ERR || check_reconnect(c)) {
        reset_zkclient(c);
        if (do_connect(c) != ZK_OK) {
            logger(ERROR, "Reconnect to zookeeper error, exited...");
            exit(1);
        } else {
            logger(WARN, "Reconnect to zookeeper success.");
        }
    }
    TIME_END();
    logger(DEBUG, "Process %s command cost %d ms", cmd, TIME_COST());
    return;

ARGN_ERR:
    printf("Error num of arguments.\n");
    TIME_END();
    logger(DEBUG, "Process %s command cost %d ms", cmd, TIME_COST());
}

static void usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s -z zookeeper\n", prog_name);
    fprintf(stderr, "\t-z default 127.0.0.1:2181, delimiter is comma\n");
    fprintf(stderr, "\t-d debug mode.\n");
    fprintf(stderr, "\t-h help\n");
    fprintf(stderr, "\n\tsupport commands:\n");
    fprintf(stderr, "\t\tget path\n");
    fprintf(stderr, "\t\tls path\n");
    fprintf(stderr, "\t\tcreate path [data]\n");
    fprintf(stderr, "\t\tmkdir path\n");
    fprintf(stderr, "\t\tset path data\n");
    fprintf(stderr, "\t\tdel path\n");
    fprintf(stderr, "\t\tstat path\n");
    exit(0);
}

int main(int argc, char **argv) {
    char *line, **args, *zk_list = NULL;
    int ch, narg, show_usage = 0;

    while((ch = getopt(argc, argv, "z:dh")) != -1) {
        switch(ch) {
            case 'z': zk_list = optarg; break;
            case 'd': set_log_level(DEBUG); break;
            case 'h': show_usage = 1; break;
        }
    }
    if (show_usage) usage(argv[0]);
    if (!zk_list) {
        zk_list = "127.0.0.1:2181";
    }

    quit = 0;
    srand(time(0));
    client = new_client(zk_list, 6, 2);

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
