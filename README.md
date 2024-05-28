
NuRaft-Forensics
======

NuRaft library with forensics support.

Forensics
---------
### Log Replication
Several fields are maintained and shared during log replication for forensics purposes:

- Hash Pointers: a hash pointer cache (`hash_cache_`) is maintained in each node to store the hash pointer of log entries between the last committed entry and the last appended entry. Any `append_entries` request whose last entry is within the cache will include the hash pointer in the first entry in the request. Followers will verify the hash pointer before appending the entries to their logs. On failure, the follower will set the result code to `cmd_result_code::BAD_CHAIN`.
- Leader Signature (`leader_sig_` field in `log_entry` class)
- Commitment Certificate (`commit_cert_`: CC of the latest committed log entry, `working_certs_`: CCs still waiting for followers to acknowledge)


### Leader Election
Leader certificates are first stored in memory and then written to disk when the number of certificates in memory reaches a certain threshold. The threshold is configured by `election_list_max_` in `include/libnuraft/raft_params.hxx`. The default value is `10`. On shutdown, a final write to disk is performed. The certificates lists are written to `[forensics_dir]/el_[timestamp]_p[node_id].dat`.

#### Election list Parser
An easy-to-use Python parser is provided in `scripts/forensics/election_list.py`.


How to Build
------------
#### 1. Install `cmake` and `openssl`: ####

* Ubuntu
```sh
$ sudo apt-get install cmake openssl libssl-dev libz-dev
```

* OSX
```sh
$ brew install cmake
$ brew install openssl
```
* Windows
    * Download and install [CMake](https://cmake.org/download/).
    * Currently, we do not support SSL for Windows.


#### 2. Install `boost` library: ####

See the [official documentation](https://www.boost.org/doc/libs/1_80_0/more/getting_started/index.html) for reference. 

#### 3. Fetch [Asio](https://github.com/chriskohlhoff/asio) library: ####

* Linux & OSX
```sh
$ ./prepare.sh
```
* Windows
    * Clone [Asio](https://github.com/chriskohlhoff/asio) `asio-1-12-0`
      into the project directory.
```sh
C:\NuRaft> git clone https://github.com/chriskohlhoff/asio -b asio-1-12-0
```

#### 3. Build static library, tests, and examples: ####

* Linux & OSX
```sh
$ mkdir build
$ cd build
build$ cmake ../
build$ make
```

Run unit tests
```sh
build$ ./runtests.sh
```

* Windows:
```sh
C:\NuRaft> mkdir build
C:\NuRaft> cd build
C:\NuRaft\build> cmake -G "NMake Makefiles" ..\
C:\NuRaft\build> nmake
```

You may need to run `vcvars` script first in your `build` directory. For example (it depends on how you installed MSVC):
```sh
C:\NuRaft\build> c:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat
```

Supported Platforms
-------------------
* Ubuntu (tested on 14.04, 16.04, and 18.04)
* Centos (tested on 7)
* OSX (tested on 10.13 and 10.14)

3rd Party Code
--------------
1. URL: https://github.com/datatechnology/cornerstone<br>
License: https://github.com/datatechnology/cornerstone/blob/master/LICENSE<br>
Originally licensed under the Apache 2.0 license.

