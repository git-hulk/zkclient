# zkclient

A simple client for zookeeper with auto-complete

## 1) install

```shell
$ git clone https://github.com/git-hulk/zkclient.git 
$ cd zkclient
$ make
```

## 2) how to use

```
Usage: ./zkclient -z zookeeper
    -z default 127.0.0.1:2181, delimiter is comma.
    -d debug mode.
    -h help.
```

#### support commands 

```
ls path
create path [data]
set path data
del path
stat path
```

## 3) TODO

1. create path recursive 
2. create ephemeral node
3. delete recursive 
