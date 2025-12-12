#pragma once
#include <string>
#include "types.h"


namespace engine {

class Board;

struct Weights {
    int material[PHASENB][PIECETYPECOUNT];
    int psqt[PHASENB][PIECETYPECOUNT][SQUARECOUNT];
};

extern Weights W;
extern int gamePhaseWeightings[PIECETYPECOUNT];

void init_eval_weights_default();

bool load_eval_weights_from_file(const std::string& path);

inline Square mirrorSquareIfBlack(Square sq, Colour c) {
    int mask = c * 56;
    return Square(sq ^ mask);
}

Value evaluate(Board& board);

}