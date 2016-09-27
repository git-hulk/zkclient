# zkclient

A simple client for zookeeper with auto-complete

## 1) install

```shell
$ git clone https://github.com/git-hulk/zkclient.git 
$ cd zkclient
$ make
$ sudo make install
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
get path
ls path
create path [data]
mkdir path
set path data
del path
stat path
```

## 3) TODO

* watch
