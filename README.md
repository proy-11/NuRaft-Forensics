
NuRaft-Forensics
======

NuRaft library with forensics support (TODO) and benchmarker. 

Issues
------
* some test cases fail

Features
--------
### New features added in this project ###
* [Benchmarker](client/README.md)


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

