#pragma once
#include "types.h"

namespace engine
{
struct History {
    int cont1[PIECETYPECOUNT][SQUARECOUNT][PIECETYPECOUNT][SQUARECOUNT]{};
    int cont2[PIECETYPECOUNT][SQUARECOUNT][PIECETYPECOUNT][SQUARECOUNT]{};
    int cont3[PIECETYPECOUNT][SQUARECOUNT][PIECETYPECOUNT][SQUARECOUNT]{};
    int cont4[PIECETYPECOUNT][SQUARECOUNT][PIECETYPECOUNT][SQUARECOUNT]{};
    int main [COLOURNB][PIECETYPECOUNT][SQUARECOUNT][SQUARECOUNT]{};
    int cap  [PIECETYPECOUNT][SQUARECOUNT][PIECETYPECOUNT]{};

    inline int& mainHist(Colour us, int pt, int from, int to) {
        return main[us][pt][from][to];
    }
    inline int  mainHist(Colour us, int pt, int from, int to) const {
        return main[us][pt][from][to];
    }

    inline int& cont1Hist(int prevPT, int prevTo, int pt, int to) {
        return cont1[prevPT][prevTo][pt][to];
    }
    inline int  cont1Hist(int prevPT, int prevTo, int pt, int to) const {
        return cont1[prevPT][prevTo][pt][to];
    }

    inline int& cont2Hist(int prevPT, int prevTo, int pt, int to) {
        return cont2[prevPT][prevTo][pt][to];
    }
    inline int  cont2Hist(int prevPT, int prevTo, int pt, int to) const {
        return cont2[prevPT][prevTo][pt][to];
    }

    inline int& cont3Hist(int prevPT, int prevTo, int pt, int to) {
        return cont3[prevPT][prevTo][pt][to];
    }
    inline int  cont3Hist(int prevPT, int prevTo, int pt, int to) const {
        return cont3[prevPT][prevTo][pt][to];
    }

    inline int& cont4Hist(int prevPT, int prevTo, int pt, int to) {
        return cont4[prevPT][prevTo][pt][to];
    }
    inline int  cont4Hist(int prevPT, int prevTo, int pt, int to) const {
        return cont4[prevPT][prevTo][pt][to];
    }

    inline int& capHist(int movedPT, int to, int capturedPT) {
        return cap[movedPT][to][capturedPT];
    }
    inline int  capHist(int movedPT, int to, int capturedPT) const {
        return cap[movedPT][to][capturedPT];
    }

    void clear() {
            // Main quiet history
    for (int c = 0; c < COLOURNB; ++c)
        for (int pt = 0; pt < PIECETYPECOUNT; ++pt)
            for (int from = 0; from < SQUARECOUNT; ++from)
                for (int to = 0; to < SQUARECOUNT; ++to)
                    main[c][pt][from][to] = -50;

    // Continuation histories
    for (int ppt = 0; ppt < PIECETYPECOUNT; ++ppt)
        for (int pto = 0; pto < SQUARECOUNT; ++pto)
            for (int pt = 0; pt < PIECETYPECOUNT; ++pt)
                for (int to = 0; to < SQUARECOUNT; ++to) {
                    cont1[ppt][pto][pt][to] = -100;
                    cont2[ppt][pto][pt][to] = -100;
                    cont3[ppt][pto][pt][to] = -100;
                    cont4[ppt][pto][pt][to] = -100;
                }

    // Capture history
    for (int pt = 0; pt < PIECETYPECOUNT; ++pt)
        for (int to = 0; to < SQUARECOUNT; ++to)
            for (int cpt = 0; cpt < PIECETYPECOUNT; ++cpt)
                cap[pt][to][cpt] = -200;
    }
};

}

    