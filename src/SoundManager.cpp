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

int SoundManager::getVolumePercent() const {
    auto val = Mod::get()->getSettingValue<int64_t>("ding-volume");
    return static_cast<int>(std::clamp<int64_t>(val, 0, 1000));
}

void SoundManager::setVolumePercent(int pct) {
    int clamped = std::clamp(pct, 0, 1000);
    Mod::get()->setSettingValue<int64_t>("ding-volume", static_cast<int64_t>(clamped));
}

std::string SoundManager::resolveSoundPath(int frames, float& outSpeedMultiplier) const {
    outSpeedMultiplier = 1.0f;
    auto const& cats = ModState::get().categories();

    // 1. Check per-category custom sound path
    if (auto const* cat = cats.findCategory(frames)) {
        if (!cat->customSoundPath.empty()) {
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

    int volPct = getVolumePercent();
    float volume = static_cast<float>(volPct) / 100.0f;
    if (forcePlayForGuide && volume <= 0.01f) {
        volume = 1.0f;
    }
    if (volume <= 0.001f) {
        return;
    }

    float speed = 1.0f;
    std::string path = resolveSoundPath(frames, speed);
    if (path.empty()) return;

    auto* fmod = FMODAudioEngine::sharedEngine();
    if (!fmod) return;

    // Play directly on FMOD's master channel group so volume > 1.0 (e.g. 500% = 5.0f, 1000% = 10.0f)
    // is amplified linearly without being clamped to 100% by FMODAudioEngine::playEffect or muted in PauseLayer.
    if (fmod->m_system) {
        FMOD::Sound* sound = nullptr;
        auto it = m_soundCache.find(path);
        if (it != m_soundCache.end() && it->second != nullptr) {
            sound = it->second;
        } else {
            if (fmod->m_system->createSound(path.c_str(), FMOD_DEFAULT, nullptr, &sound) == FMOD_OK && sound != nullptr) {
                m_soundCache[path] = sound;
            }
        }
        if (sound != nullptr) {
            FMOD::Channel* channel = nullptr;
            if (fmod->m_system->playSound(sound, nullptr, true, &channel) == FMOD_OK && channel != nullptr) {
                channel->setPitch(speed);
                channel->setVolume(volume);
                channel->setPaused(false);
                return;
            }
        }
    }

    int chId = fmod->playEffect(path, speed, 0.0f, volume);
    if (auto* ch = fmod->channelForChannelID(chId)) {
        ch->setPitch(speed);
        ch->setVolume(volume);
    }
}

} // namespace ffc
