#include "engine/eval.h"
#include "engine/board.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>

namespace engine {

// ---------- Pesto base arrays ----------

// material values [EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING]
static const int mgMat[PIECETYPECOUNT] = { 0, 82, 337, 365, 477, 1025,  0 };
static const int egMat[PIECETYPECOUNT] = { 0, 94, 281, 297, 512,  936,  0 };

static const int mgPawnTable[SQUARECOUNT] = {
      0,   0,   0,   0,   0,   0,  0,   0,
     98, 134,  61,  95,  68, 126, 34, -11,
     -6,   7,  26,  31,  65,  56, 25, -20,
    -14,  13,   6,  21,  23,  12, 17, -23,
    -27,  -2,  -5,  12,  17,   6, 10, -25,
    -26,  -4,  -4, -10,   3,   3, 33, -12,
    -35,  -1, -20, -23, -15,  24, 38, -22,
      0,   0,   0,   0,   0,   0,  0,   0,    
};

static const int egPawnTable[SQUARECOUNT] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0,    
};

static const int mgKnightTable[SQUARECOUNT] = {
    -167, -89, -34, -49,  61, -97, -15, -107,
     -73, -41,  72,  36,  23,  62,   7,  -17,
     -47,  60,  37,  65,  84, 129,  73,   44,
      -9,  17,  19,  53,  37,  69,  18,   22,
     -13,   4,  16,  13,  28,  19,  21,   -8,
     -23,  -9,  12,  10,  19,  17,  25,  -16,
     -29, -53, -12,  -3,  -1,  18, -14,  -19,
    -105, -21, -58, -33, -17, -28, -19,  -23,
};

static const int egKnightTable[SQUARECOUNT] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64,
};

static const int mgBishopTable[SQUARECOUNT] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21,
};

static const int egBishopTable[SQUARECOUNT] = {
    -14, -21, -11,  -8, -7,  -9, -17, -24,
     -8,  -4,   7, -12, -3, -13,  -4, -14,
      2,  -8,   0,  -1, -2,   6,   0,   4,
     -3,   9,  12,   9, 14,  10,   3,   2,
     -6,   3,  13,  19,  7,  10,  -3,  -9,
    -12,  -3,   8,  10, 13,   3,  -7, -15,
    -14, -18,  -7,  -1,  4,  -9, -15, -27,
    -23,  -9, -23,  -5, -9, -16,  -5, -17,
};

static const int mgRookTable[SQUARECOUNT] = {
     32,  42,  32,  51, 63,  9,  31,  43,
     27,  32,  58,  62, 80, 67,  26,  44,
     -5,  19,  26,  36, 17, 45,  61,  16,
    -24, -11,   7,  26, 24, 35,  -8, -20,
    -36, -26, -12,  -1,  9, -7,   6, -23,
    -45, -25, -16, -17,  3,  0,  -5, -33,
    -44, -16, -20,  -9, -1, 11,  -6, -71,
    -19, -13,   1,  17, 16,  7, -37, -26,
};

static const int egRookTable[SQUARECOUNT] = {
    13, 10, 18, 15, 12,  12,   8,   5,
    11, 13, 13, 11, -3,   3,   8,   3,
     7,  7,  7,  5,  4,  -3,  -5,  -3,
     4,  3, 13,  1,  2,   1,  -1,   2,
     3,  5,  8,  4, -5,  -6,  -8, -11,
    -4,  0, -5, -1, -7, -12,  -8, -16,
    -6, -6,  0,  2, -9,  -9, -11,  -3,
    -9,  2,  3, -1, -5, -13,   4, -20,
};

static const int mgQueenTable[SQUARECOUNT] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50,
};

static const int egQueenTable[SQUARECOUNT] = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41,
};

static const int mgKingTable[SQUARECOUNT] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14,
};

static const int egKingTable[SQUARECOUNT] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43
};

static const int attackerWeight[8] = {0, 1, 2, 2, 3, 6, 0};

