#include <string>
#include "engine/board.h"
#include "engine/movegen.h"
#include <iostream>

namespace engine{

Board::Board() {
    initFields();
}

void Board::initFields() {
    for (int i = 0; i < PIECECOUNT; i++) {
        pieceBB[i] = (Bitboard)0ULL;
    }
    for (int i = 0; i < SQUARECOUNT; i++) {
        board[i] = EMPTY;
    }
    colourBB[WHITE] = (Bitboard)0ULL;
    colourBB[BLACK] = (Bitboard)0ULL;
    allPieces = (Bitboard)0ULL;
    colToMove = WHITE;
    st = nullptr;

    for (int j = 0; j < COLOURNB; j++) {
        material[j] = { 0, 0 };
        psqtv[j] = { 0, 0 };
    }
}

void Board::initRootState(State* rootState) {
    rootState->boardKey = (ZobristKey)0ULL;
    rootState->castlingRights = CastlingRightsNone;
    rootState->captured = EMPTY;
    rootState->checkers = 0ULL;
    rootState->blockersForKing[WHITE] = 0ULL;
    rootState->blockersForKing[BLACK] = 0ULL;
    rootState->pinners[WHITE] = 0ULL;
    rootState->pinners[BLACK] = 0ULL;
    rootState->checkSquares[PAWN] = 0ULL;
    rootState->checkSquares[KNIGHT] = 0ULL;
    rootState->checkSquares[BISHOP] = 0ULL;
    rootState->checkSquares[ROOK] = 0ULL;
    rootState->checkSquares[QUEEN] = 0ULL;
    rootState->checkSquares[KING] = 0ULL;
    rootState->epSquare = NOSQUARE;
    rootState->halfmoveClock = 0;
    rootState->ply = 0;
    rootState->fullmoveNumber = 1;
    rootState->movesFromNull = 0;
    rootState->repetitionCount = 0;
    rootState->previous = nullptr;
}

void Board::fenToBoard(std::string fenString, State* rootState) {
    initFields();
    initRootState(rootState);

    std::istringstream iss(fenString);

    std::string placement, sideStr, castlingStr, epStr;

    std::string halfmoveStr = "0";
    std::string fullmoveStr = "1";

    if (!(iss >> placement >> sideStr >> castlingStr >> epStr)) {
        throw std::runtime_error("Invalid FEN: missing required fields");
    }

    // 3) Piece placement
    int rank = 7;
    int file = 0;

    for (char ch : placement) {
        if (ch == '/') {
            rank--;
            file = 0;
            continue;
        }

        if (ch >= '1' && ch <= '8') {
            file += ch - '0';
        } else {
            if (file >= 8 || rank < 0) {
                throw std::runtime_error("Invalid FEN: too many pieces in rank");
            }
            Square sq = squareFromFileRank(file, rank);
            Piece pc = pieceFromFenChar(ch);
            putPiece(pc, sq);
            file++;
        }
    }

    // 4) Side to move
    if (sideStr == "w") {
        colToMove = WHITE;
    } else if (sideStr == "b") {
        colToMove = BLACK;
    } else {
        throw std::runtime_error("Invalid FEN: side to move");
    }

    // 5) Castling rights
    rootState->castlingRights = CastlingRightsNone;
    if (castlingStr != "-") {
        for (char c : castlingStr) {
            switch (c) {
            case 'K': rootState->castlingRights = static_cast<CastlingRight>(rootState->castlingRights | WhiteKingSide);  break;
            case 'Q': rootState->castlingRights = static_cast<CastlingRight>(rootState->castlingRights | WhiteQueenSide); break;
            case 'k': rootState->castlingRights = static_cast<CastlingRight>(rootState->castlingRights | BlackKingSide);  break;
            case 'q': rootState->castlingRights = static_cast<CastlingRight>(rootState->castlingRights | BlackQueenSide); break;
            default:
                throw std::runtime_error("Invalid FEN: castling rights");
            }
        }
    }

    // 6) En passant square
    if (epStr == "-") {
        rootState->epSquare = NOSQUARE;
    } else {
        if (epStr.size() != 2)
            throw std::runtime_error("Invalid FEN: ep square");

        int file = fileFromChar(epStr[0]);
        int rank = rankFromChar(epStr[1]);
        if (file < 0 || file > 7 || rank < 0 || rank > 7)
            throw std::runtime_error("Invalid FEN: ep square coords");
        Square candidateEp = squareFromFileRank(file, rank);

        // As in makeMove: only treat this as a real ep square if a capture is
        // actually available, so hashing/repetition stays consistent with positions
        // reached via makeMove rather than FEN.
        rootState->epSquare = (pawnAttacks[~colToMove][candidateEp] & pieces(colToMove, PAWN))
            ? candidateEp : NOSQUARE;
    }

    rootState->captured = EMPTY;
    iss >> halfmoveStr;
    iss >> fullmoveStr;
    rootState->halfmoveClock = std::stoi(halfmoveStr);
    rootState->ply = rootState->halfmoveClock;
    rootState->fullmoveNumber = std::stoi(fullmoveStr);

    rootState->previous = nullptr;
    st = rootState;

    updateChecksAndPins(colToMove);
    calcZobristHashFromScratch();
}

void Board::makeMove(Move move, State* newState) {
    Flags flag = moveFlag(move);
    Square from = fromSq(move);
    Square to = toSq(move);
    
    Colour us = colToMove;
    Colour them = ~us;

    Piece moved = pieceOn(from);
    Piece capt = flag == ENPASSANT? makePiece(them, PAWN) : pieceOn(to);

    memcpy(newState, st, offsetof(State, captured));

    newState->captured = capt;
    newState->halfmoveClock += 1;
    newState->movesFromNull += 1;
    newState->ply += 1;
    if (us == BLACK) {
        newState->fullmoveNumber += 1;
    }

    if (capt) {
        int captSq = to;
        if (flag == ENPASSANT) {
            captSq = us == WHITE? captSq - 8 : captSq + 8;
        }
        newState->boardKey ^= zobrist::psq[capt][captSq];
        removePiece(static_cast<Square>(captSq));
        newState->halfmoveClock = 0;
    }
    if (flag == PROMOTION) {
        newState->boardKey ^= zobrist::psq[pieceOn(from)][from];
        removePiece(from);
        putPiece(makePiece(us, promoPiece(move)), from);
        newState->boardKey ^= zobrist::psq[pieceOn(from)][from];
    }
    if (flag == CASTLE) {
        Square rookOrigin;
        Square rookDest;
        if (to > from) {
            rookOrigin = static_cast<Square>(to + 1);
            rookDest = static_cast<Square>(to - 1);
        } else {
            rookOrigin = static_cast<Square>(to - 2);
            rookDest = static_cast<Square>(to + 1);
        }
        newState->boardKey ^= zobrist::psq[pieceOn(rookOrigin)][rookOrigin];
        movePiece(rookOrigin, rookDest);
        newState->boardKey ^= zobrist::psq[pieceOn(rookDest)][rookDest];
    }

    if (newState->epSquare != NOSQUARE) {
        newState->boardKey ^= zobrist::epFile[newState->epSquare & 7];
    }
    newState->epSquare = NOSQUARE;

    if (typeOf(moved) == PAWN) {
        newState->halfmoveClock = 0;
        if ((to ^ from) == 16) {
            Square candidateEp = us == WHITE ? static_cast<Square>(from + 8) : static_cast<Square>(from - 8);
            // Only counts as a "real" ep square (and only affects the hash / repetition
            // comparison) if the opponent actually has a pawn that can capture there.
            // Otherwise the position is indistinguishable from one with no ep square at
            // all, per the "possible moves are the same" repetition rule.
            if (pawnAttacks[us][candidateEp] & pieces(them, PAWN)) {
                newState->epSquare = candidateEp;
                newState->boardKey ^= zobrist::epFile[candidateEp & 7];
            }
        }
    }

    newState->boardKey ^= zobrist::psq[pieceOn(from)][from];
    movePiece(from, to);
    newState->boardKey ^= zobrist::psq[pieceOn(to)][to];

    colToMove = ~colToMove;
    newState->boardKey ^= zobrist::sideToMoveKey;
    newState->boardKey ^= zobrist::castlingKey[st->castlingRights];
    newState->castlingRights = newState->castlingRights & (castlingRights[from] & castlingRights[to]);
    newState->boardKey ^= zobrist::castlingKey[newState->castlingRights];

    newState->previous = st;
    st = newState;

    updateChecksAndPins(colToMove);

    st->repetitionCount = 0;
    int end = std::min(st->halfmoveClock, st->movesFromNull);
    if (end >= 4)
    {
        State* stp = st->previous->previous;
        for (int i = 4; i <= end; i += 2)
        {
            stp = stp->previous->previous;
            if (stp->boardKey == st->boardKey)
            {
                st->repetitionCount = stp->repetitionCount + 1;
                break;
            }
        }
    }
}

void Board::undoMove(Move move) {
    Flags flag = moveFlag(move);
    Square from = fromSq(move);
    Square to = toSq(move);

    colToMove = ~colToMove;
    Piece capt = st->captured;
    st = st->previous;

    movePiece(to, from);
    if (capt) {
        int captSq = to;
        if (flag == ENPASSANT) {
            captSq = colToMove == WHITE ? to - 8 : to + 8;
        }
        putPiece(capt, static_cast<Square>(captSq));
    }
    if (flag == PROMOTION) {
        removePiece(from);
        putPiece(makePiece(colToMove, PAWN), from);
    }
    if (flag == CASTLE) {
        if (to > from) {
            movePiece(static_cast<Square>(to - 1), static_cast<Square>(to + 1));
        } else {
            movePiece(static_cast<Square>(to + 1), static_cast<Square>(to - 2));
        }
    }
}

void Board::makeNullMove(State* newState) {
    memcpy(newState, st, offsetof(State, captured));
    newState->boardKey ^= zobrist::sideToMoveKey;
    newState->captured = EMPTY;
    if (newState->epSquare != NOSQUARE) {
        newState->boardKey ^= zobrist::epFile[newState->epSquare & 7];
        newState->epSquare = NOSQUARE;
    }
    colToMove = ~colToMove;
    newState->previous = st;
    newState->movesFromNull = 0;
    newState->repetitionCount = 0;
    newState->ply += 1;
    st = newState;
    updateChecksAndPins(colToMove);
}

void Board::undoNullMove() {
    colToMove = ~colToMove;
    st = st->previous;
}

void Board::calcZobristHashFromScratch() {
    ZobristKey key = 0ULL;

    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        Piece pc = board[sq];
        if (pc != EMPTY) {
            key ^= zobrist::psq[pc][sq];
        }
    }

