Steps to build and run benchmarking
======

#### Build
* Linux & OSX
```sh
$ mkdir build
$ cd build
build$ cmake ../
build$ make
```

#### Config files
* [config.json](./config.json).
* [workload.json](./workload.json)

#### Run
* Linux & OSX
```sh
$ cd build
$ cp ../client/config.json client/
$ cp ../client/workload.json client/
$ client/d_raft_launcher client/config.json
```