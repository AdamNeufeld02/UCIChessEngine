// apps/tools_main.cpp
#include <iostream>
#include <string_view>

namespace perft_cli {
    int run(int argc, char** argv);   // engine_tools perft ...
}

namespace bench_cli {
    int run(int argc, char** argv);   // engine_tools bench ...
}

namespace verify_cli {
    int run(int argc, char** argv);   // engine_tools verify ...
}

namespace tests_cli {
    int run(int argc, char** argv);   // engine_tools tests ...
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  engine_tools perft  [args...]\n"
                  << "  engine_tools bench  [args...]\n"
                  << "  engine_tools verify [args...]\n"
                  << "  engine_tools tests  [args...]\n";
        return 1;
    }

    std::string_view cmd{argv[1]};

    if (cmd == "perft") {
        std::cout << "Not Implmented Yet" << "\n";
        return 0;
    } else if (cmd == "bench") {
        std::cout << "Not Implmented Yet" << "\n";
        return 0;
    } else if (cmd == "verify") {
        std::cout << "Not Implmented Yet" << "\n";
        return 0;
    } else if (cmd == "tests") {
        return tests_cli::run(argc - 1, argv + 1);
    }

    std::cerr << "Unknown command: " << cmd << "\n";

    std::cerr << "Usage:\n"
                  << "  engine_tools perft  [args...]\n"
                  << "  engine_tools bench  [args...]\n"
                  << "  engine_tools verify [args...]\n"
                  << "  engine_tools tests  [args...]\n";
    return 1;
}