static const Score safetyTable[100] = {
    Score(0, 0),  Score(0, 0),   Score(1, 1),   Score(2, 2),   Score(3, 3),
    Score(5, 5),  Score(7, 7),   Score(9, 9),   Score(12, 12), Score(15, 15),
    Score(18, 18),  Score(22, 22),  Score(26, 26),  Score(30, 30),  Score(35, 35),
    Score(39, 39),  Score(44, 44),  Score(50, 50),  Score(56, 56),  Score(62, 62),
    Score(68, 68),  Score(75, 75),  Score(82, 82),  Score(85, 85),  Score(89, 89),
    Score(97, 97), Score(105, 105), Score(113, 113), Score(122, 122), Score(131,131),
    Score(140, 140), Score(150, 150), Score(169, 169), Score(180, 180), Score(191, 191),
    Score(202, 202), Score(213, 213), Score(225, 225), Score(237, 237), Score(248, 248),
    Score(260, 260), Score(272, 272), Score(283, 283), Score(295, 295), Score(307, 307),
    Score(319, 319), Score(330, 330), Score(342, 342), Score(354, 354), Score(366, 366),
    Score(377, 377), Score(389, 389), Score(401, 401), Score(412, 412), Score(424, 424),
    Score(436, 436), Score(448, 448), Score(459, 459), Score(471, 471), Score(483, 483),
    Score(494, 494), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500),
    Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500),
    Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500),
    Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500),
    Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500),
    Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500),
    Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500),
    Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500), Score(500, 500)
};

static const Score shelter[4][8] = {
    {
        Score{-22, 0},
        Score{+16, 0},
        Score{+12, 0},
        Score{ +8, 0},
        Score{ +4, 0},
        Score{  0, 0},
        Score{ -6, 0},
        Score{-12, 0}
    },
    {
        Score{-20, 0},
        Score{+14, 0},
        Score{+10, 0},
        Score{ +6, 0},
        Score{ +2, 0},
        Score{ -2, 0},
        Score{ -7, 0},
        Score{-13, 0}
    },
    {
        Score{-18, 0},
        Score{+12, 0},
        Score{ +8, 0},
        Score{ +4, 0},
        Score{  0, 0},
        Score{ -4, 0},
        Score{ -8, 0},
        Score{-14, 0}
    },
    {
        Score{-16, 0},
        Score{+10, 0},
        Score{ +6, 0},
        Score{ +2, 0},
        Score{ -2, 0},
        Score{ -6, 0},
        Score{-10, 0},
        Score{-16, 0}
    }
};

static const Score unblockedStorm[8] = {
    Score{  0, 0},
    Score{-18, 0},
    Score{-14, 0},
    Score{-10, 0},
    Score{ -6, 0},
    Score{ -3, 0},
    Score{ -1, 0},
    Score{  0, 0}
};

static const Score blockedStorm[8] = {
    Score{  0, 0},
    Score{-10, 0},
    Score{ -8, 0},
    Score{ -6, 0},
    Score{ -4, 0},
    Score{ -2, 0},
    Score{ -1, 0},
    Score{  0, 0}
};

static const Score kingOpenFile[2][2] = {
    { Score{  0, 0}, Score{ -4, 0} },
    { Score{-10, 0}, Score{-18, 0} }
};

static const Score rookOpenFile[2][2] = {
    { Score{ 0, 0}, Score{ 0, 0} },
    { Score{10, 5}, Score{20,10} }
};

Score knightMobility[9] = {
    Score{-8, -4},
    Score{-4, -2},
    Score{-1, -1},
    Score{ 1,  0},
    Score{ 3,  1},
    Score{ 5,  2},
    Score{ 7,  3},
    Score{ 9,  4},
    Score{11,  5}
};

Score bishopMobility[14] = {
    Score{-10, -5},
    Score{ -6, -3},
    Score{ -3, -2},
    Score{ -1, -1},
    Score{  1,  0},
    Score{  3,  1},
    Score{  5,  2},
    Score{  7,  3},
    Score{  9,  4},
    Score{ 11,  5},
    Score{ 13,  6},
    Score{ 15,  7},
    Score{ 17,  8},
    Score{ 19,  9}
};