    key ^= zobrist::castlingKey[static_cast<CastlingRight>(st->castlingRights)];

    if (st->epSquare != NOSQUARE) {
        int fileEp = static_cast<int>(st->epSquare) & 7;
        key ^= zobrist::epFile[fileEp];
    }

    if (colToMove == BLACK) {
        key ^= zobrist::sideToMoveKey;
    }

    st->boardKey = key;
}

Bitboard Board::getAttackers(Square sq, Colour col, Bitboard occ) const {
    Bitboard attackers = genAttacksBB<ROOK>(sq, occ) & (pieces(col, ROOK) | pieces(col, QUEEN));
    attackers |= genAttacksBB<BISHOP>(sq, occ) & (pieces(col, BISHOP) | pieces(col, QUEEN));
    attackers |= genAttacksBB<KNIGHT>(sq, occ) & pieces(col, KNIGHT);
    attackers |= genAttacksBB<KING>(sq, occ) & pieces(col, KING);
    attackers |= pawnAttacks[~col][sq] & pieces(col, PAWN);
    return attackers;
}

void Board::updateKingBlockers(Colour col) {
    Square ksq = kingSquare(col);
    Bitboard potentialThreats = (genAttacksBB<ROOK>(ksq, 0) & (pieces(~col, ROOK) | pieces(~col, QUEEN))) |
                                (genAttacksBB<BISHOP>(ksq, 0) & (pieces(~col, BISHOP) | pieces(~col, QUEEN)));

    Bitboard blockers = 0;
    Bitboard pinners = 0;
    Bitboard occupancy = allPieces ^ potentialThreats;

    while (potentialThreats) {
        Square threatSq = pop_lsb(potentialThreats);
        Bitboard pinned = betweenBB[ksq][threatSq] & occupancy;

        if (pinned && !moreThanOne(pinned)) {
            blockers |= pinned;
            if (pinned & pieces(col)) {
                pinners |= bit(threatSq);
            }
        }
    }

    st->blockersForKing[col] = blockers;
    st->pinners[~col] = pinners;
}

