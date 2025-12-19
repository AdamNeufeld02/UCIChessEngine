#pragma once
#include <string>
#include "types.h"


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

    // Mobility
    Score knightMobility[9];
    Score bishopMobility[14];
    Score rookMobility[15];
    Score queenMobility[28];
};

extern Weights W;
extern int gamePhaseWeightings[PIECETYPECOUNT];

void init_eval_weights_default();

bool load_eval_weights_from_file(const std::string& path);

inline Square mirrorSquareIfBlack(Square sq, Colour c) {
    int mask = c * 56;
    return Square(sq ^ mask);
}

inline int relativeRank(Colour col, int rank) {
    return col == WHITE ? rank : 7 - rank;
}

Value evaluate(Board& board);
template<Colour col>
Score evaluatePawnStructure(Board& board);
template<Colour col>
Score evaluateKingShelter(Board& board);
template<Colour col>
Score evaluatePieceActivity(Board& board);
}