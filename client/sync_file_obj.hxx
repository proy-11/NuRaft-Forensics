#pragma once

#include <mutex>
#include <string>

class sync_file_obj {
public:
    sync_file_obj(std::string path) {
        fp = std::fopen(path.c_str(), "w");
        if (errno != 0) {
            std::fprintf(stderr, "Error: Cannot open file %s\n", path.c_str());
            exit(1);
        }
    };
    ~sync_file_obj() { std::fclose(fp); };

    void writeline(std::string line) {
        if (line.empty()) {
            return;
        }
        if (line.back() != '\n') {
            line += "\n";
        }
        mutex.lock();
        std::fprintf(fp, "%s", line.c_str());
        mutex.unlock();
    }

private:
    FILE* fp;
    std::mutex mutex;
};