Score rookMobility[15] = {
    Score{-8, -4},
    Score{-5, -3},
    Score{-3, -2},
    Score{-1, -1},
    Score{ 1,  0},
    Score{ 3,  1},
    Score{ 5,  2},
    Score{ 7,  3},
    Score{ 9,  4},
    Score{11,  5},
    Score{13,  6},
    Score{15,  7},
    Score{17,  8},
    Score{19,  9},
    Score{21, 10}
};

Score queenMobility[28] = {
    Score{-6, -3},
    Score{-5, -3},
    Score{-4, -2}, 
    Score{-3, -2},
    Score{-2, -1},
    Score{-1, -1},
    Score{ 0,  0},
    Score{ 1,  0},
    Score{ 2,  1},
    Score{ 3,  1},
    Score{ 4,  2},
    Score{ 5,  2},
    Score{ 6,  3},
    Score{ 7,  3},
    Score{ 8,  4},
    Score{ 9,  4},
    Score{10,  5},
    Score{11,  5},
    Score{12,  6},
    Score{13,  6},
    Score{14,  7},
    Score{15,  7},
    Score{16,  8},
    Score{17,  8},
    Score{18,  9},
    Score{19,  9},
    Score{20, 10},
    Score{21, 10}
};

int gamePhaseWeightings[PIECETYPECOUNT] = {0, 0, 1, 1, 2, 4, 0};

Weights W;

static void copy_psqt(Phase phase, PieceType pt, const int src[64]) {
    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        if (phase == PHASEMG) {
            W.psqt[pt][sq].mg = src[mirrorSquareIfBlack(Square(sq), BLACK)];
        } else {
            W.psqt[pt][sq].eg = src[mirrorSquareIfBlack(Square(sq), BLACK)];
        }
        
    }
}

void init_eval_weights_default() {
    // 1) Material
    for (int pt = 0; pt < PIECETYPECOUNT; ++pt) {
        W.material[pt].mg = mgMat[pt];
        W.material[pt].eg = egMat[pt];
    }

    // 2) PSQT

    // Pesto only gives tables for P,N,B,R,Q,K
    copy_psqt(PHASEMG, PAWN,   mgPawnTable);
    copy_psqt(PHASEEG, PAWN,   egPawnTable);

    copy_psqt(PHASEMG, KNIGHT, mgKnightTable);
    copy_psqt(PHASEEG, KNIGHT, egKnightTable);

    copy_psqt(PHASEMG, BISHOP, mgBishopTable);
    copy_psqt(PHASEEG, BISHOP, egBishopTable);

    copy_psqt(PHASEMG, ROOK,   mgRookTable);
    copy_psqt(PHASEEG, ROOK,   egRookTable);

    copy_psqt(PHASEMG, QUEEN,  mgQueenTable);
    copy_psqt(PHASEEG, QUEEN,  egQueenTable);

    copy_psqt(PHASEMG, KING,   mgKingTable);
    copy_psqt(PHASEEG, KING,   egKingTable);

    // Pawn Structure
    W.isolatedPawn  = Score{-10,  -8};
    W.doubledPawn   = Score{-12, -10};

    W.backwardPawn  = Score{ -6,  -2};
    W.hangingPawn   = Score{ -4,  -2};

    W.supportedPawn = Score{  3,   2};

    W.connectedPawn[0] = Score{0, 0};
    W.connectedPawn[1] = Score{2, 2};
    W.connectedPawn[2] = Score{4, 4};
    W.connectedPawn[3] = Score{6, 6};
    W.connectedPawn[4] = Score{8, 8};
    W.connectedPawn[5] = Score{10,10};
    W.connectedPawn[6] = Score{12,12};
    W.connectedPawn[7] = Score{0, 0};

    W.passedPawn[0] = Score{0,   0};
    W.passedPawn[1] = Score{6,  10};
    W.passedPawn[2] = Score{10, 18};
    W.passedPawn[3] = Score{16, 28};
    W.passedPawn[4] = Score{26, 45};
    W.passedPawn[5] = Score{40, 70};
    W.passedPawn[6] = Score{70,120};
    W.passedPawn[7] = Score{0,   0};

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            W.shelter[i][j] = shelter[i][j];
        }
    }

    for (int i = 0; i < 8; i++) {
        W.unblockedStorm[i] = unblockedStorm[i];
        W.blockedStorm[i] = blockedStorm[i];
    }

    W.kingOpenFile[0][0] = kingOpenFile[0][0];
    W.kingOpenFile[0][1] = kingOpenFile[0][1];
    W.kingOpenFile[1][0] = kingOpenFile[1][0];
    W.kingOpenFile[1][1] = kingOpenFile[1][1];

    for (int i = 0; i < 8; i++) {
        W.attackerWeight[i] = attackerWeight[i];
    }

    for (int i = 0; i < 100; i++) {
        W.safetyTable[i] = safetyTable[i];
    }

    W.rookOpenFile[0][0] = rookOpenFile[0][0];
    W.rookOpenFile[0][1] = rookOpenFile[0][1];
    W.rookOpenFile[1][0] = rookOpenFile[1][0];
    W.rookOpenFile[1][1] = rookOpenFile[1][1];

    W.bishopPair = Score{25, 35};
    W.tempo      = Score{20, 10};

    for (int i = 0; i < 9; i++) {
        W.knightMobility[i] = knightMobility[i];
    }

    for (int i = 0; i < 14; i++) {
        W.bishopMobility[i] = bishopMobility[i];
    }

    for (int i = 0; i< 15; i++) {
        W.rookMobility[i] = rookMobility[i];
    }

    for (int i = 0; i < 28; i++) {
        W.queenMobility[i] = queenMobility[i];
    }
}

