#ifndef __UTIL_H_
#define __UTIL_H_

#include <stdint.h>
__attribute__((constructor)) int32_t get_xid();
char *ll2string(long long v);
char **sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void sdsfreesplitres(char **tokens, int count); 
#endif
