#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include "types.h"
#include "hashing.h"


namespace engine {

class Board;

struct Score {
    int mg = 0;
    int eg = 0;

    constexpr Score& operator+=(const Score& other) noexcept {
        mg += other.mg;
        eg += other.eg;
        return *this;
    }

    constexpr Score& operator-=(const Score& other) noexcept {
        mg -= other.mg;
        eg -= other.eg;
        return *this;
    }
};
inline Score operator+(Score a, Score b){ return {a.mg+b.mg, a.eg+b.eg}; }
inline Score operator-(Score a, Score b){ return {a.mg-b.mg, a.eg-b.eg}; }
inline Score operator*(Score a, int k){ return {a.mg*k, a.eg*k}; }

struct Weights {
    Score material[PIECETYPECOUNT];
    Score psqt[PIECETYPECOUNT][SQUARECOUNT];

    // Pawn structure
    Score passedPawn[8];
    Score connectedPawn[8];
    Score isolatedPawn;
    Score doubledPawn;
    Score backwardPawn;
    Score hangingPawn;
    Score supportedPawn;

    // King shelter
    Score shelter[4][8];
    Score blockedStorm[8];
    Score unblockedStorm[8];
    Score kingOpenFile[2][2];

    // King safety (attacker weighting -> weighted attack count -> safety curve)
    int attackerWeight[8];
    Score safetyTable[100];

    // Mobility
    Score knightMobility[9];
    Score bishopMobility[14];
    Score rookMobility[15];
    Score queenMobility[28];
    Score rookOpenFile[2][2];

    // Misc
    Score bishopPair;
    Score tempo;
};

extern Weights W;
extern int gamePhaseWeightings[PIECETYPECOUNT];

void init_eval_weights_default();

bool load_eval_weights_from_file(const std::string& path);
bool save_eval_weights_to_file(const std::string& path, Weights& w);

// One named handle per tunable scalar inside a Weights instance - built by
// walking the same field structure init_eval_weights_default() populates, so
// the name list always matches whatever Weights actually contains. Used for
// the semi-human-readable weights file format and by the tuner.
struct TunableParam {
    std::string name;
    int* value;
};
std::vector<TunableParam> enumerateTunableParams(Weights& w);

// Tuner-support tracing. When g_traceSink is non-null, every eval-term
// accumulation inside evaluatePawnStructure/evaluateKingShelter/
// evaluatePieceActivity also records which Score field fired and with what
// signed count, alongside its normal contribution to the running Score - the
// same code path either way, so a trace can never drift from what evaluate()
// actually computes. Null (the default, and always the case during search)
// costs one pointer check per accumulation and changes nothing else.
struct TraceEntry {
    Score* param;
    int count;
};
extern thread_local std::vector<TraceEntry>* g_traceSink;

inline Square mirrorSquareIfBlack(Square sq, Colour c) {
    int mask = c * 56;
    return Square(sq ^ mask);
}

inline int relativeRank(Colour col, int rank) {
    return col == WHITE ? rank : 7 - rank;
}

// Per-thread pawn hash: keyed by a joint (both colours) pawn-only Zobrist
// key, independent of side to move. King squares are not part of the key -
// shelter is revalidated lazily per colour whenever that colour's king has
// moved off the square it was last computed for, without invalidating the
// (more expensive) pawn structure half of the entry.
constexpr size_t PAWN_TABLE_SIZE = 1 << 15;

struct PawnEntry {
    ZobristKey key = 0;
    Score pawnScore[COLOURNB];
    Score shelterScore[COLOURNB];
    Square kingSquare[COLOURNB] = {NOSQUARE, NOSQUARE};
    uint8_t pad[64 - (sizeof(ZobristKey) + 2 * sizeof(Score) * COLOURNB + sizeof(Square) * COLOURNB)];
};
static_assert(sizeof(PawnEntry) == 64);

Value evaluate(Board& board, PawnEntry* pawnTable);
template<Colour col>
Score evaluatePawnStructure(Board& board);
template<Colour col>
Score evaluateKingShelter(Board& board);
template<Colour col>
Score evaluatePieceActivity(Board& board);
}