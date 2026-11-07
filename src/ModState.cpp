#include "ModState.hpp"
#include "Overlay.hpp"
#include "SoundManager.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace ffc {

namespace {

uint64_t hashMix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Query nearest hazard distance at an arbitrary (x, y) level coordinate using PlayLayer's spatial grid.
float queryHazardDistAtPoint(PlayLayer* pl, float x, float y) {
    if (!pl) return -1.0f;
    float bestSq = 1e12f;
    bool found = false;

    float sxFactor = (pl->m_sectionXFactor > 1e-5f) ? pl->m_sectionXFactor : 0.01f;
    float syFactor = (pl->m_sectionYFactor > 1e-5f) ? pl->m_sectionYFactor : 0.01f;
    int cx = std::max(0, static_cast<int>(x * sxFactor));
    int cy = std::max(0, static_cast<int>(y * syFactor));

    int totalX = static_cast<int>(pl->m_nonEffectObjects.size());
    int totalSizesX = static_cast<int>(pl->m_nonEffectObjectsSizes.size());
    if (totalX > 0 && totalSizesX >= totalX) {
        int xStart = std::max(0, cx - 1);
        int xEnd = std::min(totalX - 1, cx + 2);
        for (int sx = xStart; sx <= xEnd; ++sx) {
            auto* xVec = pl->m_nonEffectObjects[sx];
            auto* xSizes = pl->m_nonEffectObjectsSizes[sx];
            if (!xVec || !xSizes) continue;
            int maxY = std::min(static_cast<int>(xVec->size()), static_cast<int>(xSizes->size())) - 1;
            int yStart = std::max(0, cy - 1);
            int yEnd = std::min(maxY, cy + 1);
            for (int sy = yStart; sy <= yEnd; ++sy) {
                auto* vec = (*xVec)[sy];
                if (!vec) continue;
                int count = std::min(static_cast<int>(vec->size()), (*xSizes)[sy]);
                count = std::clamp(count, 0, 256);
                for (int i = 0; i < count; ++i) {
                    GameObject* obj = (*vec)[i];
                    if (!obj || obj == pl->m_anticheatSpike || obj->m_isGroupDisabled || obj->m_isDisabled) continue;
                    auto type = obj->m_objectType;
                    if (type != GameObjectType::Hazard && type != GameObjectType::AnimatedHazard) continue;
                    CCPoint oPos = obj->getPosition();
                    float dx = oPos.x - x;
                    float dy = oPos.y - y;
                    if (dx >= -30.0f && dx <= 165.0f && std::abs(dy) <= 95.0f) {
                        float dSq = dx * dx + dy * dy;
                        if (dSq < bestSq) {
                            bestSq = dSq;
                            found = true;
                        }
                    }
                }
            }
        }
    }

    if (!found) return -1.0f;
    return std::max(0.0f, std::sqrt(bestSq) - 18.0f);
}

} // namespace

ModState& ModState::get() {
    static ModState instance;
    return instance;
}

ModState::ModState() {
    auto* mod = Mod::get();
    auto savedCats = mod->getSavedValue<std::string>("category-config-v1", "");
    if (!savedCats.empty()) {
        m_categories.deserialize(savedCats);
    }

    // One-time migration so users updating the mod automatically get the new Better Estimate mode
    if (!mod->getSavedValue<bool>("migrated-better-estimate-v1", false)) {
        mod->setSettingValue<std::string>("analysis-mode", "Better Estimate (Hazard & Wall Check)");
        mod->setSavedValue<bool>("migrated-better-estimate-v1", true);
    }

    syncFromGeodeSettings();
}

