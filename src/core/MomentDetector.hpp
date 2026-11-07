#pragma once

#include "Categories.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ffc {

enum class Gamemode : uint8_t {
    Cube = 0,
    Ship = 1,
    Ball = 2,
    Ufo = 3,
    Wave = 4,
    Robot = 5,
    Spider = 6,
    Swing = 7,
};

enum class SensitivityPreset : uint8_t {
    Selective = 0,   // Stricter: requires higher hazard/action salience
    Balanced = 1,    // Default: filters open-space fluff while preserving rapid sequences & meaningful jumps
    Permissive = 2,  // Labels almost all distinct non-duplicate inputs
};

enum class ReleaseFilterMode : uint8_t {
    SmartGamemode = 0, // Default: Wave, Ship, active Dash orb, rising Robot jump releases only
    PressesOnly = 1,   // Never label button releases
    AllReleases = 2,   // Consider releases in all gamemodes
};

enum class FilterReason : uint8_t {
    Accepted = 0,
    ModDisabled = 1,
    NoEnabledCategories = 2,
    DuplicateEvent = 3,
    DualMirrorDuplicate = 4,
    IgnoredReleaseForGamemode = 5,
    SpatialProximityTooClose = 6,
    BelowSensitivityThreshold = 7,
    DensityRateLimited = 8,
};

struct GameplayInputEvent {
    int64_t tick = 0;               // Physics tick (m_currentProgress)
    double timeSeconds = 0.0;       // Level time in seconds
    uint8_t button = 1;             // 1 = Jump, 2 = Left, 3 = Right
    bool isDown = true;             // true = Press, false = Release
    bool isPlayer2 = false;         // true = Player 2
    bool isDualMode = false;        // Level currently in dual mode
    bool isTwoPlayerMode = false;   // Level has independent 2-player controls enabled

    // World / object-layer coordinates of the player at the moment of input
    float x = 0.0f;
    float y = 0.0f;

    // Gameplay physics context
    double yVelocity = 0.0;
    float playerSpeed = 0.9f;
    bool isMini = false;
    bool isUpsideDown = false;
    bool isOnGround = true;
    bool isDashing = false;
    bool touchedRingOrPortal = false;
    Gamemode gamemode = Gamemode::Cube;

    // Nearest hazard (spike/saw) and lethal solid wall clearance in level units (30 units = 1 block).
    // If unknown/unavailable (e.g. unit test without scene), set to < 0 (-1.0f).
    // If in a live level with no nearby hazard/wall, set to a large value (e.g. 9999.0f).
    float nearestHazardUnits = -1.0f;
    float nearestSolidUnits = -1.0f;
    bool isNearLethalObstacle = false;

    // Optional pre-measured real window in frames (used only when Experimental Real Mode has a verified measurement)
    bool hasRealMeasurement = false;
    double realWindowFrames = 0.0;
};

struct DetectionConfig {
    SensitivityPreset sensitivity = SensitivityPreset::Balanced;
    ReleaseFilterMode releaseFilter = ReleaseFilterMode::SmartGamemode;

    // Better Estimate mode: only places a circle when the player is close to a hazard
    // or a lethal wall that would kill the player on impact, then picks a random enabled frame window.
    bool betterEstimate = false;

    // Minimum world-space distance (in GD units; 30 = 1 block) between two circles of the same player
    float minDistanceUnits = 18.0f;

    // Maximum moments per second in a 1.0s sliding window (with burst allowance for rapid sequences)
    float maxMomentsPerSecond = 8.0f;

    // Burst allowance: number of rapid consecutive inputs allowed before sliding-window throttling kicks in
    int burstAllowance = 4;

    // Effective hazard proximity scale in level units (default 135 units = 4.5 blocks)
    float proximityScaleUnits = 135.0f;

    // Fine sensitivity bias [0.0 = most selective, 0.5 = default, 1.0 = most permissive]
    float sensitivityBias = 0.5f;
};

struct MomentResult {
    bool accepted = false;
    FilterReason reason = FilterReason::Accepted;

    int categoryFrames = 1;         // Chosen enabled category (e.g. 1..12 or custom)
    std::string label;              // e.g. "1F", "4F", "12F"
    RGBColor color;                 // Matching circle & label color

    float x = 0.0f;                 // Object-layer X coordinate
    float y = 0.0f;                 // Object-layer Y coordinate
    bool isDown = true;
    bool isPlayer2 = false;
    uint8_t button = 1;
    int64_t tick = 0;
    double timeSeconds = 0.0;
    float salience = 0.0f;          // Computed event salience [0..1]
    bool isSimulatedEstimate = true;
};

class MomentDetector {
public:
    MomentDetector();

    void reset();

    void setConfig(DetectionConfig const& cfg) { m_cfg = cfg; }
    DetectionConfig const& config() const { return m_cfg; }

    MomentResult evaluate(GameplayInputEvent const& ev, CategoryConfig const& categories);

    uint64_t totalAccepted() const { return m_totalAccepted; }
    static char const* reasonToString(FilterReason reason);

private:
    bool isReleaseMeaningful(GameplayInputEvent const& ev, double holdDurationSeconds) const;
    float computeSalience(GameplayInputEvent const& ev, double interInputSeconds, double holdDurationSeconds) const;
    int estimateFakeCategory(GameplayInputEvent const& ev, CategoryConfig const& categories,
                             double interInputSeconds, double holdDurationSeconds);
    int pickBetterEstimateCategory(GameplayInputEvent const& ev, CategoryConfig const& categories);

    DetectionConfig m_cfg;

    // Per-player (0=P1, 1=P2), per-button (1..3) state tracking to prevent duplicate events
    bool m_buttonHeld[2][4] = {};
    int64_t m_lastEventTick[2][4][2] = {}; // [player][button][isDown]
    double m_lastPressTime[2][4] = {};
    double m_lastAnyInputTime[2] = {-10.0, -10.0};

    // Dual-mode single-click mirror suppression
    int64_t m_lastP1Tick[4][2] = {};
    double m_lastP1Time[4][2] = {};

    // Last accepted marker location per player to prevent overlapping circles at identical coordinates
    bool m_hasLastCircle[2] = {false, false};
    bool m_lastCircleWasGround[2] = {false, false};
    float m_lastCircleX[2] = {0.0f, 0.0f};
    float m_lastCircleY[2] = {0.0f, 0.0f};
    double m_lastCircleTime[2] = {-10.0, -10.0};

    // Sliding 1.0s window of accepted moment timestamps
    std::deque<double> m_recentAcceptedTimes;
    uint64_t m_totalAccepted = 0;
    uint64_t m_sequenceCounter = 0;
    int m_lastPickedCategory = -1;
};

} // namespace ffc
