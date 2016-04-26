#include "zookeeper.h"
#include "proto.h"
#include "cJSON/cJSON.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static zhandle_t *zh;
static clientid_t myid;
const char *brokers_ids_path = "/brokers/ids";
const char *consumers_group_path = "/consumers";

static void usage()
{
    fprintf(stderr, "Option                                  Description \n");
    fprintf(stderr, "_________                               ____________\n");
    fprintf(stderr, "-h                                      show usage. \n");
    fprintf(stderr, "-z <hostname:port,..,                   REQUIRED:zookeeper list\n");
    fprintf(stderr, "   hostname:port>                                   \n"); 
    fprintf(stderr, "-c <show_broker_list | show_group_list>.                  REQUIRED:the command to be executed.\n");
    exit(1);
}

void broker_list() {
    struct String_vector id_list;
    int status, i, len=4096;
    struct Stat stat;
    char path[128],buff[4096];
    cJSON *data;
    status = zoo_get_children(zh, brokers_ids_path, 0, &id_list);
    if ( status != ZOK) {
        printf("can't get children from %s, error num is %d\n", brokers_ids_path, status);
        exit(1);
    }
    for (i=0; i<id_list.count; i++) {
        strcpy(path, brokers_ids_path);
        strcat(path, "/");
        strcat(path, id_list.data[i]);
        status = zoo_get(zh, path, 0, buff, &len, &stat);
        if ( status != ZOK) {
            printf("can't get information from %s, error num is %d\n", consumers_group_path, status);
            exit(1);
        }
        printf("broker:%s  ", id_list.data[i]);
        data = cJSON_Parse(buff);
        char *host = cJSON_GetObjectItem(data, "host")->valuestring;
        printf("host:%s  ", host);
        int port = cJSON_GetObjectItem(data, "port")->valueint;
        printf("port:%d\n", port);
        cJSON_Delete(data);
    }
    deallocate_String_vector(&id_list);
}

void group_list() {
    struct String_vector group_list;
    int status, i;
    status = zoo_get_children(zh, consumers_group_path, 0, &group_list);
    if ( status !=ZOK) {
        printf("can't get children from %s, error num is %d\n", consumers_group_path, status);
        exit(1);
    }
    for (i=0; i<group_list.count; i++) {
        printf("%s\n", group_list.data[i]);
    }
    deallocate_String_vector(&group_list);
}

int main(int argc, char **argv) {
    char ch;
    char zk_host_list[1024], command[1024];
    int has_command = 0, has_zk_host_list = 0;

    while((ch = getopt(argc, argv, "z:c:")) != -1) {
        switch(ch) {
            case 'z': strcpy(zk_host_list, optarg); has_zk_host_list = 1; break;
            case 'c': strcpy(command, optarg); has_command = 1; break;
            default: usage();
        }
    }
    if (! has_zk_host_list) {
        printf("Missing required argument [zookeeper]\n");
        usage();
    }
    if (! (has_command) ) {
        printf("Missing required argument [command]\n");
        usage();
    }		
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zh = zookeeper_init(zk_host_list, NULL, 30000, &myid, 0, 0);     
    if (!zh) {
        printf("can't create new zk_client\n");
    }

    if (has_command) {
        if (strcmp(command, "show_broker_list") == 0) {
            broker_list(); 
        } else if (strcmp(command, "show_group_list") == 0) {
            group_list();
        } else {
            zookeeper_close(zh);
            usage();
        }
    }
    zookeeper_close(zh);
    return 0;
}
