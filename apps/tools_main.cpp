// apps/tools_main.cpp
#include <iostream>
#include <string_view>
#include <sstream>
#include <vector>
#include "engine/board.h"
#include "engine/bitboards.h"
#include "engine/hashing.h"
#include "engine/movegen.h"

namespace perft_cli {
    int run(int argc, char** argv);
}

namespace bench_cli {
    int run(int argc, char** argv);
}

namespace verify_cli {
    int run(int argc, char** argv);
}

namespace tests_cli {
    int run(int argc, char** argv);
}

namespace magic_gen_cli {
    int run(int argc, char** argv);
}

namespace tune_cli {
    int run(int argc, char** argv);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  engine_tools perft  [args...]\n"
                  << "  engine_tools bench  [args...]\n"
                  << "  engine_tools verify [args...]\n"
                  << "  engine_tools tests  [args...]\n"
                  << "  engine_tools gen_magics  [args...]\n";
        return 1;
    }

    std::string_view cmd{argv[1]};

    if (cmd == "perft") {
        return perft_cli::run(argc - 1, argv + 1);
    } else if (cmd == "bench") {
        std::cout << "Not Implmented Yet" << "\n";
        return 0;
    } else if (cmd == "verify") {
        std::cout << "Not Implmented Yet" << "\n";
        return 0;
    } else if (cmd == "tests") {
        return tests_cli::run(argc - 1, argv + 1);
    } else if (cmd == "gen_magics") {
        return magic_gen_cli::run(argc - 1, argv + 1);
    } else if (cmd == "tune") {
        return tune_cli::run(argc - 1, argv + 1);
    } else if (cmd == "replay") {
        using namespace engine;
        bb::init();
        zobrist::initZobrist();

        auto squareStr = [](Square sq) {
            std::string s;
            s += char('a' + (sq & 7));
            s += char('1' + (sq / 8));
            return s;
        };
        auto moveUci = [&](Move m) {
            std::string s = squareStr(fromSq(m)) + squareStr(toSq(m));
            if (moveFlag(m) == PROMOTION) s += "**nbrq*"[promoPiece(m)];
            return s;
        };

        Board board;
        State root{};
        board.fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &root);

        std::vector<State> states;
        states.reserve(argc);

        for (int i = 2; i < argc; i++) {
            std::string want(argv[i]);
            MoveList<GEN_LEGAL> moves(board);
            Move found = NOMOVE;
            for (const ScoredMove& sm : moves) {
                if (moveUci(sm.move) == want) { found = sm.move; break; }
            }
            if (found == NOMOVE) {
                std::cout << "Move " << want << " (index " << (i - 2) << ") not legal here. Stopping.\n";
                break;
            }
            states.emplace_back();
            board.makeMove(found, &states.back());

            std::string placement;
            for (int sq = 0; sq < SQUARECOUNT; sq++) {
                Piece pc = board.pieceOn(static_cast<Square>(sq));
                placement += pc == EMPTY ? '.' : "?PNBRQK??pnbrqk?"[pc];
            }

            std::cout << (i - 2) << ": " << want
                      << "  repCount=" << board.st->repetitionCount
                      << "  isRepetitionDraw=" << (board.isRepetitionDraw() ? "true" : "false")
                      << "  halfmoveClock=" << board.st->halfmoveClock
                      << "  stm=" << (board.sideToMove() == WHITE ? "W" : "B")
                      << "  castling=" << (int)board.st->castlingRights
                      << "  ep=" << (int)board.epSquare()
                      << "  key=" << board.key()
                      << "  placement=" << placement
                      << "\n";
        }
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";

    std::cerr << "Usage:\n"
                  << "  engine_tools perft  [args...]\n"
                  << "  engine_tools bench  [args...]\n"
                  << "  engine_tools verify [args...]\n"
                  << "  engine_tools tests  [args...]\n"
                  << "  engine_tools gen_magics  [args...]\n";
    return 1;
}