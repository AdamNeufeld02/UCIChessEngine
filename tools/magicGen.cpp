#include <iostream>
#include <vector>
#include <random>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include "engine/bitboards.h"

namespace engine{
namespace bb {

static std::mt19937_64 rng(0x123456789ABCDEFULL);

static Bitboard random_u64() {
    return rng();
}

static Bitboard random_magic_candidate() {
    return random_u64() & random_u64() & random_u64();
}

template<typename AttackFunc>
static Bitboard findMagicForSquare(Bitboard occMask, AttackFunc attackFunc) {
    int relevantBits = popcount(occMask);
    int entries = 1 << relevantBits;
    int shift = 64 - relevantBits;

    std::vector<Bitboard> occs(entries);
    std::vector<Bitboard> attacks(entries);

    for (int i = 0; i < entries; i++) {
        Bitboard occ = indexToOccupancy(i, occMask);
        occs[i] = occ;
        attacks[i] = attackFunc(occ);
    }

    while (true) {
        Bitboard magic = random_magic_candidate();

        if (popcount((occMask * magic) & 0xFF00000000000000ULL) < 6) {
            continue;
        }

        std::vector<Bitboard> used(entries, 0ULL);
        std::vector<bool> filled(entries, false);
        bool fail = false;

        for (int i = 0; i < entries && !fail; ++i) {
            Bitboard key = (occs[i] * magic) >> shift;
            int idx = static_cast<int>(key);
            if (!filled[idx]) {
                filled[idx] = true;
                used[idx] = attacks[i];
            } else {
                if (used[idx] != attacks[i]) {
                    fail = true;
                }
            }
        }

        if (!fail) {
            return magic;
        }
    }
}

static std::ofstream open_data_file_trunc(const std::string& filename) {
    namespace fs = std::filesystem;

    fs::path buildDir = fs::current_path();
    fs::path dataDir  = buildDir.parent_path() / "data";

    std::error_code ec;
    fs::create_directories(dataDir, ec);

    fs::path fullPath = dataDir / filename;

    std::ofstream out(fullPath, std::ios::out | std::ios::trunc);
    if (!out) {
        std::cerr << "Failed to open " << fullPath.string() << " for writing\n";
    } else {
        std::cerr << "Writing magics to " << fullPath.string() << "\n";
    }

    return out;
}

int runMagicBBGen() {
    std::cout << "Running Magic Bitboard Generation" << "\n";
    Bitboard rookMagics[SQUARECOUNT];
    Bitboard bishopMagics[SQUARECOUNT];

    for (int i = 0; i < SQUARECOUNT; i++) {
        Square s = static_cast<Square>(i);
        Bitboard mask = rookMask(s);

        rookMagics[i] = findMagicForSquare(
            mask,
            [s](Bitboard occ) {
                return computeRookAttacks(s, occ);
            }
        );

        std::cerr << "Rook square " << i << " magic: 0x"
                  << std::hex << rookMagics[i] << std::dec << "\n";
    }

    for (int i = 0; i < SQUARECOUNT; i++) {
        Square s = static_cast<Square>(i);
        Bitboard mask = bishopMask(s);

        bishopMagics[i] = findMagicForSquare(
            mask,
            [s](Bitboard occ) {
                return computeBishopAttacks(s, occ);
            }
        );

        std::cerr << "Bishop square " << i << " magic: 0x"
                  << std::hex << bishopMagics[i] << std::dec << "\n";
    }

     {
        std::ofstream out = open_data_file_trunc("rook_magics.txt");
        if (!out) {
            return 1;
        }
        out << std::hex;
        for (int sq = 0; sq < SQUARECOUNT; ++sq) {
            out << "0x" << rookMagics[sq] << "\n";
        }
    }

    {
        std::ofstream out = open_data_file_trunc("bishop_magics.txt");
        if (!out) {
            return 1;
        }
        out << std::hex;
        for (int sq = 0; sq < SQUARECOUNT; ++sq) {
            out << "0x" << bishopMagics[sq] << "\n";
        }
    }

    std::cout << "Magic generation complete.\n";
    return 0;
}
}
}

namespace magic_gen_cli {
    int run(int argc, char** argv) {
        (void)argc;
        (void)argv;
        return engine::bb::runMagicBBGen();
    }
}