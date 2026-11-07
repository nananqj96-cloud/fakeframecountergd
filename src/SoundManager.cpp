#include "SoundManager.hpp"
#include "ModState.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <filesystem>

using namespace geode::prelude;

namespace ffc {

SoundManager& SoundManager::get() {
    static SoundManager instance;
    return instance;
}

std::string SoundManager::resolveSoundPath(int frames, float& outSpeedMultiplier) const {
    outSpeedMultiplier = 1.0f;
    auto const& cats = ModState::get().categories();

    // 1. Check per-category custom sound path
    if (auto const* cat = cats.findCategory(frames)) {
        if (cat->hasCustomSound && !cat->customSoundPath.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(cat->customSoundPath, ec)) {
                outSpeedMultiplier = 1.0f;
                return cat->customSoundPath;
            }
        }
    }

    // 2. Check global custom ding sound setting
    auto globalPath = Mod::get()->getSettingValue<std::filesystem::path>("custom-global-sound");
    if (!globalPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(globalPath, ec)) {
            outSpeedMultiplier = cats.fmodPitchMultiplierForFrames(frames);
            return geode::utils::string::pathToString(globalPath);
        }
    }

    // 3. Default built-in synthesized bell chimes (ding_12f.wav highest pitch .. ding_1f.wav lowest pitch)
    std::string resName = cats.builtinSoundResourceForFrames(frames);
    if (frames > 12) {
        if (cats.pitchDirection() == PitchDirection::High12FToLow1F) {
            outSpeedMultiplier = std::clamp(1.0f + static_cast<float>(frames - 12) * 0.03f, 1.0f, 1.65f);
        } else {
            outSpeedMultiplier = std::clamp(1.0f - static_cast<float>(frames - 12) * 0.02f, 0.55f, 1.0f);
        }
    }
    auto fullPath = Mod::get()->getResourcesDir() / resName;
    return geode::utils::string::pathToString(fullPath);
}

void SoundManager::playCategoryDing(int frames, bool forcePlayForGuide) {
    bool soundEn = Mod::get()->getSettingValue<bool>("sound-enabled");
    if (!soundEn && !forcePlayForGuide) {
        return;
    }

    float volume = static_cast<float>(Mod::get()->getSettingValue<double>("sound-volume"));
    if (forcePlayForGuide && volume <= 0.01f) {
        volume = 0.75f;
    }
    if (volume <= 0.001f) {
        return;
    }

    float speed = 1.0f;
    std::string path = resolveSoundPath(frames, speed);
    if (path.empty()) return;

    if (auto* fmod = FMODAudioEngine::sharedEngine()) {
        fmod->playEffect(path, speed, 0.0f, volume);
    }
}

} // namespace ffc
