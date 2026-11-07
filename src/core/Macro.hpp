#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ffc {

struct MacroInput {
    int64_t frame = 0;       // Physics tick / frame index at the macro's TPS
    uint8_t button = 1;      // 1 = Jump, 2 = Left (Platformer), 3 = Right (Platformer)
    bool player2 = false;    // false = Player 1, true = Player 2
    bool down = true;        // true = Press, false = Release

    // Optional recorded player position snapshot from the macro (if available)
    bool hasPosition = false;
    float x = 0.0f;
    float y = 0.0f;
};

struct MacroData {
    std::string name;
    std::string format;      // e.g. "Mega Hack Binary (.mhr)", "Mega Hack JSON (.mhr.json)", "GDR2 (.gdr2)", "GDR (.gdr)"
    std::string botName;
    std::string author;
    std::string levelName;
    uint32_t levelId = 0;
    double tps = 240.0;      // Framerate / physics TPS stored in the macro
    double durationSeconds = 0.0;
    bool platformer = false;
    size_t practiceDeaths = 0;
    std::vector<MacroInput> inputs;
};

struct MacroLoadResult {
    bool ok = false;
    std::string error;
    MacroData macro;
};

std::vector<std::string> supportedMacroFilterPatterns();

MacroLoadResult parseMacroBytes(std::vector<uint8_t> const& data, std::string const& fileName);
MacroLoadResult loadMacroFromFile(std::filesystem::path const& path);

} // namespace ffc