void Board::updateChecksAndPins(Colour col) {
    st->checkers = getAttackers(kingSquare(col), ~col, allPieces);
    updateKingBlockers(WHITE);
    updateKingBlockers(BLACK);

    Square ksq = kingSquare(~sideToMove());

    st->checkSquares[PAWN] = pawnAttacks[ksq][~sideToMove()];
    st->checkSquares[KNIGHT] = genAttacksBB<KNIGHT>(ksq, pieces());
    st->checkSquares[BISHOP] = genAttacksBB<BISHOP>(ksq, pieces());
    st->checkSquares[ROOK] = genAttacksBB<ROOK>(ksq, pieces());
    st->checkSquares[QUEEN] = st->checkSquares[BISHOP] | st->checkSquares[ROOK];
    st->checkSquares[KING] = 0;
}

bool Board::givesCheck(Move move) const {
    Square from = fromSq(move);
    Square to = toSq(move);
    Flags flag = moveFlag(move);

    // Straight check
    if (checkSquares(typeOf(pieceOn(from))) & bit(to)) return true;

    // discovered check
    if (st->blockersForKing[~sideToMove()] & bit(from)) {
        return (!(lineBB[from][to] & pieces(~sideToMove(), KING)) || (flag == CASTLE));
    }

    if (flag == NOFLAG) {
        return false;
    } else if (flag == ENPASSANT) {
        Square capSq = sideToMove() == WHITE ? to + SOUTH : to + NORTH;
        Bitboard occ = (pieces() ^ bit(from) ^ bit(capSq)) | bit(to);
        return (genAttacksBB<BISHOP>(kingSquare(~sideToMove()), occ) & (pieces(sideToMove(), BISHOP) | pieces(sideToMove(), QUEEN))) |
               (genAttacksBB<ROOK>(kingSquare(~sideToMove()), occ) & (pieces(sideToMove(), ROOK) | pieces(sideToMove(), QUEEN)));
    } else if (flag == PROMOTION) {
        return genAttacksBB(promoPiece(move), to, pieces() ^ bit(from) | bit(to)) & pieces(~sideToMove(), KING);
    } else {
        Square rookSq;
        if (to > from) {
            rookSq = static_cast<Square>(to - 1);
        } else {
            rookSq = static_cast<Square>(to + 1);
        }
        return checkSquares(ROOK) & bit(rookSq);
    }
}

