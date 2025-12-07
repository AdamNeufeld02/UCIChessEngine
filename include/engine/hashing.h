#pragma once

#include <cstdint>
#include "engine/types.h"

namespace engine {

typedef uint64_t ZobristKey;

namespace zobrist {
extern ZobristKey psq[PIECECOUNT][SQUARECOUNT];
extern ZobristKey castlingKey[16];
extern ZobristKey epFile[8];
extern ZobristKey sideToMoveKey;


void initZobrist();
}
}