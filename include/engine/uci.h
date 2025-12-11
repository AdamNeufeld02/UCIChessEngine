#pragma once

#include <string>
#include <vector>
#include <istream>

#include "engine.h"
#include "types.h"
#include "move.h"

namespace engine {

class UCIEngine {
public:
    UCIEngine();
    void loop();

private:
    Engine engine;

    // === Command dispatch ===
    void handleLine(const std::string& line);

    void cmdUci();
    void cmdIsReady();
    void cmdUciNewGame();
    void cmdPosition(std::istringstream& iss);
    void cmdGo(std::istringstream& iss);
    void cmdStop();
    void cmdQuit();

    // === Helpers ===
    SearchLimits parseGo(std::istringstream& iss);
    Move parseMove(const std::string& uciMove) const;
    void parsePosition(std::istringstream& iss);
    std::vector<Move> parseMovesFromStream(std::istringstream& iss);
    
};

}