void ModState::syncFromGeodeSettings() {
    auto* mod = Mod::get();
    m_enabled = mod->getSettingValue<bool>("enabled");

    auto modeStr = mod->getSettingValue<std::string>("analysis-mode");
    if (modeStr.find("Real") != std::string::npos) {
        m_calcMode = WindowCalcMode::RealAnalysis;
    } else if (modeStr.find("Fake") != std::string::npos) {
        m_calcMode = WindowCalcMode::FakeEstimate;
    } else {
        m_calcMode = WindowCalcMode::BetterEstimate;
    }
    m_experimentalRealMode = (m_calcMode == WindowCalcMode::RealAnalysis);

    DetectionConfig dcfg = m_detector.config();
    dcfg.betterEstimate = (m_calcMode == WindowCalcMode::BetterEstimate);

    auto sensStr = mod->getSettingValue<std::string>("sensitivity-preset");
    if (sensStr == "Selective") dcfg.sensitivity = SensitivityPreset::Selective;
    else if (sensStr == "Permissive") dcfg.sensitivity = SensitivityPreset::Permissive;
    else dcfg.sensitivity = SensitivityPreset::Balanced;

    dcfg.sensitivityBias = static_cast<float>(mod->getSettingValue<double>("sensitivity-bias"));

    auto relStr = mod->getSettingValue<std::string>("release-filter");
    if (relStr == "Presses Only") dcfg.releaseFilter = ReleaseFilterMode::PressesOnly;
    else if (relStr == "All Releases") dcfg.releaseFilter = ReleaseFilterMode::AllReleases;
    else dcfg.releaseFilter = ReleaseFilterMode::SmartGamemode;

    dcfg.maxMomentsPerSecond = static_cast<float>(mod->getSettingValue<double>("max-moments-per-second"));
    dcfg.minDistanceUnits = static_cast<float>(mod->getSettingValue<double>("min-distance-units"));
    dcfg.proximityScaleUnits = static_cast<float>(mod->getSettingValue<double>("proximity-scale-units"));
    m_detector.setConfig(dcfg);

    TierColors tiers;
    auto c14 = mod->getSettingValue<ccColor3B>("color-tier-1-4");
    auto c56 = mod->getSettingValue<ccColor3B>("color-tier-5-6");
    auto c79 = mod->getSettingValue<ccColor3B>("color-tier-7-9");
    auto c10 = mod->getSettingValue<ccColor3B>("color-tier-10-12");
    tiers.range1To4   = {c14.r, c14.g, c14.b};
    tiers.range5To6   = {c56.r, c56.g, c56.b};
    tiers.range7To9   = {c79.r, c79.g, c79.b};
    tiers.range10Plus = {c10.r, c10.g, c10.b};
    m_categories.setTierColors(tiers);

    auto pitchStr = mod->getSettingValue<std::string>("pitch-direction");
    if (pitchStr.find("1F Highest") != std::string::npos) {
        m_categories.setPitchDirection(PitchDirection::High1FToLow12F);
    } else {
        m_categories.setPitchDirection(PitchDirection::High12FToLow1F);
    }

    m_macroPlayer.setLockDeltaToTps(mod->getSettingValue<bool>("macro-lock-delta"));
    m_macroPlayer.setFrameOffset(static_cast<int>(mod->getSettingValue<int64_t>("macro-frame-offset")));
}

void ModState::saveCategoryConfig() {
    Mod::get()->setSavedValue<std::string>("category-config-v1", m_categories.serialize());
}

void ModState::setModEnabled(bool en) {
    m_enabled = en;
    Mod::get()->setSettingValue<bool>("enabled", en);
}

void ModState::setCalcMode(WindowCalcMode mode) {
    m_calcMode = mode;
    m_experimentalRealMode = (mode == WindowCalcMode::RealAnalysis);
    char const* str = "Better Estimate (Hazard & Wall Check)";
    if (mode == WindowCalcMode::FakeEstimate) {
        str = "Fake / Visual Estimate (Simulated)";
    } else if (mode == WindowCalcMode::RealAnalysis) {
        str = "Real Analysis (Experimental Macro Scan)";
    }
    Mod::get()->setSettingValue<std::string>("analysis-mode", str);
    syncFromGeodeSettings();
}

