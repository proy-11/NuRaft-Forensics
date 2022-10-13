#include <iostream>

#ifndef SOCK_MGR
#define SOCK_MGR

class socket_mgr {
public:
    virtual inline void notify() = 0;
};

#endif