static const char* pieceTypeShortName(int pt) {
    switch (pt) {
        case NONE:   return "NONE";
        case PAWN:   return "PAWN";
        case KNIGHT: return "KNIGHT";
        case BISHOP: return "BISHOP";
        case ROOK:   return "ROOK";
        case QUEEN:  return "QUEEN";
        case KING:   return "KING";
        default:     return "SPARE";
    }
}

static std::string squareName(int sq) {
    std::string s;
    s += char('a' + (sq & 7));
    s += char('1' + (sq >> 3));
    return s;
}

std::vector<TunableParam> enumerateTunableParams(Weights& w) {
    std::vector<TunableParam> params;
    params.reserve(1500);

    auto addScore = [&](const std::string& name, Score& s) {
        params.push_back({name + ".mg", &s.mg});
        params.push_back({name + ".eg", &s.eg});
    };
    auto addInt = [&](const std::string& name, int& v) {
        params.push_back({name, &v});
    };

    for (int pt = 0; pt < PIECETYPECOUNT; ++pt) {
        addScore(std::string("material.") + pieceTypeShortName(pt), w.material[pt]);
    }

    for (int pt = 0; pt < PIECETYPECOUNT; ++pt) {
        for (int sq = 0; sq < SQUARECOUNT; ++sq) {
            addScore(std::string("psqt.") + pieceTypeShortName(pt) + "." + squareName(sq), w.psqt[pt][sq]);
        }
    }

    for (int r = 0; r < 8; ++r) addScore("passedPawn.rank" + std::to_string(r), w.passedPawn[r]);
    for (int r = 0; r < 8; ++r) addScore("connectedPawn.rank" + std::to_string(r), w.connectedPawn[r]);

    addScore("isolatedPawn", w.isolatedPawn);
    addScore("doubledPawn", w.doubledPawn);
    addScore("backwardPawn", w.backwardPawn);
    addScore("hangingPawn", w.hangingPawn);
    addScore("supportedPawn", w.supportedPawn);

    for (int f = 0; f < 4; ++f)
        for (int r = 0; r < 8; ++r)
            addScore("shelter.file" + std::to_string(f) + ".rank" + std::to_string(r), w.shelter[f][r]);

    for (int r = 0; r < 8; ++r) addScore("blockedStorm.rank" + std::to_string(r), w.blockedStorm[r]);
    for (int r = 0; r < 8; ++r) addScore("unblockedStorm.rank" + std::to_string(r), w.unblockedStorm[r]);

    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            addScore("kingOpenFile.own" + std::to_string(a) + ".enemy" + std::to_string(b), w.kingOpenFile[a][b]);

    for (int pt = 0; pt < 8; ++pt)
        addInt(std::string("attackerWeight.") + pieceTypeShortName(pt), w.attackerWeight[pt]);

    for (int i = 0; i < 100; ++i) addScore("safetyTable[" + std::to_string(i) + "]", w.safetyTable[i]);

    for (int i = 0; i < 9;  ++i) addScore("knightMobility[" + std::to_string(i) + "]", w.knightMobility[i]);
    for (int i = 0; i < 14; ++i) addScore("bishopMobility[" + std::to_string(i) + "]", w.bishopMobility[i]);
    for (int i = 0; i < 15; ++i) addScore("rookMobility[" + std::to_string(i) + "]", w.rookMobility[i]);
    for (int i = 0; i < 28; ++i) addScore("queenMobility[" + std::to_string(i) + "]", w.queenMobility[i]);

    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            addScore("rookOpenFile.own" + std::to_string(a) + ".enemy" + std::to_string(b), w.rookOpenFile[a][b]);

    addScore("bishopPair", w.bishopPair);
    addScore("tempo", w.tempo);

    return params;
}

