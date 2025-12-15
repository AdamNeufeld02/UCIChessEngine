#pragma once
#include "types.h"
#include "move.h"
#include "hashing.h"
#include <tuple>


namespace engine {

struct TTData {
    Move move;
    Value eval;
    Value value;
    int depth;
    Bound bound;

    TTData(Move m, Value ev, Value val, int dpt, Bound bnd) :
        move(m),
        eval(ev),
        value(val),
        depth(dpt),
        bound(bnd) {};
};

struct TTEntry {
    uint16_t key16;
    int16_t move16;
    int16_t eval16;
    int16_t value16;
    uint8_t depth8;
    uint8_t generation8;
    uint8_t bound8;

    bool isOccupied();
    TTData read();
    void save(ZobristKey key, Move move, Value eval, Value value, int depth, uint8_t generation8, Bound bound);
};

static_assert(sizeof(TTEntry) == 12);

constexpr int BUCKETSIZE = 5;

struct Bucket {
    TTEntry entry[BUCKETSIZE];
    uint8_t pad[4];
};

static_assert(sizeof(Bucket) == 64);

struct TableWriter {
public:
    TableWriter(TTEntry* tte) : entry(tte) {};
    void write(ZobristKey key, Move move, Value eval, Value value, int depth, uint8_t generation, Bound bound);
private:
    TTEntry* entry;
};



class TranspositionTable {
public:

    void newSearch();
    void clear();
    void free();
    void resize(size_t mb);
    std::tuple<bool, TTData, TableWriter> probe(const ZobristKey key);
    TTEntry* bucketHead(const ZobristKey key);
    uint8_t currentGeneration();

    // Debug
    void dumpInfo() const;

private:
    Bucket* buckets;
    uint8_t generation;
    size_t bucketCount;

    // Debug
    bool usingLargePages = false;
    size_t bytesAllocated = 0;
};

inline uint8_t TranspositionTable::currentGeneration() {
    return generation;
}

}
