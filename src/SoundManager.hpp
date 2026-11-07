#pragma once

#include <string>

namespace ffc {

class SoundManager {
public:
    static SoundManager& get();

    // Play the ding sound for a given frame-window category (e.g., 12 -> highest pitch, 1 -> lowest pitch).
    // If forcePlayForGuide is true, plays even if gameplay dings are muted (for the Sound Guide / preview buttons).
    void playCategoryDing(int frames, bool forcePlayForGuide = false);

    // Return the resolved filesystem path that will be played for a category
    std::string resolveSoundPath(int frames, float& outSpeedMultiplier) const;

private:
    SoundManager() = default;
};

} // namespace ffc
