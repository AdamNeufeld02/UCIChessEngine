#include "engine/uci.h"

int main() {
    engine::UCIEngine engine = engine::UCIEngine();
    engine.loop();
}