## kafka_inspect
------
### 1）kafka_inspect 是干啥的
从ZK中获取kafka的相关信息，包括broker list，group list，consumer list等。

目前只实现获取broker list的功能，其他功能会继续开发。

### 2）如何使用
```shell
$ cd $zk_cclient_Dir
$ make 
$ ./kafka_inspect -z localhost:2181 -c show_broker_list
```
#### 2.1）选项说明
```
-z <hostname:port,..,hostname:port>  必选项，需要连接到的ZK列表
-c <broker_list | ... | ... >        必选项，需要执行的命令，当前版本只支持show_broker_list
-h                                   显示帮助
            
```

