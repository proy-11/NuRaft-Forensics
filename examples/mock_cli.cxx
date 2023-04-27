#include <boost/program_options.hpp>
#include <iostream>
#include <string>

namespace po = boost::program_options;

int main(int argc, char** argv) {
    std::string input;
    std::string output;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "print usage message")("input,i", po::value(&input), "Input file")(
        "output,o", po::value(&output), "Output file");

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    if (vm.count("input")) {
        std::cout << "Input file was set to " << vm["input"].as<std::string>() << ".\n";
        return 0;
    }

    if (vm.count("output")) {
        std::cout << "Output file was set to " << vm["output"].as<std::string>() << ".\n";
        return 0;
    }

    if (vm.count("help") || !vm.count("input") || !vm.count("output")) {
        std::cerr << desc << "\n";
        return 1;
    }

    return 0;
}
