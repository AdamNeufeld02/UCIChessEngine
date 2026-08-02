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
#include "eval.h"

namespace engine {

struct State{
    ZobristKey boardKey;
    ZobristKey pawnKey;
    CastlingRight castlingRights;
    Square epSquare;
    int halfmoveClock;
    int fullmoveNumber;
    int ply;
    int movesFromNull;

    Piece captured;
    Bitboard blockersForKing[COLOURNB];
    Bitboard pinners[COLOURNB];
    Bitboard checkSquares[PIECETYPECOUNT];
    Bitboard checkers;
    int repetitionCount;
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
    Score material[COLOURNB];
    Score psqtv[COLOURNB];


public:
    Board();
    void initFields();
    void initRootState(State* rootState);
    void fenToBoard(std::string fenString, State* rootState);
    void makeMove(Move move, State* newState);
    void undoMove(Move move);

    void makeNullMove(State* newState);
    void undoNullMove();

    void updateChecksAndPins(Colour col);
    Bitboard getAttackers(Square sq, Colour col, Bitboard occ) const;
    Bitboard checkSquares(PieceType pt) const;
    void updateKingBlockers(Colour col);

    void putPiece(Piece pc, Square sq);
    void removePiece(Square sq);
    void movePiece(Square from, Square to);

    void calcZobristHashFromScratch();

    bool seeThreshold(Move move, int thresh) const;

    bool legalMove(Move move) const;
    bool pseudoLegalMove(Move move) const;
    bool givesCheck(Move move) const;
    bool isCapture(Move move) const;
    bool captureGenType(Move move) const;
    bool nonPawnMaterial(Colour col) const;

    bool isDraw() const;
    bool isRepetitionDraw() const;

    bool onOpenFile(Colour col, Square sq) const;

    // Accessors
    Piece pieceOn(Square sq) const;
    Bitboard pieces(Colour col, PieceType pt) const;
    Bitboard pieces(PieceType pt) const;
    Bitboard pieces(Colour col) const;
    Bitboard pieces() const;
    Colour sideToMove() const;
    Square kingSquare(Colour col) const;
    Square epSquare() const;
    Bitboard checkers() const;
    Bitboard pinners(Colour col) const;
    Bitboard pinned(Colour col) const;
    bool canCastle(CastlingRight cr) const;
    bool castlingBlocked(CastlingRight cr) const;
    ZobristKey key() const;
    ZobristKey pawnKey() const;
    int ply() const;
};

inline Piece Board::pieceOn(Square sq) const {
    return board[sq];
}

inline Bitboard Board::pieces(Colour col, PieceType pt) const {
    return pieceBB[makePiece(col, pt)];
}

inline Bitboard Board::pieces(PieceType pt) const {
    return pieceBB[makePiece(WHITE, pt)] | pieceBB[makePiece(BLACK, pt)];
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

inline Bitboard Board::checkSquares(PieceType pt) const {
    return st->checkSquares[pt];
}

inline Bitboard Board::pinners(Colour col) const {
    return st->pinners[col];
}

inline Bitboard Board::pinned(Colour col) const {
    return st->blockersForKing[col] & pieces(col);
}

inline bool Board::canCastle(CastlingRight cr) const {
    return st->castlingRights & cr;
}

inline bool Board::castlingBlocked(CastlingRight cr) const {
    return allPieces & castleMasks[cr];
}

inline bool Board::onOpenFile(Colour col, Square sq) const {
    return !(pieces(col, PAWN) & fileBB[sq & 7]);
}

inline Square Board::epSquare() const {
    return st->epSquare;
}

inline ZobristKey Board::key() const {
    return st->boardKey;
}

inline ZobristKey Board::pawnKey() const {
    return st->pawnKey;
}

inline bool Board::isDraw() const {
    return st->halfmoveClock > 99;
}

inline bool Board::isRepetitionDraw() const {
    return st->repetitionCount >= 2;
}

inline bool Board::isCapture(Move move) const {
    return board[toSq(move)];
}

inline bool Board::captureGenType(Move move) const {
    return board[toSq(move)] || (promoPiece(move) == QUEEN);
}

inline bool Board::nonPawnMaterial(Colour col) const {
    return pieces(col, KNIGHT) | pieces(col, BISHOP) | pieces(col, ROOK) | pieces(col, QUEEN);
}

inline int Board::ply() const {
    return st->ply;
}

inline void Board::putPiece(Piece pc, Square sq) {
    Bitboard place = bit(sq);
    Colour col = colourOf(pc);
    board[sq] = pc;
    pieceBB[pc] |= place;
    colourBB[col] |= place;
    allPieces |= place;

    PieceType pt = typeOf(pc);
    Square mirrored = mirrorSquareIfBlack(sq, col);
    material[col] += W.material[pt];
    psqtv[col] += W.psqt[pt][mirrored];
}

inline void Board::removePiece(Square sq) {
    Bitboard place = bit(sq);
    Piece pc = board[sq];
    Colour col = colourOf(pc);
    pieceBB[pc] ^= place;
    colourBB[col] ^= place;
    allPieces ^= place;
    board[sq] = EMPTY;

    PieceType pt = typeOf(pc);
    Square mirrored = mirrorSquareIfBlack(sq, col);
    material[col] -= W.material[pt];
    psqtv[col] -= W.psqt[pt][mirrored];
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

    PieceType pt = typeOf(pc);
    Square mirroredFrom = mirrorSquareIfBlack(from, col);
    Square mirroredTo = mirrorSquareIfBlack(to, col);

    psqtv[col] -= W.psqt[pt][mirroredFrom];
    psqtv[col] += W.psqt[pt][mirroredTo];
}
}