bool save_eval_weights_to_file(const std::string& path, Weights& w) {
    std::ofstream out(path);
    if (!out) return false;

    out << "# UCIChessEngine eval weights\n";
    out << "# format: <name> <value>, one per line, '#' starts a comment.\n";
    out << "# A missing/unrecognized name is skipped on load, not an error.\n";

    for (const TunableParam& p : enumerateTunableParams(w)) {
        out << p.name << " " << *p.value << "\n";
    }

    return bool(out);
}

bool load_eval_weights_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    std::unordered_map<std::string, int*> byName;
    for (TunableParam& p : enumerateTunableParams(W)) {
        byName[p.name] = p.value;
    }

    std::string line;
    int lineNo = 0;
    int applied = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);

        std::istringstream iss(line);
        std::string name;
        if (!(iss >> name)) continue;

        int value;
        if (!(iss >> value)) {
            std::cerr << "load_eval_weights_from_file: " << path << ":" << lineNo
                      << ": missing value for '" << name << "', skipping\n";
            continue;
        }

        auto it = byName.find(name);
        if (it == byName.end()) {
            std::cerr << "load_eval_weights_from_file: " << path << ":" << lineNo
                      << ": unknown parameter '" << name << "', skipping\n";
            continue;
        }

        *(it->second) = value;
        ++applied;
    }

    return applied > 0;
}

thread_local std::vector<TraceEntry>* g_traceSink = nullptr;

static inline void accum(Score& score, Score& term, int count) {
    score += term * count;
    if (g_traceSink) g_traceSink->push_back({&term, count});
}

