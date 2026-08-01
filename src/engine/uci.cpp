#include "engine/uci.h"
#include "engine/bitboards.h"
#include "engine/eval.h"
#include "engine/search.h"

#include <iostream>
#include <sstream>
#include <cctype>

namespace engine {

constexpr auto StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

UCIEngine::UCIEngine() {
    zobrist::initZobrist();
    bb::init();
    init_eval_weights_default();

    engine.setBestMoveCallback(
        [this](Move best, int score, int depth, Move* pv) {
            sendBestMove(best);
        });

    engine.setInfoCallback(
        [this](const SearchInfo& info) {
            printInfo(info);
        });

    engine.setThreads(16);
    engine.setTranspositionTable(64);
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
        cmdSetOption(iss);
    } else {
        return;
    }
}

// --- UCI core commands ---

void UCIEngine::cmdUci() {
    // Engine id
    std::cout << "id name MyEngine 0.1\n";
    std::cout << "id author Adam\n";

    std::cout << "option name Hash type spin default 64 min 1 max 1024\n";
    std::cout << "option name Threads type spin default 16 min 1 max 256\n";

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
            limits.hasTimeControl = true;
        } else if (token == "btime") {
            iss >> limits.btime_ms;
            limits.hasTimeControl = true;
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

void UCIEngine::cmdSetOption(std::istringstream& iss) {
    std::string token;

    std::string name;
    std::string value;

    // Expected forms:
    // setoption name Hash value 256
    // setoption name Hash
    // setoption name Some Option value blah
    while (iss >> token) {
        if (token == "name") {
            name.clear();
            while (iss >> token && token != "value") {
                if (!name.empty()) name += ' ';
                name += token;
            }
            if (token != "value") {
                // no "value" provided; ignore
                break;
            }

            // Read rest of stream as value (can have spaces, but for Hash it won't)
            std::getline(iss, value);
            // trim leading spaces
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            break;
        }
    }

    if (name.empty()) return;

    if (name == "Hash") {
        // value should be an integer in MB
        if (value.empty()) return;
        try {
            long long mb = std::stoll(value);
            if (mb < 1) mb = 1;
            if (mb > 1024) mb = 1024;

            // Call whichever API you actually have:
            engine.setTranspositionTable(static_cast<size_t>(mb));
        } catch (...) {
            // ignore invalid values
        }
    }

    else if (name == "Threads") {
        if (value.empty()) return;
        try {
            long long n = std::stoll(value);
            if (n < 1) n = 1;
            if (n > 256) n = 256;

            engine.setThreads(static_cast<size_t>(n));
        } catch (...) {
            // ignore invalid values
        }
    }
}

void UCIEngine::printInfo(const SearchInfo& info) {
    std::cout << "info";

    if (info.depth > 0)
        std::cout << " depth " << info.depth;
    if (info.seldepth > 0)
        std::cout << " seldepth " << info.seldepth;

    if (info.isMate) {
        std::cout << " score mate " << info.score;
    } else {
        std::cout << " score cp " << info.score;
    }

    if (info.timeMs > 0)
        std::cout << " time " << info.timeMs;
    if (info.nodes > 0)
        std::cout << " nodes " << info.nodes;
    if (info.nps > 0)
        std::cout << " nps " << info.nps;

    if (info.pv[0] != NOMOVE) {
        std::cout << " pv";
        for (int i = 0; i < MAXPLY; i++) {
            if (info.pv[i] == NOMOVE) {
                break;
            }
            std::cout << " " << engine.moveToUciString(info.pv[i]);
        }
    }

    std::cout << "\n";
    std::cout.flush();
}

void UCIEngine::sendBestMove(Move best) {
    std::cout << "bestmove " << engine.moveToUciString(best) << "\n";
    std::cout.flush();
}

}