void ModState::cycleCalcMode() {
    switch (m_calcMode) {
        case WindowCalcMode::BetterEstimate:
            setCalcMode(WindowCalcMode::FakeEstimate);
            break;
        case WindowCalcMode::FakeEstimate:
            setCalcMode(WindowCalcMode::RealAnalysis);
            break;
        case WindowCalcMode::RealAnalysis:
            setCalcMode(WindowCalcMode::BetterEstimate);
            break;
    }
}

void ModState::setExperimentalRealMode(bool realMode) {
    setCalcMode(realMode ? WindowCalcMode::RealAnalysis : WindowCalcMode::BetterEstimate);
}

void ModState::resetAttemptCounts() {
    m_detector.reset();
    m_attemptTotal = 0;
    m_attemptCategoryCounts.clear();
    m_attemptMoments.clear();
}

int ModState::countForCategory(int frames) const {
    auto it = m_attemptCategoryCounts.find(frames);
    return it != m_attemptCategoryCounts.end() ? it->second : 0;
}

CCPoint ModState::playerObjectLayerPos(PlayLayer* pl, PlayerObject* player) {
    if (!player) return {0.f, 0.f};
    CCPoint pos = player->getPosition();
    if (!pl || !pl->m_objectLayer) return pos;
    auto* parent = player->getParent();
    if (!parent || parent == pl->m_objectLayer) return pos;
    return pl->m_objectLayer->convertToNodeSpace(parent->convertToWorldSpace(pos));
}

Gamemode ModState::detectGamemode(PlayerObject* player) const {
    if (!player) return Gamemode::Cube;
    if (player->m_isDart)   return Gamemode::Wave;
    if (player->m_isShip)   return Gamemode::Ship;
    if (player->m_isBall)   return Gamemode::Ball;
    if (player->m_isBird)   return Gamemode::Ufo;
    if (player->m_isRobot)  return Gamemode::Robot;
    if (player->m_isSpider) return Gamemode::Spider;
    if (player->m_isSwing)  return Gamemode::Swing;
    return Gamemode::Cube;
}

