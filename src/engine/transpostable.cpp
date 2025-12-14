#include "engine/transpostable.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <algorithm>

#include <cstdio>

namespace engine {

static inline uint64_t fast_index(uint64_t x, uint64_t n) {
#if defined(_MSC_VER) && defined(_M_X64)
    return __umulh(x, n);
#else
    return (uint64_t)(((__uint128_t)x * n) >> 64);
#endif
}

inline size_t bucketIndex(uint64_t key, size_t bucketCount) {
    uint64_t x = key >> 16;
    return (size_t)fast_index(x, (uint64_t)bucketCount);
}

static size_t roundUp(size_t x, size_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

static bool enableLockMemoryPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    LUID luid{};
    if (!LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);

    
    DWORD err = GetLastError();
    CloseHandle(token);

    return err == ERROR_SUCCESS;
}

static void* allocNormal(size_t bytes) {
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

static void* allocLargePages(size_t& bytesInOut) {
    SIZE_T lp = GetLargePageMinimum();
    if (!lp) return nullptr;

    enableLockMemoryPrivilege();

    bytesInOut = roundUp(bytesInOut, (size_t)lp);

    return VirtualAlloc(nullptr, bytesInOut,
                        MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                        PAGE_READWRITE);
}

static void freePages(void* p) {
    if (p) VirtualFree(p, 0, MEM_RELEASE);
}

bool TTEntry::isOccupied() {
    return bool(key16);
}

TTData TTEntry::read() {
    return TTData(Move(move16), Value(eval16), Value(value16), depth8, Bound(bound8));
}

void TTEntry::save(ZobristKey key, Move move, Value eval, Value value, int depth, uint8_t generation, Bound bound) {
    generation8 = generation;

    if (move || uint16_t(key) != key16) {
        move16 = move;
    }

    if ((depth > depth8) || (bound == EXACT && (depth >= depth8)) || key16 != uint16_t(key)) {
        key16 = uint16_t(key);
        eval16 = int16_t(eval);
        value16 = int16_t(value);
        depth8 = uint8_t(depth);
        bound8 = uint8_t(bound);
    }
}

void TableWriter::write(ZobristKey key, Move move, Value eval, Value value, int depth, uint8_t generation, Bound bound) {
    entry->save(key, move, eval, value, depth, generation, bound);
}

void TranspositionTable::clear() {
    generation = 0;
    if (buckets && bucketCount) {
        std::memset(buckets, 0, bucketCount * sizeof(Bucket));
    }
}

void TranspositionTable::newSearch() {
    generation++;
}

void TranspositionTable::free() {
    if (buckets) {
        VirtualFree(buckets, 0, MEM_RELEASE);
        buckets = nullptr;
        bucketCount = 0;
    }
}

void TranspositionTable::resize(size_t mb) {
    if (mb < 1) mb = 1;

    freePages(buckets);
    buckets = nullptr;
    bucketCount = 0;
    usingLargePages = false;
    bytesAllocated = 0;

    const size_t bytesRequested = mb * 1024ull * 1024ull;

    size_t bytes = (bytesRequested / sizeof(Bucket)) * sizeof(Bucket);
    if (bytes < sizeof(Bucket)) bytes = sizeof(Bucket);

    size_t bytesLP = bytes;
    void* mem = allocLargePages(bytesLP);

    if (mem) {
        buckets = static_cast<Bucket*>(mem);
        bucketCount = bytesLP / sizeof(Bucket);
        usingLargePages = true;
        bytesAllocated = bytesLP;
        clear();
        return;
    }

    mem = allocNormal(bytes);
    if (!mem) {
        buckets = nullptr;
        bucketCount = 0;
        return;
    }

    buckets = static_cast<Bucket*>(mem);
    bucketCount = bytes / sizeof(Bucket);
    usingLargePages = false;
    bytesAllocated = bytes;
    clear();
}

std::tuple<bool, TTData, TableWriter> TranspositionTable::probe(ZobristKey key) {
    TTEntry* tte = bucketHead(key);
    uint16_t key16 = uint16_t(key);

    for (int i = 0; i < BUCKETSIZE; i++) {
        if (tte[i].key16 == key16) {
            return {true, tte[i].read(), TableWriter(&tte[i])};
        }
    }
    TTEntry* replace = &tte[0];
    int victimScore = VALUEINFINITE;
    for (int i = 0; i < BUCKETSIZE; i++) {
        if (!tte[i].isOccupied()) {
            replace = &tte[i];
            break;
        } else {
            int age   = (int)(uint8_t)(generation - tte[i].generation8);
            int exactBonus = tte[i].bound8 == EXACT ? 2 : 0;
            int score = tte[i].depth8 - 3 * age + exactBonus;
            if (score < victimScore) {
                victimScore = score;
                replace = &tte[i];
            }
        }
    }
    return{false, {NOMOVE, NOVALUE, NOVALUE, 0, NOBOUND}, TableWriter(replace)};
}

TTEntry* TranspositionTable::bucketHead(const ZobristKey key) {
    size_t idx = bucketIndex(key, bucketCount);
    return buckets[idx].entry;
}

void TranspositionTable::dumpInfo() const {
    printf("TT: %zu MB (%s pages)\n",
       bytesAllocated / (1024*1024),
       usingLargePages ? "LARGE" : "normal");
}

}