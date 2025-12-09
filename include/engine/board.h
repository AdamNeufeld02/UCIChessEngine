#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <stdexcept>
#include "types.h"
#include "move.h"
#include "bitboards.h"
#include "hashing.h"
#include "parsing.h"

namespace engine {

struct State{
    ZobristKey boardKey;
    CastlingRight castlingRights;
    Square epSquare;
    int halfmoveClock;
    int fullmoveNumber;

    Piece captured;
    Bitboard blockersForKing[COLOURNB];
    Bitboard checkers;
    State* previous;
};

class Board{
public:
    Bitboard pieceBB[PIECECOUNT];
    Bitboard colourBB[COLOURNB];
    Bitboard allPieces;
    Piece board[SQUARECOUNT];
    Colour colToMove;
    State* st;


public:
    Board();
    void initFields();
    void initRootState(State* rootState);
    void fenToBoard(std::string fenString, State* rootState);
    void makeMove(Move move, State* newState);
    void undoMove(Move move);

    void updateChecksAndPins(Colour col);
    Bitboard getAttackers(Square sq, Colour col, Bitboard occ) const;
    void updateKingBlockers(Colour col);

    void putPiece(Piece pc, Square sq);
    void removePiece(Square sq);
    void movePiece(Square from, Square to);

    void calcZobristHashFromScratch();

    bool legalMove(Move move) const;

    // Accessors
    Piece pieceOn(Square sq) const;
    Bitboard pieces(Colour col, PieceType pt) const;
    Bitboard pieces(Colour col) const;
    Bitboard pieces() const;
    Colour sideToMove() const;
    Square kingSquare(Colour col) const;
    Square epSquare() const;
    Bitboard checkers() const;
    bool canCastle(CastlingRight cr) const;
    bool castlingBlocked(CastlingRight cr) const;
};

inline Piece Board::pieceOn(Square sq) const {
    return board[sq];
}

inline Bitboard Board::pieces(Colour col, PieceType pt) const {
    return pieceBB[makePiece(col, pt)];
}

inline Bitboard Board::pieces(Colour col) const {
    return colourBB[col];
}

inline Bitboard Board::pieces() const {
    return allPieces;
}

inline Colour Board::sideToMove() const {
    return colToMove;
}

inline Square Board::kingSquare(Colour col) const {
    return static_cast<Square>(lsb(pieces(col, KING)));
}

inline Bitboard Board::checkers() const {
    return st->checkers;
}

inline bool Board::canCastle(CastlingRight cr) const {
    return st->castlingRights & cr;
}

inline bool Board::castlingBlocked(CastlingRight cr) const {
    return allPieces & castleMasks[cr];
}

inline Square Board::epSquare() const {
    return st->epSquare;
}

inline void Board::putPiece(Piece pc, Square sq) {
    Bitboard place = bit(sq);
    Colour col = colourOf(pc);
    board[sq] = pc;
    pieceBB[pc] |= place;
    colourBB[col] |= place;
    allPieces |= place;
}

inline void Board::removePiece(Square sq) {
    Bitboard place = bit(sq);
    Piece pc = board[sq];
    Colour col = colourOf(pc);
    pieceBB[pc] ^= place;
    colourBB[col] ^= place;
    allPieces ^= place;
    board[sq] = EMPTY;
}

inline void Board::movePiece(Square from, Square to) {
    Bitboard fromTo = bit(from) | bit(to);
    Piece pc = board[from];
    Colour col = colourOf(pc);
    pieceBB[pc] ^= fromTo;
    colourBB[col] ^= fromTo;
    allPieces ^= fromTo;
    board[from] = EMPTY;
    board[to] = pc; 
}
}