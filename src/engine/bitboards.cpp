#include "engine/bitboards.h"
#include <stdexcept>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

namespace engine {

Magic rookMagics[SQUARECOUNT];
Magic bishopMagics[SQUARECOUNT];

Bitboard pawnAttacks[COLOURNB][SQUARECOUNT];
Bitboard knightAttacks[SQUARECOUNT];
Bitboard kingAttacks[SQUARECOUNT];
Bitboard rookAttackTable[ROOK_ATTACK_TABLE_SIZE];
Bitboard bishopAttackTable[BISHOP_ATTACK_TABLE_SIZE];

Bitboard betweenBB[SQUARECOUNT][SQUARECOUNT];
Bitboard lineBB[SQUARECOUNT][SQUARECOUNT];
Bitboard castleMasks[BlackQueenSide + 1];
Bitboard aheadMask[COLOURNB][SQUARECOUNT];

namespace bb {

void init(){
    for (int i = 0; i < SQUARECOUNT; i++) {
        pawnAttacks[WHITE][i] = computePawnAttacks(static_cast<Square>(i), WHITE);
        pawnAttacks[BLACK][i] = computePawnAttacks(static_cast<Square>(i), BLACK);
        knightAttacks[i] = computeKnightAttacks(static_cast<Square>(i));
        kingAttacks[i] = computeKingAttacks(static_cast<Square>(i));
        aheadMask[WHITE][i] = aheadFileMask(static_cast<Square>(i), WHITE);
        aheadMask[BLACK][i] = aheadFileMask(static_cast<Square>(i), BLACK);
    }
    initMagicBitboards();
    initBetweenBBAndLineBB();
    castleMasks[WhiteKingSide] = (Bitboard)0x60;
    castleMasks[WhiteQueenSide] = (Bitboard)0xe;
    castleMasks[BlackKingSide] = castleMasks[WhiteKingSide] << 56;
    castleMasks[BlackQueenSide] = castleMasks[WhiteQueenSide] << 56;
}

void initBetweenBBAndLineBB() {
    for (int sq1 = 0; sq1 < SQUARECOUNT; ++sq1) {
        Square s1 = static_cast<Square>(sq1);
        Bitboard s1BB = bit(s1);

        Bitboard rookRays   = genAttacksBB<ROOK>(s1, 0);
        Bitboard bishopRays = genAttacksBB<BISHOP>(s1, 0);

        for (int sq2 = 0; sq2 < SQUARECOUNT; ++sq2) {
            Square s2 = static_cast<Square>(sq2);
            Bitboard s2BB = bit(s2);

            Bitboard between = 0ULL;
            Bitboard line = 0ULL;

            if (rookRays & s2BB) {
                between = genAttacksBB<ROOK>(s1, s2BB) &
                          genAttacksBB<ROOK>(s2, s1BB);
                line = (rookRays & genAttacksBB<ROOK>(s2, 0)) | s1BB | s2BB;
            } else if (bishopRays & s2BB) {
                between = genAttacksBB<BISHOP>(s1, s2BB) &
                          genAttacksBB<BISHOP>(s2, s1BB);
                line = (bishopRays & genAttacksBB<BISHOP>(s2, 0)) | s1BB | s2BB;
            }

            between |= s2BB;
            betweenBB[sq1][sq2] = between;
            lineBB[sq1][sq2] = line;
        }
    }
}

inline Bitboard aheadFileMask(Square sq, Colour col) {
    const int f = sq & 7;
    const int r = sq >> 3;
    Bitboard file = fileBB[f];
    if (col == WHITE) {
        return file << (8 * (r + 1));
    } else {
        return file >> (8 * (8 - r));
    }
}

template<Direction Dir>
Bitboard slidingAttacksInDir(Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    Bitboard ray = shift<Dir>(bit(sq));

    while (ray) {
        attacks |= ray;
        if (ray & occ) break;
        ray = shift<Dir>(ray);
    }
    return attacks;
}

template<Direction... Dirs>
Bitboard computeSlidingAttacks(Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    ((attacks |= slidingAttacksInDir<Dirs>(sq, occ)), ...);
    return attacks;
}

template<Direction... Dirs>
constexpr Bitboard stepInDirections(Bitboard bb) {
    Bitboard attacks = 0;
    ((attacks |= shift<Dirs>(bb)), ...);
    return attacks;
}

Bitboard computePawnAttacks(Square sq, Colour col){
    Bitboard bb = bit(sq);
    if (col == WHITE) {
        return stepInDirections<NORTHEAST, NORTHWEST>(bb);
    } else {
        return stepInDirections<SOUTHEAST, SOUTHWEST>(bb);
    }
}

Bitboard computeRookAttacks(Square sq, Bitboard occ){
    return computeSlidingAttacks<NORTH, SOUTH, EAST, WEST>(sq, occ);
}

Bitboard computeBishopAttacks(Square sq, Bitboard occ) {
    return computeSlidingAttacks<NORTHWEST, NORTHEAST, SOUTHEAST, SOUTHWEST>(sq, occ);
}

Bitboard computeKingAttacks(Square sq) {
    return stepInDirections<
        NORTH, SOUTH, EAST, WEST,
        NORTHEAST, NORTHWEST, SOUTHEAST, SOUTHWEST>
        (bit(sq));
}

Bitboard computeKnightAttacks(Square sq) {
    Bitboard bb = bit(sq);

    return
        shift<NORTH>(shift<NORTHEAST>(bb)) |  // N + NE = NNE
        shift<NORTH>(shift<NORTHWEST>(bb)) |  // N + NW = NNW

        shift<SOUTH>(shift<SOUTHEAST>(bb)) |  // S + SE = SSE
        shift<SOUTH>(shift<SOUTHWEST>(bb)) |  // S + SW = SSW

        shift<EAST>(shift<NORTHEAST>(bb)) |   // E + NE = ENE
        shift<EAST>(shift<SOUTHEAST>(bb)) |   // E + SE = ESE

        shift<WEST>(shift<NORTHWEST>(bb)) |   // W + NW = WNW
        shift<WEST>(shift<SOUTHWEST>(bb));    // W + SW = WSW
}

Bitboard rookMask(Square sq) {
    Bitboard mask = 0;
    Bitboard bb = bit(sq);

    Bitboard n = shift<NORTH>(bb);
    while (n & ~Rank8BB) {
        mask |= n;
        n = shift<NORTH>(n);
    }

    Bitboard s = shift<SOUTH>(bb);
    while (s & ~Rank1BB) {
        mask |= s;
        s = shift<SOUTH>(s);
    }

    Bitboard e = shift<EAST>(bb);
    while (e & ~FileHBB) {
        mask |= e;
        e = shift<EAST>(e);
    }

    Bitboard w = shift<WEST>(bb);
    while (w & ~FileABB) {
        mask |= w;
        w = shift<WEST>(w);
    }

    return mask;
}

Bitboard bishopMask(Square sq) {
    Bitboard mask = 0;
    Bitboard bb = bit(sq);

    Bitboard ne = shift<NORTHEAST>(bb);
    while (ne && (ne & ~Rank8BB) && (ne & ~FileHBB)) {
        mask |= ne;
        ne = shift<NORTHEAST>(ne);
    }

    Bitboard nw = shift<NORTHWEST>(bb);
    while (nw && (nw & ~Rank8BB) && (nw & ~FileABB)) {
        mask |= nw;
        nw = shift<NORTHWEST>(nw);
    }

    Bitboard se = shift<SOUTHEAST>(bb);
    while (se && (se & ~Rank1BB) && (se & ~FileHBB)) {
        mask |= se;
        se = shift<SOUTHEAST>(se);
    }

    Bitboard sw = shift<SOUTHWEST>(bb);
    while (sw && (sw & ~Rank1BB) && (sw & ~FileABB)) {
        mask |= sw;
        sw = shift<SOUTHWEST>(sw);
    }

    return mask;
}

Bitboard indexToOccupancy(int index, Bitboard mask) {
    Bitboard occ = 0;
    int bitIndex = 0;

    while (mask) {
        Bitboard lsbBB = mask & -mask;
        if (index & (1 << bitIndex)) {
            occ |= lsbBB;
        }
        mask ^= lsbBB;
        ++bitIndex;
    }

    return occ;
}

static std::filesystem::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, len)).parent_path();
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::string(buf, static_cast<size_t>(len))).parent_path();
#endif
}

