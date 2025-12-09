#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <cstdint>

#include "engine/bitboards.h"
#include "engine/board.h"
#include "engine/move.h"
#include "engine/movegen.h"

static char pieceToChar(engine::Piece p) {
    if (p == engine::EMPTY) return '.';

    engine::PieceType pt = typeOf(p);
    engine::Colour col   = colourOf(p);

    char c = '?';
    switch (pt) {
        case engine::PAWN:   c = 'P'; break;
        case engine::KNIGHT: c = 'N'; break;
        case engine::BISHOP: c = 'B'; break;
        case engine::ROOK:   c = 'R'; break;
        case engine::QUEEN:  c = 'Q'; break;
        case engine::KING:   c = 'K'; break;
        case engine::NONE:   c = '.'; break;
    }
    return (col == engine::WHITE ? c : tolower(c));
}
static void printBoard(const engine::Board& b) {
    std::cout << "\n============== CURRENT BOARD ==============\n\n";

    for (int rank = 7; rank >= 0; --rank) {
        std::cout << (rank + 1) << "  ";
        for (int file = 0; file < 8; ++file) {
            engine::Square sq = static_cast<engine::Square>(rank * 8 + file);
            engine::Piece p   = b.pieceOn(sq);
            std::cout << pieceToChar(p) << " ";
        }
        std::cout << "\n";
    }

    std::cout << "\n   a b c d e f g h\n";

    std::cout << "\nSide to move: "
              << (b.sideToMove() == engine::WHITE ? "White" : "Black") << "\n";

    std::cout << "All pieces BB: 0x" << std::hex << b.allPieces << std::dec << "\n";
    std::cout << "White pieces BB: 0x" << std::hex << b.colourBB[engine::WHITE] << std::dec << "\n";
    std::cout << "Black pieces BB: 0x" << std::hex << b.colourBB[engine::BLACK] << std::dec << "\n";

    std::cout << "White king square: " << int(b.kingSquare(engine::WHITE)) << "\n";
    std::cout << "Black king square: " << int(b.kingSquare(engine::BLACK)) << "\n";

    std::cout << "\n===========================================\n\n";
}
namespace engine {

static std::string squareToString(Square sq) {
    int s = static_cast<int>(sq);
    int file = s % 8;
    int rank = s / 8;
    char buf[3];
    buf[0] = static_cast<char>('a' + file);
    buf[1] = static_cast<char>('1' + rank);
    buf[2] = '\0';
    return std::string(buf);
}

static char promoPieceToChar(PieceType pt) {
    switch (pt) {
    case QUEEN:  return 'q';
    case ROOK:   return 'r';
    case BISHOP: return 'b';
    case KNIGHT: return 'n';
    default:     return 'q';
    }
}

static std::string moveToUci(Move m) {
    std::string s = squareToString(fromSq(m)) + squareToString(toSq(m));
    if (isPromotion(m)) {
        s.push_back(promoPieceToChar(promoPiece(m)));
    }
    return s;
}

std::uint64_t perft(Board& board, int depth) {
    using namespace engine;

    if (depth == 0) {
        return 1;
    }

    Move moves[256];
    Move* end = generate<GEN_LEGAL>(board, moves);

    if (depth == 1) {
        return static_cast<std::uint64_t>(end - moves);
    }

    std::uint64_t nodes = 0;

    for (Move* m = moves; m != end; ++m) {
        State newState{};
        board.makeMove(*m, &newState);
        nodes += perft(board, depth - 1);
        board.undoMove(*m);
    }

    return nodes;
}


void perft_root(Board& board, int depth) {
    using clock = std::chrono::steady_clock;

    Move moves[256];
    Move* rootEnd = generate<GEN_LEGAL>(board, moves);

    std::cout << "Perft results\n";
    std::cout << "  Depth: " << depth << "\n";
    std::cout << "  Root moves: " << (rootEnd - moves) << "\n\n";

    auto start = clock::now();

    std::uint64_t totalNodes = 0;

    for (Move* m = moves; m != rootEnd; ++m) {
        State newState{};
        board.makeMove(*m, &newState);
        std::uint64_t nodes = perft(board, depth - 1);
        board.undoMove(*m);

        totalNodes += nodes;

        std::cout << "  " << moveToUci(*m) << ": " << nodes << "\n";
    }

    auto stop = clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    double seconds = ms / 1000.0;

    std::cout << "\n";
    std::cout << "  Total nodes: " << totalNodes << "\n";
    std::cout << "  Time: " << ms << " ms\n";

    if (seconds > 0.0) {
        std::uint64_t nps = static_cast<std::uint64_t>(totalNodes / seconds);
        std::cout << "  NPS: " << nps << "\n";
    } else {
        std::cout << "  NPS: (too fast to measure)\n";
    }
}

}

// ----------------------
// CLI front-end
// ----------------------
namespace perft_cli {

static int parseDepthOrDefault(const char* arg, int defaultDepth) {
    try {
        return std::stoi(arg);
    } catch (...) {
        std::cerr << "Invalid depth '" << arg << "', using default depth "
                  << defaultDepth << "\n";
        return defaultDepth;
    }
}

static std::string joinFenArgs(int argc, char** argv, int beginIndex) {
    std::ostringstream oss;
    for (int i = beginIndex; i < argc; ++i) {
        if (i > beginIndex) oss << ' ';
        oss << argv[i];
    }
    return oss.str();
}

int run(int argc, char** argv) {
    using namespace engine;

    int depth = 4;
    std::string fen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    if (argc >= 2) {
        depth = parseDepthOrDefault(argv[1], depth);
    }

    if (argc >= 3) {
        fen = joinFenArgs(argc, argv, 2);
    }

    engine::bb::init();

    Board board;
    State rootState{};
    board.fenToBoard(fen, &rootState);

    std::cout << "Running perft\n";
    std::cout << "  FEN:   " << fen << "\n";
    std::cout << "  Depth: " << depth << "\n\n";

    perft_root(board, depth);

    return 0;
}

}