#include "engine/uci.h"
#include "engine/bitboards.h"
#include "engine/eval.h"

#include <iostream>
#include <sstream>
#include <cctype>

namespace engine {

constexpr auto StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

UCIEngine::UCIEngine() {
    bb::init();
    init_eval_weights_default();
}

void UCIEngine::loop() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        handleLine(line);
    }
}

void UCIEngine::handleLine(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    iss >> token;
    if (token == "uci") {
        cmdUci();
    } else if (token == "isready") {
        cmdIsReady();
    } else if (token == "ucinewgame") {
        cmdUciNewGame();
    } else if (token == "position") {
        cmdPosition(iss);
    } else if (token == "go") {
        cmdGo(iss);
    } else if (token == "stop") {
        cmdStop();
    } else if (token == "quit") {
        cmdQuit();
    } else if (token == "setoption") {
        // cmdSetOption(iss);
    } else {
        return;
    }
}

// --- UCI core commands ---

void UCIEngine::cmdUci() {
    // Engine id
    std::cout << "id name MyEngine 0.1\n";
    std::cout << "id author Adam\n";

    // Options (add as needed later)
    // std::cout << "option name Hash type spin default 16 min 1 max 1024\n";

    std::cout << "uciok\n";
    std::cout.flush();
}

void UCIEngine::cmdIsReady() {
    std::cout << "readyok\n";
    std::cout.flush();
}

void UCIEngine::cmdUciNewGame() {
    engine.clearSearch();
}

// --- position command ---
// position [fen <fenstring> | startpos ]  moves <move1> <move2> ...
void UCIEngine::cmdPosition(std::istringstream& iss) {
    std::string token;
    iss >> token;
    std::string fen;

    if (token == "startpos") {
        fen = StartFEN;
        iss >> token;
    } else if (token == "fen") {
        while (iss >> token && token != "moves") {
            fen += token + " ";
        }
    } else {
        return;
    }

    std::vector<std::string> moves;

    while (iss >> token) {
        moves.push_back(token);
    }

    engine.setPosition(fen, moves);
}

void UCIEngine::cmdGo(std::istringstream& iss) {
    engine.go(parseGo(iss));
}

SearchLimits UCIEngine::parseGo(std::istringstream& iss) {
    SearchLimits limits;
    std::string token;

    while (iss >> token) {
        if (token == "wtime") {
            iss >> limits.wtime_ms;
        } else if (token == "btime") {
            iss >> limits.btime_ms;
        } else if (token == "winc") {
            iss >> limits.winc_ms;
        } else if (token == "binc") {
            iss >> limits.binc_ms;
        } else if (token == "movestogo") {
            iss >> limits.movestogo;
        } else if (token == "movetime") {
            iss >> limits.movetime_ms;
        } else if (token == "depth") {
            iss >> limits.depth;
        } else if (token == "infinite") {
            limits.infinite = true;
        } else if (token == "ponder") {
            // you can support ponder later
        }
    }

    return limits;
}

void UCIEngine::cmdStop() {
    engine.stopSearch();
}

void UCIEngine::cmdQuit() {
    std::exit(0);
}

}