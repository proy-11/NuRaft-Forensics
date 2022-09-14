#include <raft_client.hxx>
#include<iostream>

int main() {
    raft_client client;
    if(client.initialise_raft_server()) {
        client.append_log_entries();
    }
    client.shutdown();
    return 0;
}