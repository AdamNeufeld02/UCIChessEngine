#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <functional> 

#include "board.h"
#include "types.h"
#include "search.h"
#include "transpostable.h"


namespace engine {

class ThreadPool;

class Thread {
public:
    Thread(SharedState& st, size_t n, size_t total);
    ~Thread();

    void idleLoop();
    void startSearching(const Board& board, const SearchLimits sl);
    void clearWorker();

    void waitForSearch();

    Move getBestMove();
    int getBestDepth();
    int getBestScore();
    int getPVLength();
    Move* getBestPv();

    size_t idx;
private:
    std::mutex mutex;
    std::condition_variable cv;
    size_t nThreads;
    Worker worker;
    bool searching = true;
    bool exit = false;
    std::thread th;
};




class ThreadPool {
public:
    ThreadPool();
    void startSearching(const Board& board, const SearchLimits sl);
    void clearThreads();
    Thread* voteBestMove();
    size_t numThreads();
    void set(size_t nThreads, TranspositionTable& tt);
    void waitForAllThreads();
    void waitForOthers(size_t idx);

    // === Callbacks ===
    using BestMoveCallback = std::function<void(Move best, int score, int depth, Move* pv)>;
    using InfoCallback = std::function<void(const SearchInfo&)>;

    void setBestMoveCallback(BestMoveCallback cb) { bestMoveCallback = std::move(cb); }
    void setInfoCallback(InfoCallback cb)         { infoCallback     = std::move(cb); }

    void fireBestMove(Move best, int score, int depth, Move* pv);
    void fireInfo(const SearchInfo& info);

    void incrementActive();
    void decrementActive();
    size_t activeSearchers() const { return searchingThreads.load(); }

    std::atomic_bool stop = false;
    std::atomic<size_t> searchingThreads;
private:
    std::vector<std::unique_ptr<Thread>> threads;

    BestMoveCallback bestMoveCallback;
    InfoCallback     infoCallback;

};
}