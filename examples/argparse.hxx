#include <map>
#include <string>
#include <vector>

#define _ERR_INVALID_FLAG 1
#define _ERR_FLAG_NOT_SET 2
#define _ERR_FLAG_DOUBLE_SET 3
#define _ERR_FLAG_MISSING 4

class ArgumentParser {
public:
    ArgumentParser();
    ~ArgumentParser();

    void parse_args(int argc, const char** argv);
    bool help_only();
    void print_help(const char* prog);
    std::string get_arg(std::string argname);

private:
    bool help;
    int nargs;
    // std::vector<std::string> argnames;
    std::map<char, std::string> aliases;
    std::map<char, std::string> helpers;
    std::map<std::string, std::string> args;
};