bool Board::pseudoLegalMove(Move move) const {
    Colour us = colToMove;
    Square from = fromSq(move);
    Square to = toSq(move);
    Flags flag = moveFlag(move);
    Piece pc = board[from];

    if (flag != NOFLAG) {
        if (checkers()) {
            return MoveList<GEN_EVASIONS>(*this).contains(move);
        } else {
            return MoveList<GEN_PSEUDO_LEGAL>(*this).contains(move);
        }
    }

    if (pc == EMPTY || colourOf(pc) != us) return false;

    if (pieces(us) & bit(to)) return false;

    if (typeOf(pc) == PAWN) {
        if((Rank1BB | Rank8BB) & bit(to)) return false;

        Direction up = us == WHITE ? NORTH : SOUTH;
        Bitboard rank2BB = us == WHITE ? Rank2BB : Rank7BB;

        bool isCapture = bool(pawnAttacks[us][from] & pieces(~us) & bit(to));
        bool isSinglePush = (from + up == to) && (board[to] == EMPTY);
        bool isDoublePush = (from + up + up == to) && (bit(from) & rank2BB) && 
                            (board[to - up] == EMPTY) && (board[to] == EMPTY);

        if (!(isCapture || isSinglePush || isDoublePush)) {
            return false;
        }

    } else if (!(genAttacksBB(typeOf(pc), from, pieces()) & bit(to))) {
        return false;
    }

    if (checkers()) {
        if (typeOf(pc) != KING) {
            if (moreThanOne(checkers())) {
                return false;
            }

            if (!(betweenBB[kingSquare(us)][lsb(checkers())] & bit(to))) {
                return false;
            }
        } else if (getAttackers(to, ~us, pieces() ^ bit(from))) {
            return false;
        }
    }
    
    return true;
}