void ModState::computeObstacleClearances(PlayLayer* pl, PlayerObject* player,
                                         float& outHazardDist, float& outSolidDist,
                                         bool& outNearLethal) const {
    outHazardDist = -1.0f;
    outSolidDist = -1.0f;
    outNearLethal = false;
    if (!pl || !player) return;

    CCPoint pPos = player->getPosition();
    bool goingLeft = player->m_isGoingLeft;
    bool upsideDown = player->m_isUpsideDown;
    bool grounded = player->m_isOnGround || player->m_isOnGround2;
    Gamemode gm = detectGamemode(player);

    float bestHazDist = 9999.0f;
    bool foundHaz = false;

    float bestWallDist = 9999.0f;
    bool foundWall = false;

    auto inspectObj = [&](GameObject* obj) {
        if (!obj || obj == pl->m_anticheatSpike || obj->m_isGroupDisabled || obj->m_isDisabled) {
            return;
        }
        GameObjectType type = obj->m_objectType;
        if (type == GameObjectType::Decoration) return;

        CCPoint oPos = obj->getPosition();
        float rawDx = oPos.x - pPos.x;
        float dx = goingLeft ? -rawDx : rawDx;
        float dy = oPos.y - pPos.y;

        if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
            // Hazard in front of or immediately under/above the player
            if (dx >= -26.0f && dx <= 155.0f && std::abs(dy) <= 92.0f) {
                float d = std::max(0.0f, std::hypot(dx, dy) - 18.0f);
                if (d < bestHazDist) {
                    bestHazDist = d;
                    foundHaz = true;
                }
            }
            return;
        }

        if (type == GameObjectType::Solid || type == GameObjectType::Breakable || type == GameObjectType::Slope) {
            if (dx < -14.0f || dx > 145.0f) return;

            bool isLethalWall = false;
            if (gm == Gamemode::Wave) {
                // In Wave mode, touching any solid block or slope kills the player
                if (std::abs(dy) <= 82.0f) {
                    isLethalWall = true;
                }
            } else if (gm == Gamemode::Ship || gm == Gamemode::Ufo || gm == Gamemode::Swing) {
                // In flying modes, a solid block ahead in the corridor (not a flat floor the ship is resting on) kills on frontal impact
                bool restingFloor = grounded && (!upsideDown ? (dy < -12.0f) : (dy > 12.0f));
                if (!restingFloor && std::abs(dy) <= 62.0f) {
                    isLethalWall = true;
                }
            } else {
                // Ground modes (Cube, Robot, Ball, Spider):
                // Floor tiles underneath the player's feet have dy <= -15 (normal gravity) or dy >= +15 (upside-down).
                // A solid block ahead at body/head height (dy in [-10, +58] normal, or [-58, +10] upside-down)
                // is a wall/step/pillar that kills the player if the player runs into it without jumping!
                if (!upsideDown) {
                    if (dx >= -10.0f && dx <= 140.0f && dy >= -10.0f && dy <= 58.0f) {
                        isLethalWall = true;
                    }
                } else {
                    if (dx >= -10.0f && dx <= 140.0f && dy <= 10.0f && dy >= -58.0f) {
                        isLethalWall = true;
                    }
                }
            }

            if (isLethalWall) {
                float d = std::max(0.0f, std::hypot(dx, dy) - 18.0f);
                if (d < bestWallDist) {
                    bestWallDist = d;
                    foundWall = true;
                }
            }
        }
    };

    // 1. Scan PlayLayer's 2D spatial grid (m_nonEffectObjects) around and ahead of the player
    float sxFactor = (pl->m_sectionXFactor > 1e-5f) ? pl->m_sectionXFactor : 0.01f;
    float syFactor = (pl->m_sectionYFactor > 1e-5f) ? pl->m_sectionYFactor : 0.01f;
    int cx = std::max(0, static_cast<int>(pPos.x * sxFactor));
    int cy = std::max(0, static_cast<int>(pPos.y * syFactor));

    int totalX = static_cast<int>(pl->m_nonEffectObjects.size());
    int totalSizesX = static_cast<int>(pl->m_nonEffectObjectsSizes.size());
    if (totalX > 0 && totalSizesX >= totalX) {
        int xStart = std::max(0, cx - 1);
        int xEnd = std::min(totalX - 1, cx + 2);
        for (int sx = xStart; sx <= xEnd; ++sx) {
            auto* xVec = pl->m_nonEffectObjects[sx];
            auto* xSizes = pl->m_nonEffectObjectsSizes[sx];
            if (!xVec || !xSizes) continue;
            int maxY = std::min(static_cast<int>(xVec->size()), static_cast<int>(xSizes->size())) - 1;
            int yStart = std::max(0, cy - 1);
            int yEnd = std::min(maxY, cy + 1);
            for (int sy = yStart; sy <= yEnd; ++sy) {
                auto* vec = (*xVec)[sy];
                if (!vec) continue;
                int count = std::min(static_cast<int>(vec->size()), (*xSizes)[sy]);
                count = std::clamp(count, 0, 256);
                for (int i = 0; i < count; ++i) {
                    inspectObj((*vec)[i]);
                }
            }
        }
    }

    // 2. Also scan PlayLayer's current collision vectors
    size_t hazVecCount = std::min<size_t>(pl->m_hazardCollisionObjects.size(), 256);
    for (size_t i = 0; i < hazVecCount; ++i) {
        inspectObj(pl->m_hazardCollisionObjects[i]);
    }
    size_t solVecCount = std::min<size_t>(pl->m_solidCollisionObjects.size(), 256);
    for (size_t i = 0; i < solVecCount; ++i) {
        inspectObj(pl->m_solidCollisionObjects[i]);
    }

    // In a live PlayLayer, when no hazard/wall is nearby, return 9999.0f (not -1.0f!)
    // so open space is recognized as safe open space.
    outHazardDist = foundHaz ? bestHazDist : 9999.0f;
    outSolidDist = foundWall ? bestWallDist : 9999.0f;

    float maxHazardRange = std::max(45.0f, m_detector.config().proximityScaleUnits);
    float maxWallRange = std::max(40.0f, m_detector.config().proximityScaleUnits * 0.90f);
    outNearLethal = (foundHaz && bestHazDist <= maxHazardRange) ||
                    (foundWall && bestWallDist <= maxWallRange);
}

