#pragma once

#include <string>
#include <unordered_map>

namespace FMOD {
    class Sound;
}

namespace ffc {

class SoundManager {
public:
    static SoundManager& get();

    // Get/set custom ding volume percentage (0% to 1000%, e.g. 100, 500, 1000).
    int getVolumePercent() const;
    void setVolumePercent(int pct);

    // Play the ding sound for a given frame-window category (e.g., 12 -> highest pitch, 1 -> lowest pitch).
    // If forcePlayForGuide is true, plays even if gameplay dings are muted (for the Sound Guide / preview buttons).
    void playCategoryDing(int frames, bool forcePlayForGuide = false);

    // Return the resolved filesystem path that will be played for a category
    std::string resolveSoundPath(int frames, float& outSpeedMultiplier) const;

private:
    SoundManager() = default;
    std::unordered_map<std::string, FMOD::Sound*> m_soundCache;
};

} // namespace ffc
