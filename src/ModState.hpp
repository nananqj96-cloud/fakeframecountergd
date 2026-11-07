#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include "core/Categories.hpp"
#include "core/Macro.hpp"
#include "core/MacroPlayer.hpp"
#include "core/MomentDetector.hpp"
#include "core/RealAnalyzer.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ffc {

struct PreInputPlayerState {
    bool valid = false;
    bool wasOnGround = false;
    bool wasDashing = false;
    bool wasTouchingRing = false;
};

enum class WindowCalcMode : uint8_t {
    BetterEstimate = 0,
    FakeEstimate = 1,
    RealAnalysis = 2,
};

class ModState {
public:
    static ModState& get();

    void syncFromGeodeSettings();
    void saveCategoryConfig();

    bool isModEnabled() const { return m_enabled; }
    void setModEnabled(bool en);

    WindowCalcMode calcMode() const { return m_calcMode; }
    void setCalcMode(WindowCalcMode mode);
    void cycleCalcMode();

    bool isBetterEstimateMode() const { return m_calcMode == WindowCalcMode::BetterEstimate; }
    bool isExperimentalRealMode() const { return m_calcMode == WindowCalcMode::RealAnalysis; }
    void setExperimentalRealMode(bool realMode);

    bool isOverlayHiddenByKey() const { return m_hiddenByKey; }
    void toggleOverlayHiddenByKey() { m_hiddenByKey = !m_hiddenByKey; }

    CategoryConfig& categories() { return m_categories; }
    CategoryConfig const& categories() const { return m_categories; }

    MomentDetector& detector() { return m_detector; }
    MomentDetector const& detector() const { return m_detector; }

    MacroPlayer& macroPlayer() { return m_macroPlayer; }
    MacroPlayer const& macroPlayer() const { return m_macroPlayer; }

    RealAnalyzer& realAnalyzer() { return m_realAnalyzer; }
    RealAnalyzer const& realAnalyzer() const { return m_realAnalyzer; }

    // Per-attempt statistics for the top-right counter
    void resetAttemptCounts();
    uint64_t totalMomentsThisAttempt() const { return m_attemptTotal; }
    int countForCategory(int frames) const;
    std::map<int, int> const& categoryCounts() const { return m_attemptCategoryCounts; }
    std::vector<MomentResult> const& attemptMoments() const { return m_attemptMoments; }

    // Process a player input (from live play or macro playback)
    MomentResult processPlayerInput(PlayLayer* pl, PlayerObject* player, bool isDown, int button, bool isPlayer2,
                                    std::optional<size_t> macroInputIndex = std::nullopt,
                                    PreInputPlayerState preState = {});

    // Macro playback & Experimental Real Scan helpers
    bool startMacroPlaybackInLevel(PlayLayer* pl, std::string& outError);
    void stopMacroPlaybackInLevel(PlayLayer* pl);
    static void restartLevelSafely(PlayLayer* pl);

    bool startExperimentalRealScan(PlayLayer* pl, std::string& outError);
    void stopExperimentalRealScan(PlayLayer* pl, bool resetLevel = false);
    bool isScanningLevel(GJBaseGameLayer* layer) const { (void)layer; return false; }

    bool isInjectingInput() const { return m_injectingInput; }
    void setInjectingInput(bool inj) { m_injectingInput = inj; }
    bool isResettingLevel() const { return m_resettingLevel; }
    void setResettingLevel(bool r) { m_resettingLevel = r; }
    bool isInScanTick() const { return m_inScanTick; }
    bool isSelfResetting() const { return m_selfResetting; }

    static cocos2d::ccColor3B toCCColor3B(RGBColor c) {
        return {c.r, c.g, c.b};
    }

    static cocos2d::CCPoint playerObjectLayerPos(PlayLayer* pl, PlayerObject* player);

private:
    ModState();

    Gamemode detectGamemode(PlayerObject* player) const;
    void computeObstacleClearances(PlayLayer* pl, PlayerObject* player, float& outHazardDist, float& outSolidDist, bool& outNearLethal) const;

    bool m_enabled = true;
    WindowCalcMode m_calcMode = WindowCalcMode::BetterEstimate;
    bool m_experimentalRealMode = false;
    bool m_hiddenByKey = false;
    bool m_injectingInput = false;
    bool m_resettingLevel = false;
    bool m_inScanTick = false;
    bool m_selfResetting = false;

    CategoryConfig m_categories;
    MomentDetector m_detector;
    MacroPlayer m_macroPlayer;
    RealAnalyzer m_realAnalyzer;

    uint64_t m_attemptTotal = 0;
    std::map<int, int> m_attemptCategoryCounts;
    std::vector<MomentResult> m_attemptMoments;
};

} // namespace ffc