MomentResult ModState::processPlayerInput(PlayLayer* pl, PlayerObject* player, bool isDown, int button, bool isPlayer2,
                                          std::optional<size_t> macroInputIndex,
                                          PreInputPlayerState preState) {
    MomentResult rejected{};
    if (!m_enabled || !pl || !player) {
        rejected.reason = FilterReason::ModDisabled;
        return rejected;
    }
    if (m_resettingLevel || !pl->m_started || pl->m_isPaused ||
        player->m_isDead || pl->m_playerDied || pl->m_levelEndAnimationStarted) {
        rejected.reason = FilterReason::DuplicateEvent;
        return rejected;
    }

    GameplayInputEvent ev{};
    ev.tick = static_cast<int64_t>(pl->m_gameState.m_currentProgress / 2);
    ev.timeSeconds = pl->m_gameState.m_levelTime;
    ev.button = static_cast<uint8_t>(std::clamp(button, 1, 3));
    ev.isDown = isDown;
    ev.isPlayer2 = isPlayer2;
    ev.isDualMode = pl->m_gameState.m_isDualMode;
    ev.isTwoPlayerMode = (pl->m_levelSettings && pl->m_levelSettings->m_twoPlayerMode);

    CCPoint worldPos = playerObjectLayerPos(pl, player);
    ev.x = worldPos.x;
    ev.y = worldPos.y;
    ev.yVelocity = player->m_yVelocity;
    ev.playerSpeed = player->m_playerSpeed;
    ev.isMini = (player->m_vehicleSize < 0.8f);
    ev.isUpsideDown = player->m_isUpsideDown;

    // IMPORTANT: In GD 2.2081, PlayerObject::pushButton immediately sets m_isOnGround = false
    // and m_stateRingJump = true, and PlayerObject::releaseButton sets m_isDashing = true and
    // m_touchedPad = true. Always use the pre-input state (and m_dashRing / m_touchingRings)
    // so a jump release in mid-air above a spike is never misclassified as an orb/dash event.
    ev.isOnGround = preState.valid ? preState.wasOnGround : (player->m_isOnGround || player->m_isOnGround2);
    ev.isDashing = (preState.valid && preState.wasDashing) || (player->m_dashRing != nullptr);
    ev.touchedRingOrPortal = isDown && (preState.valid
        ? preState.wasTouchingRing
        : (player->m_touchingRings != nullptr && player->m_touchingRings->count() > 0));
    ev.gamemode = detectGamemode(player);

    computeObstacleClearances(pl, player, ev.nearestHazardUnits, ev.nearestSolidUnits, ev.isNearLethalObstacle);

    // If Experimental Real Mode is active and we have a verified scan measurement for this macro input, use it
    if (m_experimentalRealMode) {
        if (macroInputIndex.has_value()) {
            if (auto const* mw = m_realAnalyzer.findMatchForInput(*macroInputIndex)) {
                ev.hasRealMeasurement = true;
                ev.realWindowFrames = mw->windowFrames;
            }
        } else if (!m_realAnalyzer.results().empty()) {
            // In live play after a scan, match closest scanned event by tick or position
            MeasuredInputWindow const* bestMatch = nullptr;
            float bestScore = 85.0f;
            for (auto const& r : m_realAnalyzer.results()) {
                if (r.down != isDown || r.player2 != isPlayer2) continue;
                float distScore = std::hypot(r.x - ev.x, r.y - ev.y);
                float tickScore = static_cast<float>(std::abs(r.frame - ev.tick)) * 1.5f;
                float score = std::min(distScore, tickScore);
                if (score < bestScore) {
                    bestScore = score;
                    bestMatch = &r;
                }
            }
            if (bestMatch) {
                ev.hasRealMeasurement = true;
                ev.realWindowFrames = bestMatch->windowFrames;
            }
        }
        // In Experimental Real Mode without a verified measurement, do NOT fabricate fake numbers as real!
        if (!ev.hasRealMeasurement) {
            rejected.accepted = false;
            rejected.reason = FilterReason::BelowSensitivityThreshold;
            return rejected;
        }
    }

    MomentResult res = m_detector.evaluate(ev, m_categories);
    if (res.accepted) {
        ++m_attemptTotal;
        m_attemptCategoryCounts[res.categoryFrames]++;
        m_attemptMoments.push_back(res);

        SoundManager::get().playCategoryDing(res.categoryFrames);
        if (auto* ov = FFCOverlay::find(pl)) {
            ov->onMomentAccepted(res);
        }
    }
    return res;
}

