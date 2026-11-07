#include "MomentDetector.hpp"

#include <algorithm>
#include <cmath>

namespace ffc {

namespace {

uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

double hashToUnit(uint64_t seed) {
    uint64_t h = splitmix64(seed);
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace

MomentDetector::MomentDetector() {
    reset();
}

void MomentDetector::reset() {
    for (int p = 0; p < 2; ++p) {
        for (int b = 0; b < 4; ++b) {
            m_buttonHeld[p][b] = false;
            m_lastEventTick[p][b][0] = -999999;
            m_lastEventTick[p][b][1] = -999999;
            m_lastPressTime[p][b] = -10.0;
        }
        m_lastAnyInputTime[p] = -10.0;
        m_hasLastCircle[p] = false;
        m_lastCircleWasGround[p] = false;
        m_lastCircleX[p] = 0.0f;
        m_lastCircleY[p] = 0.0f;
        m_lastCircleTime[p] = -10.0;
    }
    for (int b = 0; b < 4; ++b) {
        m_lastP1Tick[b][0] = -999999;
        m_lastP1Tick[b][1] = -999999;
        m_lastP1Time[b][0] = -10.0;
        m_lastP1Time[b][1] = -10.0;
    }
    m_recentAcceptedTimes.clear();
    m_totalAccepted = 0;
    m_sequenceCounter = 0;
    m_lastPickedCategory = -1;
}

char const* MomentDetector::reasonToString(FilterReason reason) {
    switch (reason) {
        case FilterReason::Accepted: return "Accepted";
        case FilterReason::ModDisabled: return "Mod disabled";
        case FilterReason::NoEnabledCategories: return "No enabled categories";
        case FilterReason::DuplicateEvent: return "Duplicate same-tick/state event";
        case FilterReason::DualMirrorDuplicate: return "Dual-mode mirror duplicate";
        case FilterReason::IgnoredReleaseForGamemode: return "Non-meaningful release for gamemode";
        case FilterReason::SpatialProximityTooClose: return "Too close to previous circle";
        case FilterReason::BelowSensitivityThreshold: return "Below sensitivity threshold";
        case FilterReason::DensityRateLimited: return "Rate-limited by max moment density";
    }
    return "Unknown";
}

bool MomentDetector::isReleaseMeaningful(GameplayInputEvent const& ev, double holdDurationSeconds) const {
    (void)holdDurationSeconds;
    if (ev.isDown) return true;
    if (m_cfg.releaseFilter == ReleaseFilterMode::PressesOnly) return false;
    if (m_cfg.releaseFilter == ReleaseFilterMode::AllReleases) return true;

    // SmartGamemode:
    // Wave and Ship always change vertical trajectory on release.
    if (ev.gamemode == Gamemode::Wave || ev.gamemode == Gamemode::Ship) {
        return true;
    }
    // Releasing an active dash orb ring ends the dash immediately.
    if (ev.isDashing) {
        return true;
    }
    // Platformer left/right releases stop horizontal movement.
    if (ev.button == 2 || ev.button == 3) {
        return true;
    }
    return false;
}

float MomentDetector::computeSalience(GameplayInputEvent const& ev, double interInputSeconds, double holdDurationSeconds) const {
    float score = ev.isDown ? 0.34f : 0.24f;

    float proxScale = std::max(30.0f, m_cfg.proximityScaleUnits);
    if (ev.nearestHazardUnits >= 0.0f) {
        float h = std::exp(-ev.nearestHazardUnits / proxScale);
        score += 0.44f * h;
        // Penalty for wide-open space far from any hazard when not hitting an orb/portal
        if (ev.nearestHazardUnits > proxScale * 2.1f && !ev.touchedRingOrPortal && !ev.isDashing) {
            score -= 0.18f;
        }
    } else {
        // When hazard array is not populated (e.g. pure macro timeline without scene objects),
        // provide a balanced baseline so normal gameplay moments work seamlessly.
        score += 0.22f;
    }

    if (ev.nearestSolidUnits >= 0.0f) {
        float s = std::exp(-ev.nearestSolidUnits / (proxScale * 0.85f));
        score += 0.30f * s;
    }

    if (ev.touchedRingOrPortal || ev.isDashing) {
        score += 0.32f;
    }

    float vyNorm = std::clamp(static_cast<float>(std::abs(ev.yVelocity)) / 15.0f, 0.0f, 1.0f);
    score += 0.14f * vyNorm;

    if (ev.isMini) score += 0.07f;
    if (ev.playerSpeed > 1.05f) score += 0.06f;

    // Micro-clicks and rapid deliberate sequences
    if (!ev.isDown && holdDurationSeconds > 0.0 && holdDurationSeconds <= 0.055) {
        score += 0.15f;
    }
    if (interInputSeconds > 0.018 && interInputSeconds <= 0.15) {
        score += 0.10f;
    }

    // Gentle straight-fly / ship hovering far from hazards shouldn't spam every tap
    if ((ev.gamemode == Gamemode::Ship || ev.gamemode == Gamemode::Ufo) &&
        ev.nearestHazardUnits > proxScale * 1.4f &&
        !ev.touchedRingOrPortal &&
        interInputSeconds < 0.22) {
        score -= 0.22f;
    }

    return std::clamp(score, 0.0f, 1.0f);
}

int MomentDetector::estimateFakeCategory(GameplayInputEvent const& ev, CategoryConfig const& categories,
                                         double interInputSeconds, double holdDurationSeconds) {
    auto enabled = categories.enabledCategoriesAscending();
    if (enabled.empty()) return -1;
    if (enabled.size() == 1) return enabled.front().frames;

    // 1. Compute physical/gameplay tightness in [0.0 = tightest (1F), 1.0 = widest (12F)]
    float proxScale = std::max(30.0f, m_cfg.proximityScaleUnits);
    double hazardTight = 0.50;
    if (ev.nearestHazardUnits >= 0.0f) {
        // Map [0 .. 2.2 * proxScale] smoothly onto [0.0 .. 1.0] using 1 - exp(-d / scale)
        hazardTight = 1.0 - std::exp(-static_cast<double>(ev.nearestHazardUnits) / static_cast<double>(proxScale * 0.85f));
    }

    double timingTight = 0.50;
    double refDt = (!ev.isDown && holdDurationSeconds > 0.0) ? holdDurationSeconds : interInputSeconds;
    if (refDt > 0.0 && refDt < 2.0) {
        timingTight = 1.0 - std::exp(-refDt / 0.16);
    }

    double speedTight = 0.55;
    double vy = std::abs(ev.yVelocity);
    if (vy > 0.01) {
        speedTight = 1.0 - std::clamp(vy / 22.0, 0.0, 0.75);
    }
    if (ev.isMini) speedTight *= 0.82;
    if (ev.touchedRingOrPortal || ev.isDashing) speedTight *= 0.80;

    double physicsScore = std::clamp(0.48 * hazardTight + 0.30 * timingTight + 0.22 * speedTight, 0.0, 1.0);

    // 2. Low-discrepancy Golden Ratio (Weyl) + SplitMix64 dispersion
    // Ensures that even when obstacle distances are uniform, the generated categories
    // never get stuck on the same number and cover all enabled categories cleanly.
    ++m_sequenceCounter;
    uint64_t qx = static_cast<uint64_t>(std::llround(ev.x * 2.0f));
    uint64_t qy = static_cast<uint64_t>(std::llround(ev.y * 2.0f));
    uint64_t seed = (static_cast<uint64_t>(ev.tick) * 0x9e3779b97f4a7c15ULL)
                  ^ (qx * 0xbf58476d1ce4e5b9ULL)
                  ^ (qy * 0x94d049bb133111ebULL)
                  ^ (m_sequenceCounter * 0x2545f4914f6cdd1dULL)
                  ^ (ev.isDown ? 0x1ULL : 0x2ULL);

    constexpr double kGoldenConj = 0.6180339887498948482;
    double weyl = std::fmod(static_cast<double>(m_sequenceCounter) * kGoldenConj + 0.25 * hashToUnit(seed), 1.0);
    if (weyl < 0.0) weyl += 1.0;
    weyl = std::clamp(weyl, 1e-6, 0.999999);

    // Power-law quantile bijection u = weyl^exponent on (0, 1):
    // - Always maps onto the full interval (0, 1) so 1F and 12F are never clipped.
    // - When physicsScore < 0.5 (tight hazard/fast click), exponent > 1.0 biases u toward 0.0 (1F-4F).
    // - When physicsScore > 0.5 (open clearance), exponent < 1.0 biases u toward 1.0 (9F-12F).
    double centered = std::clamp(physicsScore - 0.50, -0.45, 0.45);
    double strength = (ev.nearestHazardUnits >= 0.0f) ? 0.75 : 0.40;
    double exponent = std::exp(-centered * strength);
    double u = std::clamp(std::pow(weyl, exponent), 0.0, 0.999999);

    size_t idx = static_cast<size_t>(u * static_cast<double>(enabled.size()));
    if (idx >= enabled.size()) idx = enabled.size() - 1;
    return enabled[idx].frames;
}

int MomentDetector::pickBetterEstimateCategory(GameplayInputEvent const& ev, CategoryConfig const& categories) {
    auto enabled = categories.enabledCategoriesAscending();
    if (enabled.empty()) return -1;
    if (enabled.size() == 1) return enabled.front().frames;

    ++m_sequenceCounter;
    uint64_t qx = static_cast<uint64_t>(std::llround(ev.x * 3.0f));
    uint64_t qy = static_cast<uint64_t>(std::llround(ev.y * 3.0f));
    uint64_t seed = (static_cast<uint64_t>(ev.tick) * 0x9e3779b97f4a7c15ULL)
                  ^ (qx * 0xbf58476d1ce4e5b9ULL)
                  ^ (qy * 0x94d049bb133111ebULL)
                  ^ (m_sequenceCounter * 0x2545f4914f6cdd1dULL)
                  ^ (ev.isDown ? 0x517cc1b727220a95ULL : 0x94d049bb133111ebULL);

    uint64_t h = splitmix64(seed);
    size_t idx = static_cast<size_t>(h % enabled.size());
    if (enabled.size() > 1 && enabled[idx].frames == m_lastPickedCategory) {
        idx = (idx + 1 + static_cast<size_t>((h >> 16) % (enabled.size() - 1))) % enabled.size();
    }
    m_lastPickedCategory = enabled[idx].frames;
    return enabled[idx].frames;
}

MomentResult MomentDetector::evaluate(GameplayInputEvent const& ev, CategoryConfig const& categories) {
    MomentResult out;
    out.x = ev.x;
    out.y = ev.y;
    out.isDown = ev.isDown;
    out.isPlayer2 = ev.isPlayer2;
    out.button = ev.button;
    out.tick = ev.tick;
    out.timeSeconds = ev.timeSeconds;
    out.isSimulatedEstimate = !ev.hasRealMeasurement;

    auto enabledCats = categories.enabledCategoriesAscending();
    if (enabledCats.empty()) {
        out.accepted = false;
        out.reason = FilterReason::NoEnabledCategories;
        return out;
    }

    int p = ev.isPlayer2 ? 1 : 0;
    int b = std::clamp<int>(ev.button, 1, 3);
    int d = ev.isDown ? 1 : 0;

    // 1. Same-event & hold-state deduplication
    if (ev.tick > 0 && ev.tick == m_lastEventTick[p][b][d]) {
        out.accepted = false;
        out.reason = FilterReason::DuplicateEvent;
        return out;
    }
    if (ev.isDown && m_buttonHeld[p][b] && (ev.timeSeconds - m_lastPressTime[p][b]) < 0.008) {
        out.accepted = false;
        out.reason = FilterReason::DuplicateEvent;
        return out;
    }
    if (!ev.isDown && !m_buttonHeld[p][b]) {
        out.accepted = false;
        out.reason = FilterReason::DuplicateEvent;
        return out;
    }

    double holdDuration = (!ev.isDown && m_lastPressTime[p][b] >= 0.0)
        ? std::max(0.0, ev.timeSeconds - m_lastPressTime[p][b])
        : 0.0;
    double prevSameDirTime = ev.isDown ? m_lastPressTime[p][b] : m_lastAnyInputTime[p];
    double interInput = (prevSameDirTime >= 0.0)
        ? std::max(0.0, ev.timeSeconds - prevSameDirTime)
        : 1.0;

    // Update button state before filtering so releases always pair with presses
    m_buttonHeld[p][b] = ev.isDown;
    m_lastEventTick[p][b][d] = ev.tick;
    if (ev.isDown) {
        m_lastPressTime[p][b] = ev.timeSeconds;
    }
    m_lastAnyInputTime[p] = ev.timeSeconds;

    // 2. Single-input Dual Mode mirror suppression (one click drives both P1 and P2)
    if (!ev.isPlayer2) {
        m_lastP1Tick[b][d] = ev.tick;
        m_lastP1Time[b][d] = ev.timeSeconds;
    } else if (ev.isDualMode && !ev.isTwoPlayerMode) {
        if ((ev.tick > 0 && ev.tick == m_lastP1Tick[b][d]) ||
            std::abs(ev.timeSeconds - m_lastP1Time[b][d]) < 0.006) {
            out.accepted = false;
            out.reason = FilterReason::DualMirrorDuplicate;
            return out;
        }
    }

    // 3. Gamemode-aware release filtering
    if (!isReleaseMeaningful(ev, holdDuration)) {
        out.accepted = false;
        out.reason = FilterReason::IgnoredReleaseForGamemode;
        return out;
    }

    // 3b. Suppress airborne duplicate circles during a ground jump arc over a spike:
    // In ground-jump modes (Cube, Robot, Ball, Spider), once the player jumps from the ground,
    // keep ONLY the ground circle and ignore mid-air inputs unless touching an orb/ring or dash ring.
    bool isGroundMode = (ev.gamemode == Gamemode::Cube || ev.gamemode == Gamemode::Robot ||
                         ev.gamemode == Gamemode::Ball || ev.gamemode == Gamemode::Spider);
    if (isGroundMode && ev.button == 1 && !ev.isOnGround && !ev.touchedRingOrPortal && !ev.isDashing) {
        out.accepted = false;
        out.reason = !ev.isDown
            ? FilterReason::IgnoredReleaseForGamemode
            : FilterReason::SpatialProximityTooClose;
        return out;
    }

    // 4. Spatial proximity anti-stacking check
    if (m_hasLastCircle[p] && m_cfg.minDistanceUnits > 0.0f) {
        float dx = ev.x - m_lastCircleX[p];
        float dy = ev.y - m_lastCircleY[p];
        float distSq = dx * dx + dy * dy;
        float minDist = m_cfg.minDistanceUnits;
        // For release right after a press in Wave/Ship, allow slightly closer spacing (0.55x)
        if (!ev.isDown) minDist *= 0.55f;
        if (distSq < minDist * minDist && (ev.timeSeconds - m_lastCircleTime[p]) < 0.45) {
            out.accepted = false;
            out.reason = FilterReason::SpatialProximityTooClose;
            return out;
        }
    }

    // 5. Better Estimate mode vs Salience-based Fake Estimate mode
    float salience = computeSalience(ev, interInput, holdDuration);
    out.salience = salience;

    float threshold = 0.28f;
    if (m_cfg.betterEstimate && !ev.hasRealMeasurement) {
        float maxHazardRange = std::max(45.0f, m_cfg.proximityScaleUnits);
        float maxWallRange = std::max(40.0f, m_cfg.proximityScaleUnits * 0.90f);
        bool nearHazard = (ev.nearestHazardUnits >= 0.0f && ev.nearestHazardUnits <= maxHazardRange);
        bool nearLethalWall = (ev.nearestSolidUnits >= 0.0f && ev.nearestSolidUnits <= maxWallRange);
        bool nearThreat = ev.isNearLethalObstacle || nearHazard || nearLethalWall ||
                          ev.touchedRingOrPortal || ev.isDashing;
        if (!nearThreat) {
            out.accepted = false;
            out.reason = FilterReason::BelowSensitivityThreshold;
            return out;
        }
        out.salience = 0.85f;
    } else {
        float baseThreshold = 0.28f;
        switch (m_cfg.sensitivity) {
            case SensitivityPreset::Selective:  baseThreshold = 0.52f; break;
            case SensitivityPreset::Balanced:   baseThreshold = 0.28f; break;
            case SensitivityPreset::Permissive: baseThreshold = 0.00f; break;
        }
        // Apply continuous sensitivityBias [0.0 .. 1.0] (0.5 is neutral)
        float biasAdjust = (0.5f - std::clamp(m_cfg.sensitivityBias, 0.0f, 1.0f)) * 0.24f;
        threshold = std::clamp(baseThreshold + biasAdjust, 0.0f, 0.85f);
        if (m_cfg.sensitivity == SensitivityPreset::Permissive && m_cfg.sensitivityBias >= 0.5f) {
            threshold = 0.0f;
        }

        if (salience < threshold) {
            out.accepted = false;
            out.reason = FilterReason::BelowSensitivityThreshold;
            return out;
        }
    }

    // 6. Burst-aware sliding-window density limiter
    while (!m_recentAcceptedTimes.empty() && (ev.timeSeconds - m_recentAcceptedTimes.front()) > 1.0) {
        m_recentAcceptedTimes.pop_front();
    }

    int maxPerSec = std::max(1, static_cast<int>(std::round(m_cfg.maxMomentsPerSecond)));
    int recentInWindow = static_cast<int>(m_recentAcceptedTimes.size());
    if (recentInWindow >= maxPerSec) {
        out.accepted = false;
        out.reason = FilterReason::DensityRateLimited;
        return out;
    }

    // If beyond burst allowance within 1.0s, require higher salience so rapid spam doesn't clutter
    int burstAllow = std::max(m_cfg.burstAllowance, static_cast<int>(std::round(m_cfg.maxMomentsPerSecond * 0.70f)));
    if (!m_cfg.betterEstimate && recentInWindow >= burstAllow && m_cfg.sensitivity != SensitivityPreset::Permissive) {
        float burstExtra = 0.07f * static_cast<float>(recentInWindow - burstAllow + 1);
        if (salience < std::min(0.78f, threshold + burstExtra)) {
            out.accepted = false;
            out.reason = FilterReason::DensityRateLimited;
            return out;
        }
    }

    // 7. Assign frame-window category (Real measurement if provided, Better Estimate random pick, or Fake/Visual estimate)
    int chosenFrames = -1;
    if (ev.hasRealMeasurement && ev.realWindowFrames > 0.0) {
        chosenFrames = categories.snapToNearestEnabledCategory(ev.realWindowFrames);
    } else if (m_cfg.betterEstimate) {
        chosenFrames = pickBetterEstimateCategory(ev, categories);
    } else {
        chosenFrames = estimateFakeCategory(ev, categories, interInput, holdDuration);
    }

    if (chosenFrames < 1) {
        out.accepted = false;
        out.reason = FilterReason::NoEnabledCategories;
        return out;
    }

    out.accepted = true;
    out.reason = FilterReason::Accepted;
    out.categoryFrames = chosenFrames;
    out.label = std::to_string(chosenFrames) + "F";
    out.color = categories.colorForFrames(chosenFrames);

    m_hasLastCircle[p] = true;
    m_lastCircleWasGround[p] = ev.isOnGround;
    m_lastCircleX[p] = ev.x;
    m_lastCircleY[p] = ev.y;
    m_lastCircleTime[p] = ev.timeSeconds;
    m_recentAcceptedTimes.push_back(ev.timeSeconds);
    ++m_totalAccepted;

    return out;
}

} // namespace ffc