Value evaluate(Board& board, PawnEntry* pawnTable) {
    Colour sideToMove = board.sideToMove();
    int gamePhase = 0;
    gamePhase += popcount(board.pieces(WHITE, KNIGHT) | board.pieces(BLACK, KNIGHT)) * gamePhaseWeightings[KNIGHT];
    gamePhase += popcount(board.pieces(WHITE, BISHOP) | board.pieces(BLACK, BISHOP)) * gamePhaseWeightings[BISHOP];
    gamePhase += popcount(board.pieces(WHITE, ROOK) | board.pieces(BLACK, ROOK)) * gamePhaseWeightings[ROOK];
    gamePhase += popcount(board.pieces(WHITE, QUEEN) | board.pieces(BLACK, QUEEN)) * gamePhaseWeightings[QUEEN];

    Score matScore = board.material[sideToMove] - board.material[~sideToMove];
    Score psqtScore = board.psqtv[sideToMove] - board.psqtv[~sideToMove];

    PawnEntry& pe = pawnTable[board.pawnKey() & (PAWN_TABLE_SIZE - 1)];
    if (pe.key != board.pawnKey()) {
        pe.key = board.pawnKey();
        pe.pawnScore[WHITE] = evaluatePawnStructure<WHITE>(board);
        pe.pawnScore[BLACK] = evaluatePawnStructure<BLACK>(board);
        // Force a shelter recompute below - a colliding/replaced entry's
        // cached king squares belong to a different pawn structure.
        pe.kingSquare[WHITE] = NOSQUARE;
        pe.kingSquare[BLACK] = NOSQUARE;
    }
    Square wksq = board.kingSquare(WHITE);
    if (pe.kingSquare[WHITE] != wksq) {
        pe.kingSquare[WHITE] = wksq;
        pe.shelterScore[WHITE] = evaluateKingShelter<WHITE>(board);
    }
    Square bksq = board.kingSquare(BLACK);
    if (pe.kingSquare[BLACK] != bksq) {
        pe.kingSquare[BLACK] = bksq;
        pe.shelterScore[BLACK] = evaluateKingShelter<BLACK>(board);
    }

    Score pawnStruct   = pe.pawnScore[sideToMove] - pe.pawnScore[~sideToMove];
    Score kingShelter  = pe.shelterScore[sideToMove] - pe.shelterScore[~sideToMove];
    Score pieceActivity = sideToMove == WHITE ? evaluatePieceActivity<WHITE>(board) - evaluatePieceActivity<BLACK>(board) :
                                                evaluatePieceActivity<BLACK>(board) - evaluatePieceActivity<WHITE>(board);


    int mgPhase = gamePhase > 24 ? 24 : gamePhase;
    int egPhase = 24 - mgPhase;
    return ((matScore.mg + psqtScore.mg + pawnStruct.mg + kingShelter.mg + pieceActivity.mg + W.tempo.mg) * mgPhase +
            (matScore.eg + psqtScore.eg + pawnStruct.eg + kingShelter.eg + pieceActivity.eg + W.tempo.eg) * egPhase) / 24;
}

template<Colour col>
Score evaluatePawnStructure(Board& board) {
    Score score = {0, 0};
    Direction up = col == WHITE ? NORTH : SOUTH;
    Bitboard ourPawns = board.pieces(col, PAWN);
    Bitboard theirPawns = board.pieces(~col, PAWN);

    Bitboard b = ourPawns;

    while (b) {
        Square idx = pop_lsb(b);
        int file = idx & 7;
        int rank = idx / 8;

        Bitboard neighbors = neighborFiles[file] & ourPawns;
        Bitboard support   = neighbors & rankBB[rank - (up / 8)];
        Bitboard adj       = neighbors & rankBB[rank];
        Bitboard ahead     = ourPawns & aheadMask[col][idx];

        Bitboard forwardMask = aheadMask[col][idx];
        Bitboard opposing = theirPawns & (forwardMask
                                       | shift<WEST>(forwardMask)
                                       | shift<EAST>(forwardMask));

        const bool hasNeighbors = neighbors;
        const bool hasSupport   = support;
        const bool hasAdj       = adj;

        if (hasSupport || hasAdj) {
            accum(score, W.connectedPawn[relativeRank(col, rank)], 1);
            accum(score, W.supportedPawn, popcount(support));
        } else if (!hasNeighbors) {
            accum(score, W.isolatedPawn, 1);
        }

        if (!ahead && !opposing) {
            accum(score, W.passedPawn[relativeRank(col, rank)], 1);
        } else if (ahead) {
            accum(score, W.doubledPawn, 1);
        }

        if (hasNeighbors && !hasSupport) {
            accum(score, W.backwardPawn, 1);
        }

        if (!hasSupport && !hasAdj) {
            accum(score, W.hangingPawn, 1);
        }
    }
    return score;
}