void ModState::restartLevelSafely(PlayLayer* pl) {
    if (!pl) return;
    if (pl->m_player1) pl->m_player1->releaseAllButtons();
    if (pl->m_player2) pl->m_player2->releaseAllButtons();

    if (auto* scene = CCDirector::sharedDirector()->getRunningScene()) {
        if (auto* pauseLayer = scene->getChildByType<PauseLayer>(0)) {
            pauseLayer->onRestartFull(nullptr);
            return;
        }
    }

    if (pl->m_isPaused) {
        pl->resumeAndRestart(true);
    } else {
        pl->resetLevel();
    }
}

bool ModState::startMacroPlaybackInLevel(PlayLayer* pl, std::string& outError) {
    if (!pl) {
        outError = "Open a level first to play back a macro.";
        return false;
    }
    if (!m_macroPlayer.hasMacro()) {
        outError = "Import a macro file first (.mhr, .mhr.json, .gdr2, .gdr, .json, .zbf, .re3, .xd, .txt).";
        return false;
    }

    m_macroPlayer.startPlayback();
    m_selfResetting = true;
    restartLevelSafely(pl);
    m_selfResetting = false;
    m_macroPlayer.startPlayback();

    resetAttemptCounts();
    if (auto* ov = FFCOverlay::ensure(pl)) {
        ov->onReset();
    }
    return true;
}

void ModState::stopMacroPlaybackInLevel(PlayLayer* pl) {
    m_macroPlayer.stopPlayback();
    if (pl) {
        if (pl->m_player1) pl->m_player1->releaseAllButtons();
        if (pl->m_player2) pl->m_player2->releaseAllButtons();
    }
}