// Locates the "data" directory by walking up from the executable's own location,
// since the CWD at launch depends on the build layout / how the GUI spawns the engine.
static std::filesystem::path findDataDir() {
    std::filesystem::path dir = executableDir();
    for (int i = 0; i < 6; ++i) {
        if (std::filesystem::exists(dir / "data" / "rook_magics.txt")) {
            return dir / "data";
        }
        std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    throw std::runtime_error("Could not locate 'data' directory near the executable");
}

static void loadMagicNumbers(const char* path, Bitboard out[SQUARECOUNT]) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(std::string("Failed to open magic file: ") + path);
    }
    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        std::uint64_t val;
        in >> std::hex >> val;
        if (!in) {
            throw std::runtime_error("Failed to read magic for square " + std::to_string(sq));
        }
        out[sq] = static_cast<Bitboard>(val);
    }
}

template<typename AttackFunc>
static void buildMagicForSquare(
    Magic& m,
    Bitboard magicNumber,
    Bitboard occMask,
    int offset,
    Bitboard* globalTable,
    AttackFunc attackFunc
) {
    int relevantBits = popcount(occMask);
    int entries = 1 << relevantBits;
    int shift = 64 - relevantBits;

    m.magic = magicNumber;
    m.occMask = occMask;
    m.shift = shift;
    m.attacks = globalTable + offset;

    std::vector<Bitboard> used(entries, 0ULL);
    std::vector<bool> filled(entries, false);

    for (int i = 0; i < entries; ++i) {
        Bitboard occ = indexToOccupancy(i, occMask);
        Bitboard attack = attackFunc(occ);
        Bitboard key = (occ * magicNumber) >> shift;
        int idx = static_cast<int>(key);

        if (!filled[idx]) {
            filled[idx] = true;
            used[idx] = attack;
        } else {
            if (used[idx] != attack) {
                throw std::runtime_error("Magic collision with different attacks at runtime!");
            }
        }
    }

    for (int i = 0; i < entries; ++i) {
        globalTable[offset + i] = used[i];
    }
}