template<Colour col>
Score evaluateKingShelter(Board& board) {
    Square ksq = board.kingSquare(col);
    int kingFile = ksq & 7;
    int kingRank = ksq / 8;
    Bitboard friendlyPawns = board.pieces(col, PAWN);
    Bitboard enemyPawns = board.pieces(~col, PAWN);

    Score score = {0, 0};

    for (int f = std::max(kingFile - 1, 0); f <= std::min(kingFile + 1, 7); f++) {
        Bitboard immediate = friendlyPawns & aheadMask[col][kingRank * 8 + f];
        int ourRank = immediate ? relativeRank(col, getRearMost(col, immediate) / 8) : 0;
        int distToSide = std::min(f, 7-f);
        accum(score, W.shelter[distToSide][ourRank], 1);

        immediate = enemyPawns & aheadMask[col][kingRank * 8 + f];
        int theirRank = immediate ? relativeRank(col, getFrontMost(~col, immediate) / 8) : 0;
        int rankDist = theirRank ? theirRank - relativeRank(col, kingRank) : 0;

        if (ourRank != 0 && ourRank == theirRank - 1) {
            accum(score, W.blockedStorm[rankDist], 1);
        } else {
            accum(score, W.unblockedStorm[rankDist], 1);
        }
    }

    accum(score, W.kingOpenFile[board.onOpenFile(col, ksq)][board.onOpenFile(~col, ksq)], 1);

    return score;
}

template<Colour col>
Score evaluatePieceActivity(Board& board) {
    constexpr Direction upRight = ~col == WHITE ? NORTHEAST : SOUTHWEST;
    constexpr Direction upLeft = ~col == WHITE ? NORTHWEST : SOUTHEAST;
    Square theirKing = board.kingSquare(~col);

    Bitboard pwnAttacks = shift<upRight>(board.pieces(~col, PAWN)) | shift<upLeft>(board.pieces(~col, PAWN));
    Bitboard kingZone = kingAttacks[theirKing] | bit(theirKing);
    Bitboard open = ~board.pieces(col) & ~pwnAttacks;
    Bitboard occ = board.pieces();

    Bitboard bb = board.pieces(col, KNIGHT);
    Bitboard attacks;
    int weightedAttackCount = 0;
    Score score = {0, 0};

    while (bb) {
        attacks = genAttacksBB<KNIGHT>(pop_lsb(bb), occ);
        accum(score, W.knightMobility[popcount(attacks & open)], 1);
        weightedAttackCount += popcount(attacks & kingZone) * W.attackerWeight[KNIGHT];
    }

    bb = board.pieces(col, BISHOP);
    if (popcount(bb) >= 2) {
        accum(score, W.bishopPair, 1);
    }
    while (bb) {
        attacks = genAttacksBB<BISHOP>(pop_lsb(bb), occ);
        accum(score, W.bishopMobility[popcount(attacks & open)], 1);
        weightedAttackCount += popcount(attacks & kingZone) * W.attackerWeight[BISHOP];
    }

    bb = board.pieces(col, ROOK);
    while (bb) {
        Square sq = pop_lsb(bb);
        attacks = genAttacksBB<ROOK>(sq, occ);
        accum(score, W.rookMobility[popcount(attacks & open)], 1);
        accum(score, W.rookOpenFile[board.onOpenFile(col, sq)][board.onOpenFile(~col, sq)], 1);
        weightedAttackCount += popcount(attacks & kingZone) * W.attackerWeight[ROOK];
    }

    bb = board.pieces(col, QUEEN);
    while (bb) {
        attacks = genAttacksBB<QUEEN>(pop_lsb(bb), occ);
        accum(score, W.queenMobility[popcount(attacks & open)], 1);
        weightedAttackCount += popcount(attacks & kingZone) * W.attackerWeight[QUEEN];
    }

    // attackerWeight itself is never traced/gradient-tuned: it's used above
    // purely to select which safetyTable index fires, a discrete/nonlinear
    // role incompatible with the linear-in-weights gradient this tuner
    // assumes. safetyTable's selected entry is a normal linear term.
    accum(score, W.safetyTable[weightedAttackCount], 1);
    return score;
}

}