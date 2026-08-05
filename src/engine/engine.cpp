#include "engine/engine.h"
#include "engine/movegen.h"

namespace engine {

Engine::Engine() {
    board = Board();
}

void Engine::setPosition(std::string fen, std::vector<std::string>& moves) {
    states = std::make_unique<std::deque<State>>(1);
    board.fenToBoard(fen, &states->back());
    for (const auto& ms : moves) {
        Move m = uciStringToMove(ms);
        if (m == NOMOVE) return;
        states->emplace_back();
        board.makeMove(m, &states->back());
    }  
}

void Engine::setThreads(size_t n) {
    threadPool.set(n, tt);
}

void Engine::setTranspositionTable(size_t mb) {
    tt.resize(mb);
    tt.dumpInfo();
}

void Engine::calculateLimits(SearchLimits& limits) {
    if (limits.infinite || limits.depth > 0 || limits.movetime_ms > 0 || !limits.hasTimeControl) {
        if (limits.infinite) {
            limits.hardTimeLimitMs = limits.softTimeLimitMs = 0;
        } else if (limits.movetime_ms > 0) {
            limits.softTimeLimitMs = limits.movetime_ms * 0.9;
            limits.hardTimeLimitMs = limits.movetime_ms;
        } else if (!limits.hasTimeControl) {
            limits.hardTimeLimitMs = limits.softTimeLimitMs = 0;
        }
        return;
    }

    int myTime = (board.sideToMove() == WHITE) ? limits.wtime_ms : limits.btime_ms;
    int myInc  = (board.sideToMove() == WHITE) ? limits.winc_ms  : limits.binc_ms;

    myTime = std::max(myTime, 1);

    uint64_t T_move;
    if (limits.movestogo > 0) {
        int movesToGo = limits.movestogo;
        T_move = static_cast<uint64_t>(myTime / (movesToGo + 1)) + static_cast<uint64_t>(myInc / 2);
    } else {
        T_move = static_cast<uint64_t>(myTime / 20) + static_cast<uint64_t>(myInc / 2);
    }
    T_move = std::max<uint64_t>(T_move, 1);
    limits.softTimeLimitMs = T_move * 0.9;
    limits.hardTimeLimitMs = T_move;
}

void Engine::go(SearchLimits sl) {
    tt.newSearch();
    calculateLimits(sl);
    threadPool.startSearching(board, sl);
}

void Engine::stopSearch() {
    threadPool.stop = true;
}

void Engine::clearSearch() {
    threadPool.clearThreads();
    tt.clear();
}

Move Engine::uciStringToMove(std::string move) {
    MoveList moveList = MoveList<GEN_LEGAL>(board);

    for (const ScoredMove& sm : moveList) {
        if (move == moveToUciString(sm.move)) {
            return sm.move;
        }
    }

    return NOMOVE;
}

std::string Engine::squareUciString(Square sq) {
    return std::string{char('a' + fileOf(sq)), char('1' + rankOf(sq))};
}

std::string Engine::moveToUciString(Move move) {
    Square from = fromSq(move);
    Square to = toSq(move);

    std::string moveString = squareUciString(from) + squareUciString(to);

    if (moveFlag(move) == PROMOTION) {
        moveString += "**nbrq*"[promoPiece(move)];
    }

    return moveString;
}

}