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
* [config.json](./config.json)
    - This configuration file contains list of servers and client details
    -  Server details include - 
        - `id` - server id (int)
        - `byzantine` - byzantine behavior (string), 
        - `ip` - server ip (string in dotted decimal notation), 
        - `port` - server port (int)
        - `cport` - client port (int) 
    - Client details include path to workload configuration file, `workload.json`
    - It also specifies the directory where results are stored in `working_dir` 
* [workload.json](./workload.json)
    - The workload configuration file specifies number of requests (`size`), frequency of requests (`freq`), and type of requests (`type`)

#### Run
* Linux & OSX
```sh
$ cd build
$ cp ../client/config.json client/
$ cp ../client/workload.json client/
$ client/d_raft_launcher client/config.json
```