#include "engine/movegen.h"

namespace engine {

template<GenType Type, Direction Dir, bool Capt>
ScoredMove* generatePromotions(ScoredMove* moveList, Square to) {
    constexpr bool allPromos = (Type == GEN_EVASIONS) || (Type == GEN_PSEUDO_LEGAL);

    if constexpr (Type == GEN_CAPTURES || allPromos) {
        *moveList++ = ScoredMove{ makePromoMove(to - Dir, to, QUEEN) };
    }

    if constexpr ((Type == GEN_CAPTURES && Capt) || (Type == GEN_QUIETS && !Capt) || allPromos) {
        *moveList++ = ScoredMove{ makePromoMove(to - Dir, to, ROOK) };
        *moveList++ = ScoredMove{ makePromoMove(to - Dir, to, BISHOP) };
        *moveList++ = ScoredMove{ makePromoMove(to - Dir, to, KNIGHT) };
    }

    return moveList;
}


template<Colour Col, GenType Type>
ScoredMove* generatePawnMoves(const Board& board, ScoredMove* moveList, Bitboard targets) {
    constexpr Direction up = Col == WHITE ? NORTH : SOUTH;
    constexpr Direction upRight = Col == WHITE ? NORTHEAST : SOUTHWEST;
    constexpr Direction upLeft = Col == WHITE ? NORTHWEST : SOUTHEAST;
    constexpr Bitboard relativeRank7 = Col == WHITE ? Rank7BB : Rank2BB;
    constexpr Bitboard relativeRank3 = Col == WHITE ? Rank3BB : Rank6BB;

    Bitboard emptySquares = ~board.pieces();
    Bitboard enemies = Type == GEN_EVASIONS ? board.checkers() : board.pieces(~Col);

    Bitboard pawnsOn7 = board.pieces(Col, PAWN) & relativeRank7;
    Bitboard pawnsNotOn7 = board.pieces(Col, PAWN) & ~relativeRank7;

    // Pawn Pushes
    if (Type != GEN_CAPTURES) {
        Bitboard singlePush = shift<up>(pawnsNotOn7) & emptySquares;
        Bitboard doublePush = shift<up>(singlePush & relativeRank3) & emptySquares;
    
        if (Type == GEN_EVASIONS) {
            singlePush &= targets;
            doublePush &= targets;
        }

        while (singlePush) {
            Square to = pop_lsb(singlePush);
            *moveList++ = ScoredMove{ makeMoveBasic(to - up, to) };
        }

        while (doublePush) {
            Square to = pop_lsb(doublePush);
            *moveList++ = ScoredMove{ makeMoveBasic(to - up - up, to) };
        }
    }

    // Pawn Promos
    if (pawnsOn7) {
        Bitboard captLeftProm = shift<upLeft>(pawnsOn7) & enemies;
        Bitboard captRightProm = shift<upRight>(pawnsOn7) & enemies;
        Bitboard pushProm = shift<up>(pawnsOn7) & emptySquares;

        if (Type == GEN_EVASIONS) {
            pushProm &= targets;
        }

        while(captLeftProm) {
            moveList = generatePromotions<Type, upLeft, true>(moveList, pop_lsb(captLeftProm));
        }

        while(captRightProm) {
            moveList = generatePromotions<Type, upRight, true>(moveList, pop_lsb(captRightProm));
        }

        while(pushProm) {
            moveList = generatePromotions<Type, up, false>(moveList, pop_lsb(pushProm));
        }

    }

    // Pawn Captures
    if (Type != GEN_QUIETS) {
        Bitboard captRight = shift<upRight>(pawnsNotOn7) & enemies;
        Bitboard captLeft = shift<upLeft>(pawnsNotOn7) & enemies;

        while (captRight) {
            Square to = pop_lsb(captRight);
            *moveList++ = ScoredMove{ makeMoveBasic(to - upRight, to) };
        }

        while (captLeft) {
            Square to = pop_lsb(captLeft);
            *moveList++ = ScoredMove{ makeMoveBasic(to - upLeft, to) };
        }

        if (board.epSquare() != NOSQUARE) {
            Bitboard enPassant = pawnAttacks[~Col][board.epSquare()] & pawnsNotOn7;

            while (enPassant) {
                *moveList++ = ScoredMove{ makeMoveWithFlag(pop_lsb(enPassant), board.epSquare(), ENPASSANT) };
            }
        }
    }

    return moveList;
}

template<Colour Col, PieceType PT>
ScoredMove* generateMovesByPT(const Board& board, ScoredMove* moveList, Bitboard targets) {
    Square from, to;
    Bitboard pcs = board.pieces(Col, PT);
    Bitboard occ = board.pieces();
    Bitboard attacks;

    while (pcs) {
        from = pop_lsb(pcs);
        attacks = genAttacksBB<PT>(from, occ) & targets;

        while (attacks) {
            to = pop_lsb(attacks);
            *moveList++ = ScoredMove{ makeMoveBasic(from, to) };
        }
    }
    return moveList;
}

template<Colour Col, GenType Type>
ScoredMove* generateAll(const Board& board, ScoredMove* moveList) {
    Bitboard targets;

    if (Type != GEN_EVASIONS || !moreThanOne(board.checkers())) {
        targets = Type == GEN_CAPTURES ? board.pieces(~Col) :
                  Type == GEN_QUIETS ? ~board.pieces() :
                  Type == GEN_EVASIONS ? betweenBB[board.kingSquare(Col)][lsb(board.checkers())] :
                  ~board.pieces(Col);

        moveList = generatePawnMoves<Col, Type>(board, moveList, targets);
        moveList = generateMovesByPT<Col, KNIGHT>(board, moveList, targets);
        moveList = generateMovesByPT<Col, BISHOP>(board, moveList, targets);
        moveList = generateMovesByPT<Col, ROOK>(board, moveList, targets);
        moveList = generateMovesByPT<Col, QUEEN>(board, moveList, targets);
    }

    Bitboard kingTargets = Type == GEN_EVASIONS ? ~board.pieces(Col) : targets;
    moveList = generateMovesByPT<Col, KING>(board, moveList, kingTargets);

    CastlingRight kingSide = Col == WHITE ? WhiteKingSide : BlackKingSide;
    CastlingRight queenSide = Col == WHITE ? WhiteQueenSide : BlackQueenSide;

    if ((Type == GEN_QUIETS || Type == GEN_PSEUDO_LEGAL) && board.canCastle(kingSide | queenSide)) {
        Square ksq = board.kingSquare(Col);
        if (board.canCastle(kingSide) && !board.castlingBlocked(kingSide)) {
            *moveList++ = ScoredMove{ makeMoveWithFlag(ksq, static_cast<Square>(ksq + 2), CASTLE) };
        }
        if (board.canCastle(queenSide) && !board.castlingBlocked(queenSide)) {
            *moveList++ = ScoredMove{ makeMoveWithFlag(ksq, static_cast<Square>(ksq - 2), CASTLE) };
        }
    }

    return moveList;
}

template<GenType Type>
ScoredMove* generate(const Board& board, ScoredMove* moveList) {
    Colour colToMove = board.sideToMove();
    return colToMove == WHITE ? generateAll<WHITE, Type>(board, moveList) : generateAll<BLACK, Type>(board, moveList);
}

template ScoredMove* generate<GEN_CAPTURES>(const Board&, ScoredMove*);
template ScoredMove* generate<GEN_QUIETS>(const Board&, ScoredMove*);
template ScoredMove* generate<GEN_EVASIONS>(const Board&, ScoredMove*);
template ScoredMove* generate<GEN_PSEUDO_LEGAL>(const Board&, ScoredMove*);

template<>
ScoredMove* generate<GEN_LEGAL>(const Board& board, ScoredMove* moveList) {
    ScoredMove* curr = moveList;
    moveList = board.checkers() ? generate<GEN_EVASIONS>(board, moveList) : generate<GEN_PSEUDO_LEGAL>(board, moveList);

    while (curr != moveList) {
        if (!board.legalMove(curr->move)) {
            *curr = *(--moveList);
        } else {
            curr++;
        }
    }

    return moveList;
}

}