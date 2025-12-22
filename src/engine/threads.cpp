#include "engine/threads.h"
#include <unordered_map>

namespace engine {


Thread::Thread(SharedState& st, size_t n, size_t total)
    : idx(n)
    , nThreads(total)
    , worker(st, n)
{
    searching = false;
    exit = false;
    th = std::thread(&Thread::idleLoop, this);
}

Thread::~Thread() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        exit = true;
        cv.notify_all();
    }
    if (th.joinable()) {
        th.join();
    }
}

void Thread::idleLoop() {
    std::unique_lock<std::mutex> lk(mutex);

    for (;;) {
        cv.wait(lk, [this] { return searching || exit; });
        if (exit)
            break;
        bool doSearch = searching;
        lk.unlock();

        if (doSearch) {
            worker.startSearching();
        }
        lk.lock();
        searching = false;
        cv.notify_all();
    }
}

void Thread::startSearching(const Board& board, const SearchLimits sl) {
    std::lock_guard<std::mutex> lk(mutex);
    worker.setRoot(board, sl);
    searching = true;
    cv.notify_one();
}

void Thread::waitForSearch() {
    std::unique_lock<std::mutex> lk(mutex);
    cv.wait(lk, [&]{ return !searching; });
}

void Thread::clearWorker() {
    std::lock_guard<std::mutex> lk(mutex);
    worker.clear();
}

Move Thread::getBestMove() {
    std::lock_guard<std::mutex> lk(mutex);
    return worker.bestPv[0];
}

int Thread::getBestDepth() {
    std::lock_guard<std::mutex> lk(mutex);
    return worker.bestDepth;
}

int Thread::getBestScore() {
    std::lock_guard<std::mutex> lk(mutex);
    return worker.bestScore;
}

int Thread::getPVLength() {
    std::lock_guard<std::mutex> lk(mutex);
    int pvLength = 0;
    for (int i = 0; i < MAXMOVES; i++) {
        if (worker.bestPv[i] == NOMOVE) break;
        pvLength++;
    }
    return pvLength;
}

ThreadPool::ThreadPool()
{
    stop = false;
}

void ThreadPool::set(size_t n, TranspositionTable& tt) {
    threads.clear();
    if (n == 0) n = 1;
    threads.reserve(n);
    SharedState shared = {*this, tt};
    for (size_t i = 0; i < n; ++i) {
        threads.push_back(std::make_unique<Thread>(shared, i, n));
    }
}

void ThreadPool::startSearching(const Board& board, const SearchLimits sl) {
    stop = false;
    for (auto& t : threads) {
        t->startSearching(board, sl);
    }
}

void ThreadPool::waitForAllThreads() {
    for (auto& t : threads) {
        t->waitForSearch();
    }
}

void ThreadPool::waitForOthers(size_t idx) {
    for (auto& t : threads) {
        if (t->idx != idx) {
            t->waitForSearch();
        }
    }
}

void ThreadPool::clearThreads() {
    waitForAllThreads();
    for (auto& t : threads) {
        t->clearWorker();
    }
}

Move ThreadPool::voteBestMove() {
    Thread* bestThread = threads[0].get();
    Value minScore = NOVALUE;

    std::unordered_map<Move, int64_t> votes;

    for (auto&& th : threads) {
        minScore = std::min(minScore, th->getBestScore());
    }

    for (auto&& th : threads) {
        votes[th->getBestMove()] += (th->getBestScore() - minScore + 14) * th->getBestDepth();
    }

    for (auto&& th : threads) {
        int bestThreadScore = bestThread->getBestScore();
        int newThreadScore = th->getBestScore();

        int bestThreadVotingScore = votes[bestThread->getBestMove()];
        int newThreradVotingScore = votes[th->getBestMove()];

        if (isWin(bestThreadScore) || isLoss(bestThreadScore)) {
            if (newThreadScore > bestThreadScore) {
                bestThread = th.get();
            }
        } else if (newThreradVotingScore > bestThreadVotingScore || (newThreradVotingScore == bestThreadVotingScore && th->getPVLength() > bestThread->getPVLength())) {
            bestThread = th.get();
        }
    }
    return bestThread->getBestMove();
}

size_t ThreadPool::numThreads() {
    return threads.size();
}

void ThreadPool::incrementActive() {
    searchingThreads.fetch_add(1);
}

void ThreadPool::decrementActive() {
    searchingThreads.fetch_sub(1);
}

void ThreadPool::fireBestMove(Move m, int score, int depth, Move* pv) {
    if (bestMoveCallback) {
        bestMoveCallback(m, score, depth, pv);
    }
}

void ThreadPool::fireInfo(const SearchInfo& info) {
    if (infoCallback) {
        infoCallback(info);
    }
}
}