void initMagicBitboards() {
    Bitboard rookMagicNumbers[SQUARECOUNT];
    Bitboard bishopMagicNumbers[SQUARECOUNT];

    std::filesystem::path dataDir = findDataDir();
    loadMagicNumbers((dataDir / "rook_magics.txt").string().c_str(),  rookMagicNumbers);
    loadMagicNumbers((dataDir / "bishop_magics.txt").string().c_str(), bishopMagicNumbers);

    Bitboard rookMasks[SQUARECOUNT];
    Bitboard bishopMasks[SQUARECOUNT];
    int rookBits[SQUARECOUNT];
    int bishopBits[SQUARECOUNT];

    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        rookMasks[sq]   = rookMask(static_cast<Square>(sq));
        bishopMasks[sq] = bishopMask(static_cast<Square>(sq));
        rookBits[sq]    = popcount(rookMasks[sq]);
        bishopBits[sq]  = popcount(bishopMasks[sq]);
    }

    int rookOffset = 0;
    int bishopOffset = 0;

    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        Square s = static_cast<Square>(sq);

        int rEntries = 1 << rookBits[sq];
        buildMagicForSquare(
            rookMagics[sq],
            rookMagicNumbers[sq],
            rookMasks[sq],
            rookOffset,
            rookAttackTable,
            [s](Bitboard occ) { return computeRookAttacks(s, occ); }
        );
        rookOffset += rEntries;

        int bEntries = 1 << bishopBits[sq];
        buildMagicForSquare(
            bishopMagics[sq],
            bishopMagicNumbers[sq],
            bishopMasks[sq],
            bishopOffset,
            bishopAttackTable,
            [s](Bitboard occ) { return computeBishopAttacks(s, occ); }
        );
        bishopOffset += bEntries;
    }

    if (rookOffset != ROOK_ATTACK_TABLE_SIZE) {
        throw std::runtime_error("ROOK_ATTACK_TABLE_SIZE mismatch");
    }
    if (bishopOffset != BISHOP_ATTACK_TABLE_SIZE) {
        throw std::runtime_error("BISHOP_ATTACK_TABLE_SIZE mismatch");
    }
}

}
}