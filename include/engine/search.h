#pragma once
#include "board.h"
#include "transpostable.h"
#include "move.h"
#include "history.h"
#include <vector>
#include <chrono>

namespace engine {

class ThreadPool;

struct SharedState {
    ThreadPool& threads;
    TranspositionTable& tt;
};

struct SearchInfo {
    int depth      = 0;
    int seldepth   = 0;
    int score      = 0;
    bool isMate    = false;
    std::uint64_t nodes   = 0;
    std::uint64_t timeMs  = 0;
    std::uint64_t nps     = 0;
    Move* pv;
};

struct SearchStack {
    int ply;
    Move* pv;
    Move current;
    PieceType movedPT;
    bool didNull;
};

constexpr int QSEARCHHISTORYDEPTH = 4;

constexpr int SEARCHEDLISTCAP = 32;
template<size_t MaxSize>
class SearchedMoves {
public:
    constexpr size_t size() const {return length;}
    constexpr void pushBack(Move move) {moves[length++] = move;}
    constexpr const Move* begin() const {return moves;}
    constexpr const Move& operator[](int index) const {return moves[index];}
    constexpr const Move* end() const { return moves + length; }

private:
    Move moves[MaxSize];
    size_t length = 0;
};

class Worker {
public:
    Worker(SharedState& st, size_t idx);

    void startSearching();
    void setRoot(const Board& root, const SearchLimits& l);
    void clear();

    void iterativeDeepening();
    Value aspirationSearch(SearchStack* ss, Board& board, Value center, int depth);
    Value rootSearch(SearchStack* ss, Board& board, Value alpha, Value beta, int depth);
    Value search(SearchStack* ss, Board& board, Value alpha, Value beta, int depth, bool pvNode);
    Value qsearch(SearchStack* ss, Board& board, Value alpha, Value beta);


private:

    void updatePV(Move* pv, Move move, Move* childPv);

    bool checkSoftLimit(int64_t elapsedMs);
    bool checkHardLimit(int64_t elapsedMs);
    bool checkLastManStanding();
    bool checkMainWorker();

    void updateHistories(Board& board, SearchStack* ss, SearchedMoves<SEARCHEDLISTCAP>& searchedCaptures, SearchedMoves<SEARCHEDLISTCAP>& searchedQuiets, Move bestMove, int depth);
    void updateContHistory(Board& board, SearchStack* ss, Move move, int reward);
    void updateMainHistory(Board& board, Move move, int reward);
    void updateCaptureHistory(Board& board, Move move, int reward);

    int calcReduction(Move move, int depth, int moveCount, bool pvNode);

    ThreadPool& threads;
    TranspositionTable& tt;
    History history;
    

    Board rootBoard;
    SearchLimits limits;
    size_t threadID;
    Move bestPv[MAXPLY];
    Value bestScore;
    std::chrono::steady_clock::time_point startTime;
    int bestDepth;
    int selDepth;
    int nodesSearched;

    friend class Thread;
};

}