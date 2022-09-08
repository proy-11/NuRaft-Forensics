// #include <string>
// #include <map>
// #include <vector>
#include "argparse.hxx"

ArgumentParser::ArgumentParser() {
    help = false;
    nargs = 1;
    aliases = std::map<char, std::string>();
    args = std::map<std::string, std::string>();

    std::vector<std::string> argname_list({
        "workload-config",
    });
    std::vector<char> alias_list = {
        'w',
    };
    std::vector<std::string> helper_list = {
        "Path to workload config file (json format)",
    };

    int i = 0;
    for (auto& alias: alias_list) {
        aliases[alias] = argname_list[i];
        helpers[alias] = helper_list[i];
        i++;
    }

    for (auto& argname: argname_list) {
        args[argname] = std::string("");
    }
}

ArgumentParser::~ArgumentParser() {}

bool ArgumentParser::help_only() { return help; }

void ArgumentParser::parse_args(int argc, const char** argv) {
    auto args_set = std::map<std::string, bool>{};

    std::string flag("");
    bool waiting = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            help = true;
            return;
        }

        std::string argument(argv[i]);
        if (argument.substr(0, 2) == "--") {
            if (waiting) {
                std::fprintf(stderr, "Flag \"%s\" not yet set\n", flag.c_str());
                exit(_ERR_FLAG_NOT_SET);
            }
            waiting = true;
            flag = argument.substr(2, argument.length() - 2);
            if (args_set.count(flag)) {
                std::fprintf(stderr, "Flag \"%s\" duplicated\n", flag.c_str());
                exit(_ERR_INVALID_FLAG);
            } else if (args.count(argument)) {
                flag = argument;
                args_set[flag] = true;
            } else {
                std::fprintf(stderr, "Cannot identify flag \"%s\"\n", argument.c_str());
                exit(_ERR_INVALID_FLAG);
            }
        } else if (argument.substr(0, 1) == "-") {
            if (waiting) {
                std::fprintf(stderr, "Flag \"%s\" not yet set\n", flag.c_str());
                exit(_ERR_FLAG_NOT_SET);
            }
            waiting = true;
            char alias = argument.at(1);
            if (aliases.count(alias)) {
                flag = aliases[alias];
                if (args_set.count(flag)) {
                    std::fprintf(stderr, "Flag \"%s\" duplicated\n", flag.c_str());
                    exit(_ERR_INVALID_FLAG);
                } else {
                    args_set[flag] = true;
                }
            } else {
                std::fprintf(stderr, "Cannot identify flag \"%s\"\n", argument.c_str());
                exit(_ERR_INVALID_FLAG);
            }
        } else {
            if (!waiting) {
                std::fprintf(
                    stderr, "Argument \"%s\" has no correspondence\n", argument.c_str());
                exit(_ERR_FLAG_DOUBLE_SET);
            }
            waiting = false;
            args[flag] = argument;
        }
    }

    if (waiting) {
        std::fprintf(stderr, "Flag \"%s\" not yet set\n", flag.c_str());
        exit(_ERR_FLAG_NOT_SET);
    }
    for (auto& argpair: args) {
        if (args_set.count(argpair.first) == 0) {
            std::fprintf(stderr, "Argument \"%s\" missing\n", argpair.first.c_str());
            exit(_ERR_FLAG_MISSING);
        }
    }
}

void ArgumentParser::print_help(const char* prog) {
    const char* format = "%8s  %24s  %48s\n";
    std::printf("Usage: %s [--flag/-f argument]\n\n", prog);
    std::printf(format, "Alias", "Full_flag", "Descrption");
    std::printf(format, "-h", "--help", "Print helpers");
    for (auto& alias: helpers) {
        auto alias_str = std::string("-") + std::string(1, alias.first);
        std::printf(format,
                    alias_str.c_str(),
                    (std::string("--") + aliases[alias.first]).c_str(),
                    alias.second.c_str());
    }
    std::printf("\n");
}

std::string ArgumentParser::get_arg(std::string argname) { return args[argname]; }

int main(int argc, const char** argv) {
    ArgumentParser parser = ArgumentParser();
    parser.parse_args(argc, argv);
    if (parser.help_only()) {
        parser.print_help(argv[0]);
        return 0;
    }

    std::printf("%s\n", parser.get_arg("workload-config").c_str());
    return 0;
};