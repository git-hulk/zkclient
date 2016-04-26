#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "zookeeper.h"
#include "cJSON/cJSON.h"
#include "linenoise/linenoise.h"

#define LS_CMD   "ls"
#define GET_CMD  "get"
#define SET_CMD  "set"
#define QUIT_CMD "quit"
#define STAT_CMD "stat"
#define DEL_CMD  "del" 

#define PROMPT "zkclient> "
#define HISTORY_FILE_PATH "/tmp/zkclient_history.txt"
#define STRING_EQUAL(a, b)  \
((strlen(a) == strlen(b)) && (!strncmp(a, b, strlen(a))))

static int quit;
static zhandle_t *zh;
const char * commands[] = {
    LS_CMD,
    GET_CMD,
    SET_CMD,
    DEL_CMD,
    STAT_CMD,
    QUIT_CMD
};

//------------------------------ Utils -------------------------------------//
char *ll2string(long long v) {
    int i, len = 2;
    long long tmp;
    char *buf;
    
    tmp = v;
    while ((tmp /= 10) > 0) len++;
    
    buf = malloc(len);
    if (!buf) {
        fprintf(stderr, "Exited, as malloc failed at ll2string.\n");
        exit(1);
    }

    i = len - 2;
    while (v > 0) {
        buf[i--] = v % 10 + '0';
        v /= 10;
    }
    buf[len - 1] = '\0';

    return buf;
}

// This function was copied from redis/sds.c
char **sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count) {
    int elements = 0, slots = 5, start = 0, j;
    char **tokens, *tmp;

    if (seplen < 1 || len < 0) return NULL;

    tokens = malloc(sizeof(char *)*slots);
    if (tokens == NULL) return NULL;

    if (len == 0) { 
        *count = 0; 
        return tokens;
    }    
    for (j = 0; j < (len-(seplen-1)); j++) {
        /* make sure there is room for the next element and the final one */
        if (slots < elements+2) {
            char **newtokens;

            slots *= 2;
            newtokens = realloc(tokens,sizeof(char *)*slots);
            if (newtokens == NULL) goto cleanup;
            tokens = newtokens;
        }    
        /* search the separator */
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,seplen) == 0)) {
            if (j-start > 0) {
                tmp = malloc(j-start+1);
                memcpy(tmp, s+start, j-start);
                tmp[j-start] = '\0';
                tokens[elements] = tmp;
                if (tokens[elements] == NULL) goto cleanup;
                elements++;
            }
            start = j+seplen;
            j = j+seplen-1; /* skip the separator */
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    if (len - start > 0) {
        tmp = malloc(len-start+1);
        memcpy(tmp, s+start, len-start);
        tmp[len-start] = '\0';
        tokens[elements] = tmp;
        if (tokens[elements] == NULL) goto cleanup;
        elements++;
    }
    *count = elements;
    return tokens;

cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) free(tokens[i]);
        free(tokens);
        *count = 0;
        return NULL;
    }
}

/* Free the result returned by sdssplitlen(), or do nothing if 'tokens' is NULL. */
void sdsfreesplitres(char **tokens, int count) {
    if (!tokens) return;
    while(count--)
        free(tokens[count]);
    free(tokens);
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

static void path_completion(const char *buf, int len, linenoiseCompletions *lc) {
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
    status = zoo_get_children(zh, path, 0, &childrens);
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
            path_completion(buf + i, len - i, lc);
        }
    }
}

static void getCommand(zhandle_t *zh, const char *path) {
    struct Stat stat;
    int dataLen, status;
    char *buf, *jsonStr;
    const char *msg;
    cJSON *cjson;

    if((status = zoo_exists(zh, path, 0, &stat)) != ZOK) {
        msg = zerror(status);
        goto ERR;
    }
    if(!stat.dataLength) {
        msg = "{}";
        goto ERR;
    }
    
    dataLen = stat.dataLength;
    buf = malloc(dataLen+ 1);
    buf[dataLen] = '\0';
    if ((status = zoo_get(zh, path, 0, buf, &dataLen, &stat)) != ZOK) {
        msg = zerror(status);
        free(buf);
        goto ERR;
    }
    cjson = cJSON_Parse(buf);
    jsonStr = cJSON_Print(cjson);
    if (jsonStr) {
        printf("%s\n", jsonStr);
        free(jsonStr);
    } else {
        printf("%s\n", buf);
    }

    free(buf);
    cJSON_Delete(cjson);
    return;

ERR:
    printf("%s\n", msg);
}

static void lsCommand(zhandle_t *zh, const char *path) {
    int i, status;
    struct String_vector childs;
    
    if ((status = zoo_get_children(zh, path, 0, &childs)) != ZOK) {
        printf("%s\n", zerror(status));
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

static void statCommand(zhandle_t *zh, const char *path) {
    struct Stat stat;
    char *jsonStr;
    cJSON  *cjson;

    if(zoo_exists(zh, path, 0, &stat) != ZOK) return;
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

static void setCommand(zhandle_t *zh, const char *path,
        const char *buf, int len, int version) {
    int status;

    if((status = zoo_set(zh, path, buf, len, version)) != ZOK) {
        printf("set %s failed, as %s\n", path, zerror(status));
    } else {
        printf("success.\n");
    }
}

static void delCommand(zhandle_t *zh, const char *path, int version) {
    int status;

    if((status = zoo_delete(zh, path, version)) != ZOK) {
        printf("del %s failed, as %s\n", path, zerror(status));
    } else {
        printf("success.\n");
    }
}

static void quitCommand() {
    quit = 1;
}

static void processCommand(char **args, int narg) {
    int version = -1;
    char *cmd, *path;

    cmd = args[0];
    if (narg >= 2) path = args[1];
    if (STRING_EQUAL(cmd, GET_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        getCommand(zh, path);
    } else if (STRING_EQUAL(cmd, LS_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        lsCommand(zh, path);
    } else if (STRING_EQUAL(cmd, STAT_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        statCommand(zh, path);
    } else if (STRING_EQUAL(cmd, SET_CMD)) {
        // don't support empty node value.
        if (narg < 3) goto ARGN_ERR;
        if (narg >= 4) version = atoi(args[3]);
        setCommand(zh, path, args[2], strlen(args[2]), version);
    } else if (STRING_EQUAL(cmd, DEL_CMD)) {
        if (narg < 2) goto ARGN_ERR;
        if (narg >= 3) version = atoi(args[2]);
        delCommand(zh, path, version);
    } else if (STRING_EQUAL(cmd, QUIT_CMD)) {
        quitCommand();
    } else {
        printf("%s\n", "UNKONWN COMMAND.");
    }
    return;

ARGN_ERR:
    printf("ERROR NUM OF ARGUMENTS.\n");
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
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zh = zookeeper_init(argv[1], NULL, 3000, NULL, 0, 0);     
    if (!zh) {
        printf("can't create new zk_client\n");
        exit(1);
    }

    linenoiseHistoryLoad(HISTORY_FILE_PATH);
    linenoiseSetCompletionCallback(completion);
    while(!quit && (line = linenoise(PROMPT)) != NULL) {
        linenoiseHistoryAdd(line);
        linenoiseHistorySave(HISTORY_FILE_PATH);
        args = sdssplitlen(line, strlen(line), " ", 1, &narg);
        if (narg >= 1) {
            processCommand(args, narg);
        }
        free(line);
        sdsfreesplitres(args, narg);
    }
    zookeeper_close(zh);
    return 0;
}
