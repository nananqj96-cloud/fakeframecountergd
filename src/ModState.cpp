#include "ModState.hpp"
#include "Overlay.hpp"
#include "SoundManager.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

namespace ffc {

ModState& ModState::get() {
    static ModState instance;
    return instance;
}

ModState::ModState() {
    auto savedCats = Mod::get()->getSavedValue<std::string>("category-config-v1", "");
    if (!savedCats.empty()) {
        m_categories.deserialize(savedCats);
    }
    syncFromGeodeSettings();
}

void ModState::syncFromGeodeSettings() {
    auto* mod = Mod::get();
    m_enabled = mod->getSettingValue<bool>("enabled");

    auto modeStr = mod->getSettingValue<std::string>("analysis-mode");
    m_experimentalRealMode = (modeStr.find("Real") != std::string::npos);

    DetectionConfig dcfg = m_detector.config();
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

void ModState::setExperimentalRealMode(bool realMode) {
    m_experimentalRealMode = realMode;
    Mod::get()->setSettingValue<std::string>(
        "analysis-mode",
        realMode ? "Real Analysis (Experimental Macro Scan)" : "Fake / Visual Estimate (Simulated)"
    );
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

void ModState::computeObstacleClearances(PlayLayer* pl, PlayerObject* player, float& outHazardDist, float& outSolidDist) const {
    outHazardDist = -1.0f;
    outSolidDist = -1.0f;
    if (!pl || !player) return;

    CCPoint pPos = player->getPosition();
    float bestHazSq = 1e12f;
    bool foundHaz = false;

    size_t hazCount = std::min<size_t>(
        pl->m_hazardCollisionObjects.size(),
        static_cast<size_t>(std::clamp(pl->m_hazardCollisionObjectsCount, 0, 256))
    );
    for (size_t i = 0; i < hazCount; ++i) {
        GameObject* obj = pl->m_hazardCollisionObjects[i];
        if (!obj) continue;
        CCPoint oPos = obj->getPosition();
        float dx = oPos.x - pPos.x;
        float dy = oPos.y - pPos.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestHazSq) {
            bestHazSq = dSq;
            foundHaz = true;
        }
    }
    if (foundHaz) {
        outHazardDist = std::max(0.0f, std::sqrt(bestHazSq) - 15.0f);
    }

    float bestSolSq = 1e12f;
    bool foundSol = false;
    size_t solCount = std::min<size_t>(
        pl->m_solidCollisionObjects.size(),
        static_cast<size_t>(std::clamp(pl->m_solidCollisionObjectsCount, 0, 256))
    );
    for (size_t i = 0; i < solCount; ++i) {
        GameObject* obj = pl->m_solidCollisionObjects[i];
        if (!obj) continue;
        CCPoint oPos = obj->getPosition();
        float dx = oPos.x - pPos.x;
        float dy = oPos.y - pPos.y;
        // Skip the floor block directly supporting the grounded player
        if (player->m_isOnGround && std::abs(dx) < 34.0f && dy < 0.0f && dy > -36.0f) {
            continue;
        }
        float dSq = dx * dx + dy * dy;
        if (dSq < bestSolSq) {
            bestSolSq = dSq;
            foundSol = true;
        }
    }
    if (foundSol) {
        outSolidDist = std::max(0.0f, std::sqrt(bestSolSq) - 15.0f);
    }
}

MomentResult ModState::processPlayerInput(PlayLayer* pl, PlayerObject* player, bool isDown, int button, bool isPlayer2,
                                          std::optional<size_t> macroInputIndex) {
    MomentResult rejected{};
    if (!m_enabled || !pl || !player) {
        rejected.reason = FilterReason::ModDisabled;
        return rejected;
    }
    if (player->m_isDead || pl->m_playerDied || pl->m_levelEndAnimationStarted) {
        rejected.reason = FilterReason::DuplicateEvent;
        return rejected;
    }

    GameplayInputEvent ev{};
    ev.tick = static_cast<int64_t>(pl->m_gameState.m_currentProgress);
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
    ev.isOnGround = player->m_isOnGround;
    ev.isDashing = player->m_isDashing;
    ev.touchedRingOrPortal = player->m_touchedRing || player->m_stateRingJump || player->m_touchedPad;
    ev.gamemode = detectGamemode(player);

    computeObstacleClearances(pl, player, ev.nearestHazardUnits, ev.nearestSolidUnits);

    // If Experimental Real Mode is active and we have a verified scan measurement for this macro input, use it
    if (m_experimentalRealMode) {
        if (macroInputIndex.has_value()) {
            if (auto const* mw = m_realAnalyzer.findMatchForInput(*macroInputIndex)) {
                ev.hasRealMeasurement = true;
                ev.realWindowFrames = mw->windowFrames;
            }
        } else if (!m_realAnalyzer.results().empty()) {
            // In live play after a scan, match closest scanned event within 55 units & same press/release state
            MeasuredInputWindow const* bestMatch = nullptr;
            float bestDist = 55.0f;
            for (auto const& r : m_realAnalyzer.results()) {
                if (r.down != isDown || r.player2 != isPlayer2) continue;
                float d = std::hypot(r.x - ev.x, r.y - ev.y);
                if (d < bestDist) {
                    bestDist = d;
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

bool ModState::startMacroPlaybackInLevel(PlayLayer* pl, std::string& outError) {
    if (!pl) {
        outError = "Open a level first to play back a macro.";
        return false;
    }
    if (!m_macroPlayer.hasMacro()) {
        outError = "Import a macro file first (.mhr, .mhr.json, .gdr2, .gdr, .json, .zbf, .re3, .xd, .txt).";
        return false;
    }
    if (m_realAnalyzer.isActive()) {
        stopExperimentalRealScan(pl);
    }

    m_macroPlayer.startPlayback();
    m_selfResetting = true;
    if (pl->m_player1) pl->m_player1->releaseAllButtons();
    if (pl->m_player2) pl->m_player2->releaseAllButtons();
    pl->resetLevelFromStart();
    m_selfResetting = false;

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
    if (!m_macroPlayer.hasMacro()) {
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

    stopMacroPlaybackInLevel(pl);

    RealScanConfig scfg;
    scfg.tps = m_macroPlayer.effectiveTps();
    scfg.referenceFps = 60.0;
    scfg.maxWindowFrames = 12;
    scfg.frameOffset = m_macroPlayer.frameOffset();
    scfg.includeReleases = (m_detector.config().releaseFilter != ReleaseFilterMode::PressesOnly);

    m_scanLayer = pl;
    m_scanDead = false;
    m_scanComplete = false;
    m_realAnalyzer.begin(*m_macroPlayer.macro(), scfg);
    if (!m_realAnalyzer.isActive()) {
        outError = m_realAnalyzer.error();
        m_scanLayer = nullptr;
        return false;
    }
    return true;
}

void ModState::stopExperimentalRealScan(PlayLayer* pl) {
    if (!m_realAnalyzer.isActive()) return;
    m_realAnalyzer.cancel();
    PlayLayer* target = pl ? pl : m_scanLayer;
    m_scanLayer = nullptr;
    m_inScanTick = false;
    if (target) {
        m_selfResetting = true;
        if (target->m_player1) target->m_player1->releaseAllButtons();
        if (target->m_player2) target->m_player2->releaseAllButtons();
        target->resetLevelFromStart();
        m_selfResetting = false;
    }
}

bool ModState::isScanningLevel(GJBaseGameLayer* layer) const {
    return m_realAnalyzer.isActive() && m_scanLayer && static_cast<GJBaseGameLayer*>(m_scanLayer) == layer;
}

void ModState::onScanProcessCommands(GJBaseGameLayer* layer, int64_t progress) {
    if (!m_inScanTick || !isScanningLevel(layer)) return;

    class LiveBackend final : public SimulationBackend {
    public:
        ModState& state;
        PlayLayer* pl;
        LiveBackend(ModState& s, PlayLayer* p) : state(s), pl(p) {}
        void restartFromZero() override {}
        bool saveCheckpoint() override { return false; }
        bool loadCheckpoint() override { return false; }
        int64_t currentProgress() const override { return static_cast<int64_t>(pl->m_gameState.m_currentProgress); }
        void stepSingleTick() override {}
        SimSnapshot sampleState(bool p2) const override {
            PlayerObject* p = p2 ? pl->m_player2 : pl->m_player1;
            if (!p) return {};
            CCPoint wp = ModState::playerObjectLayerPos(pl, p);
            return {wp.x, wp.y, p->m_yVelocity, pl->m_gameState.m_levelTime, true};
        }
        bool isDead() const override { return state.m_scanDead; }
        bool isLevelComplete() const override { return state.m_scanComplete || pl->m_levelEndAnimationStarted; }
        void clearRunFlags() override {}
        void injectInput(uint8_t button, bool player2, bool down) override {
            state.m_injectingInput = true;
            pl->handleButton(down, static_cast<int>(button), !player2);
            state.m_injectingInput = false;
        }
        void releaseAllButtons() override {}
    };

    LiveBackend b(*this, m_scanLayer);
    m_realAnalyzer.onProcessCommands(b, progress);
}

void ModState::runScanFrame(PlayLayer* pl, std::function<void(float)> const& stepFn) {
    if (!m_realAnalyzer.isActive() || pl != m_scanLayer) return;

    class FrameBackend final : public SimulationBackend {
    public:
        ModState& state;
        PlayLayer* pl;
        std::function<void(float)> const& stepFn;
        double tps;

        FrameBackend(ModState& s, PlayLayer* p, std::function<void(float)> const& fn, double t)
            : state(s), pl(p), stepFn(fn), tps(t) {}

        void restartFromZero() override {
            state.m_selfResetting = true;
            pl->m_isPracticeMode = false;
            pl->resetLevelFromStart();
            pl->m_levelEndAnimationStarted = false;
            state.m_selfResetting = false;
        }
        bool saveCheckpoint() override { return false; }
        bool loadCheckpoint() override { return false; }
        int64_t currentProgress() const override { return static_cast<int64_t>(pl->m_gameState.m_currentProgress); }
        void stepSingleTick() override {
            state.m_inScanTick = true;
            stepFn(static_cast<float>(1.0 / tps));
            state.m_inScanTick = false;
        }
        SimSnapshot sampleState(bool p2) const override {
            PlayerObject* p = p2 ? pl->m_player2 : pl->m_player1;
            if (!p) return {};
            CCPoint wp = ModState::playerObjectLayerPos(pl, p);
            return {wp.x, wp.y, p->m_yVelocity, pl->m_gameState.m_levelTime, true};
        }
        bool isDead() const override { return state.m_scanDead; }
        bool isLevelComplete() const override { return state.m_scanComplete || pl->m_levelEndAnimationStarted; }
        void clearRunFlags() override {
            state.m_scanDead = false;
            state.m_scanComplete = false;
        }
        void injectInput(uint8_t button, bool player2, bool down) override {
            state.m_injectingInput = true;
            pl->handleButton(down, static_cast<int>(button), !player2);
            state.m_injectingInput = false;
        }
        void releaseAllButtons() override {
            if (pl->m_player1) pl->m_player1->releaseAllButtons();
            if (pl->m_player2) pl->m_player2->releaseAllButtons();
        }
    };

    FrameBackend backend(*this, pl, stepFn, m_macroPlayer.effectiveTps());
    auto t0 = std::chrono::steady_clock::now();
    while (m_realAnalyzer.isActive()) {
        m_realAnalyzer.step(backend);
        auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (elapsedMs >= 12.0) break;
    }

    if (!m_realAnalyzer.isActive()) {
        auto phase = m_realAnalyzer.phase();
        auto err = m_realAnalyzer.error();
        size_t count = m_realAnalyzer.results().size();
        m_scanLayer = nullptr;
        m_selfResetting = true;
        pl->resetLevelFromStart();
        m_selfResetting = false;
        if (phase == RealAnalyzer::Phase::Completed) {
            Notification::create(fmt::format("FFC Real Scan complete ({} inputs measured)", count), NotificationIcon::Success)->show();
        } else if (phase == RealAnalyzer::Phase::Failed) {
            Notification::create(fmt::format("FFC Scan failed: {}", err), NotificationIcon::Error)->show();
        }
    }
}

} // namespace ffc