bool Board::legalMove(Move move) const {
    Colour us = colToMove;
    Square from = fromSq(move);
    Square to = toSq(move);
    Flags flag = moveFlag(move);

    if (flag == ENPASSANT) {
        Square ksq = kingSquare(us);
        Square capsq = us == WHITE ? to + SOUTH : to + NORTH; 

        Bitboard occupied = (pieces() ^ bit(from) ^ bit(capsq)) | bit(to);

        return !(genAttacksBB<ROOK>(ksq, occupied) & (pieces(~us, ROOK) | pieces(~us, QUEEN))) &&
               !(genAttacksBB<BISHOP>(ksq, occupied) & (pieces(~us, BISHOP) | pieces(~us, QUEEN)));
    }

    if (flag == CASTLE) {
        Direction step = to > from ? WEST : EAST;
        Square s = to;
        while(true) {
            if (getAttackers(s, ~us, allPieces)) {
                return false;
            }
            if (s == from) {
                return true;
            }
            s += step;
        }
    }
    
    if (pieceOn(from) == makePiece(us, KING)) {
        return !getAttackers(to, ~us, allPieces ^ bit(from));
    }

    return !(st->blockersForKing[us] & bit(from)) || (lineBB[from][to] & pieces(us, KING));
}

bool Board::seeThreshold(Move move, int thresh) const {
    if (moveFlag(move) != NOFLAG) return true;

    Square from = fromSq(move);
    Square to = toSq(move);

    int exchange = W.material[typeOf(pieceOn(to))].mg - thresh;

    if (exchange < 0) return false;

    exchange = W.material[typeOf(pieceOn(from))].mg - exchange;

    if (exchange <= 0) return true;

    Bitboard occ = pieces() ^ bit(from) ^ bit(to);
    Colour stm = sideToMove();
    Bitboard attackers = getAttackers(to, stm, occ) | getAttackers(to, ~stm, occ);
    Bitboard stmAttackers, bb;

    int res = 1;
    while (true) {
        stm = ~stm;
        attackers &= occ;
        stmAttackers = attackers & pieces(stm);
        if (pinners(~stm) & occ) {
            stmAttackers &= ~pinned(stm);
        }

        if (!stmAttackers) break;

        res ^= 1;

        // Cycle through attackers from least to most valuable
        // If at any point we can stop capturing and are above or at the threshold return true
        // If at any point they can stop capturing and we are below the threshold return false
        if ((bb = stmAttackers & pieces(stm, PAWN))) {
            if ((exchange = W.material[PAWN].mg - exchange) < res) break;

            occ ^= bit(static_cast<Square>(lsb(bb)));
            attackers |= genAttacksBB<BISHOP>(to, occ) & (pieces(BISHOP) | pieces(QUEEN));
        } else if ((bb = stmAttackers & pieces(stm, KNIGHT))) {
            if ((exchange = W.material[KNIGHT].mg - exchange) < res) break;

            occ ^= bit(static_cast<Square>(lsb(bb)));
        } else if ((bb = stmAttackers & pieces(stm, BISHOP))) {
            if ((exchange = W.material[BISHOP].mg - exchange) < res) break;

            occ ^= bit(static_cast<Square>(lsb(bb)));
            attackers |= genAttacksBB<BISHOP>(to, occ) & (pieces(BISHOP) | pieces(QUEEN));
        } else if ((bb = stmAttackers & pieces(stm, ROOK))) {
            if ((exchange = W.material[ROOK].mg - exchange) < res) break;

            occ ^= bit(static_cast<Square>(lsb(bb)));
            attackers |= genAttacksBB<ROOK>(to, occ) & (pieces(ROOK) | pieces(QUEEN));
        } else if ((bb = stmAttackers & pieces(stm, QUEEN))) {
            if ((exchange = W.material[QUEEN].mg - exchange) < res) break;

            occ ^= bit(static_cast<Square>(lsb(bb)));
            attackers |= genAttacksBB<BISHOP>(to, occ) & (pieces(BISHOP) | pieces(QUEEN));
            attackers |= genAttacksBB<ROOK>(to, occ) & (pieces(ROOK) | pieces(QUEEN));
        } else {
            return attackers & pieces(~stm) ? res ^ 1 : res;
        }
    }
    return res;
}

}