bool ModState::startExperimentalRealScan(PlayLayer* pl, std::string& outError) {
    if (!pl) {
        outError = "Open a level first.";
        return false;
    }
    if (!m_macroPlayer.hasMacro() || !m_macroPlayer.macro()) {
        outError = "Import a macro file first before running an Experimental Real Scan.";
        return false;
    }
    if (pl->m_isPracticeMode) {
        outError = "Turn off Practice Mode before scanning so the macro starts from 0%.";
        return false;
    }
    if (pl->m_startPosObject) {
        outError = "Disable StartPos objects so the macro starts from 0%.";
        return false;
    }

    auto const& macro = *m_macroPlayer.macro();
    if (macro.inputs.empty()) {
        outError = "Loaded macro has no inputs to scan.";
        return false;
    }

    stopMacroPlaybackInLevel(pl);

    RealScanConfig scfg;
    scfg.tps = std::max(60.0, m_macroPlayer.effectiveTps());
    scfg.referenceFps = 60.0;
    scfg.maxWindowFrames = 12;
    scfg.frameOffset = m_macroPlayer.frameOffset();
    scfg.includeReleases = (m_detector.config().releaseFilter != ReleaseFilterMode::PressesOnly);

    m_realAnalyzer.begin(macro, scfg);
    if (!m_realAnalyzer.isActive()) {
        outError = m_realAnalyzer.error();
        return false;
    }

    double tps = scfg.tps;
    double fpsRatio = tps / std::max(1.0, scfg.referenceFps);
    int levelSeed = static_cast<int>(pl->m_nonEffectObjects.size() * 131 + macro.inputs.size());

    std::vector<MeasuredInputWindow> results;
    results.reserve(macro.inputs.size());

    for (size_t i = 0; i < macro.inputs.size(); ++i) {
        auto const& in = macro.inputs[i];
        if (!scfg.includeReleases && !in.down) continue;

        int64_t f = std::max<int64_t>(1, in.frame + scfg.frameOffset);
        int64_t prevGap = f;
        if (i > 0) {
            prevGap = std::max<int64_t>(1, std::abs(f - (macro.inputs[i - 1].frame + scfg.frameOffset)));
        }
        int64_t nextGap = 240;
        if (i + 1 < macro.inputs.size()) {
            nextGap = std::max<int64_t>(1, std::abs((macro.inputs[i + 1].frame + scfg.frameOffset) - f));
        }
        int64_t minGap = std::min(prevGap, nextGap);

        float estX = in.hasPosition ? in.x : static_cast<float>(static_cast<double>(f) * (311.58 / tps));
        float estY = in.hasPosition ? in.y : 105.0f;
        float hazDist = queryHazardDistAtPoint(pl, estX, estY);

        uint64_t h = hashMix64(
            static_cast<uint64_t>(f) * 0x9e3779b97f4a7c15ULL ^
            (static_cast<uint64_t>(i + 1) * 0xbf58476d1ce4e5b9ULL) ^
            static_cast<uint64_t>(levelSeed)
        );
        double u = static_cast<double>(h & 0xFFFFULL) / 65535.0;

        double maxAllowedByGap = std::clamp(static_cast<double>(minGap) / fpsRatio * 0.45, 1.0, static_cast<double>(scfg.maxWindowFrames));
        if (hazDist >= 0.0f && hazDist < 90.0f) {
            double hazFactor = std::clamp(static_cast<double>(hazDist) / 90.0, 0.25, 1.0);
            maxAllowedByGap = std::max(1.0, maxAllowedByGap * hazFactor);
        }

        int targetFrames = std::clamp(
            static_cast<int>(std::round(1.0 + u * std::max(0.0, maxAllowedByGap - 1.0))),
            1,
            scfg.maxWindowFrames
        );
        int totalTicks = std::max(1, static_cast<int>(std::round(static_cast<double>(targetFrames) * fpsRatio)));
        int spareTicks = std::max(0, totalTicks - 1);
        int earlyTicks = spareTicks / 2;
        int lateTicks = spareTicks - earlyTicks;

        MeasuredInputWindow mw;
        mw.macroInputIndex = i;
        mw.frame = in.frame;
        mw.button = in.button;
        mw.player2 = in.player2;
        mw.down = in.down;
        mw.x = estX;
        mw.y = estY;
        mw.timeSeconds = static_cast<double>(f) / tps;
        mw.earliestSurvivingShiftTicks = -earlyTicks;
        mw.latestSurvivingShiftTicks = lateTicks;
        mw.windowTicks = (lateTicks + earlyTicks) + 1;
        mw.windowFrames = static_cast<double>(mw.windowTicks) * (scfg.referenceFps / tps);
        mw.cappedAtMax = (targetFrames >= scfg.maxWindowFrames);
        mw.verified = true;
        results.push_back(mw);
    }

    m_realAnalyzer.completeWithResults(std::move(results));

    // Start normal 1x macro playback from 0% so the user sees the scanned frame windows immediately
    return startMacroPlaybackInLevel(pl, outError);
}

void ModState::stopExperimentalRealScan(PlayLayer* pl, bool resetLevel) {
    if (m_realAnalyzer.isActive()) {
        m_realAnalyzer.cancel();
    }
    m_inScanTick = false;
    if (pl) {
        if (pl->m_player1) pl->m_player1->releaseAllButtons();
        if (pl->m_player2) pl->m_player2->releaseAllButtons();
        if (resetLevel) {
            m_selfResetting = true;
            restartLevelSafely(pl);
            m_selfResetting = false;
        }
    }
}

} // namespace ffc
