#include "util.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static char *log_file = NULL;
static enum LEVEL log_level = INFO;

int32_t atomic_inc(volatile int32_t* operand, int incr) {
    int32_t result;
    asm __volatile__(
            "lock xaddl %0,%1\n"
            : "=r"(result), "=m"(*(int *)operand)
            : "0"(incr)
            : "memory");
    return result;
}

__attribute__((constructor)) int32_t get_xid() {
    static int32_t xid = -1; 
    if (xid == -1) {
        xid = time(0);
    }   
    return atomic_inc(&xid,1);
}

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

void set_loglevel_by_string(const char *level) {
    int len;
    
    len = strlen(level);
    if (strlen("debug") == len && !memcmp("debug", level, len)) {
        log_level = DEBUG;
    } else if (strlen("info") == len && !memcmp("info", level, len)) {
        log_level = INFO;
    } else if (strlen("warn") == len && !memcmp("warn", level, len)) {
        log_level = WARN;
    } else if (strlen("error") == len && !memcmp("error", level, len)) {
        log_level = ERROR;
    }
}

void set_log_level(enum LEVEL level) {
    log_level  = level;
} 

void set_log_file(char *filename) {
    log_file = filename;
}

void logger(enum LEVEL loglevel,char *fmt, ...) {
    FILE *fp;
    va_list ap;
    time_t now;
    char buf[4096];
    char t_buf[64];
    char *msg = NULL;
    const char *color = "";

    if(loglevel < log_level) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    switch(loglevel) {
        case DEBUG: msg = "DEBUG"; color = C_YELLOW; break;
        case INFO:  msg = "INFO";  color = C_GREEN; break;
        case WARN:  msg = "WARN";  color = C_PURPLE; break;
        case ERROR: msg = "ERROR"; color = C_RED; break;
    }

    now = time(NULL);
    strftime(t_buf,64,"%Y-%m-%d %H:%M:%S",localtime(&now));
    fp = (log_file == NULL) ? stdout : fopen(log_file,"a");
    if(log_file) {
        fprintf(fp, "[%s] [%s] %s\n", t_buf, msg, buf);
        fclose(fp);
    } else {
        fprintf(fp, "%s[%s] [%s] %s"C_NONE"\n", color, t_buf, msg, buf);
    }

    if(loglevel >= ERROR) {
        exit(1);
    }
}
