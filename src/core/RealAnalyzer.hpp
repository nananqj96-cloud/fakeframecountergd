#pragma once

#include "Macro.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ffc {

struct SimSnapshot {
    float x = 0.0f;
    float y = 0.0f;
    double yVel = 0.0;
    double timeSeconds = 0.0;
    bool valid = false;
};

class SimulationBackend {
public:
    virtual ~SimulationBackend() = default;
    virtual void restartFromZero() = 0;
    virtual bool saveCheckpoint() = 0;
    virtual bool loadCheckpoint() = 0;
    virtual int64_t currentProgress() const = 0;
    virtual void stepSingleTick() = 0;
    virtual SimSnapshot sampleState(bool player2) const = 0;
    virtual bool isDead() const = 0;
    virtual bool isLevelComplete() const = 0;
    virtual void clearRunFlags() = 0;
    virtual void injectInput(uint8_t button, bool player2, bool down) = 0;
    virtual void releaseAllButtons() = 0;
};

struct RealScanConfig {
    double tps = 240.0;
    double referenceFps = 60.0; // Frame window reference FPS (e.g. 60 FPS or 240 FPS)
    int maxWindowFrames = 12;   // Maximum window in frames to probe
    int frameOffset = 0;
    bool includeReleases = true;
};

struct MeasuredInputWindow {
    size_t macroInputIndex = 0;
    int64_t frame = 0;
    uint8_t button = 1;
    bool player2 = false;
    bool down = true;
    float x = 0.0f;
    float y = 0.0f;
    double timeSeconds = 0.0;

    int earliestSurvivingShiftTicks = 0; // <= 0
    int latestSurvivingShiftTicks = 0;   // >= 0
    int windowTicks = 1;                 // latest - earliest + 1
    double windowFrames = 1.0;           // windowTicks * referenceFps / tps
    bool cappedAtMax = false;
    bool verified = false;
};

class RealAnalyzer {
public:
    enum class Phase : uint8_t {
        Idle = 0,
        ReferenceRun = 1,
        ProbingInputs = 2,
        Completed = 3,
        Failed = 4,
    };

    void begin(MacroData const& macro, RealScanConfig const& cfg);
    void cancel();

    bool isActive() const {
        return m_phase == Phase::ReferenceRun || m_phase == Phase::ProbingInputs;
    }
    Phase phase() const { return m_phase; }
    std::string const& error() const { return m_error; }
    double progressFraction() const;
    std::string statusSummary() const;

    // Advance the state machine by one simulation tick or state transition.
    void step(SimulationBackend& backend);

    // Called by the backend during stepSingleTick() right after processCommands
    void onProcessCommands(SimulationBackend& backend, int64_t progress);

    std::vector<MeasuredInputWindow> const& results() const { return m_results; }
    MeasuredInputWindow const* findMatchForInput(size_t macroInputIndex) const;

private:
    bool runTrialFromStart(SimulationBackend& backend, size_t targetIdx, int shiftTicks, int64_t surviveUntilTick);

    Phase m_phase = Phase::Idle;
    std::string m_error;
    MacroData m_macro;
    RealScanConfig m_cfg;
    int m_maxShiftTicks = 12;

    std::vector<MacroInput> m_workingInputs;
    size_t m_cursor = 0;

    // Reference trajectory indexed by tick
    std::vector<SimSnapshot> m_refP1;
    std::vector<SimSnapshot> m_refP2;
    int64_t m_refEndTick = 0;

    // Candidate indices in m_macro.inputs to probe
    std::vector<size_t> m_probeList;
    size_t m_probeCursor = 0;

    std::vector<MeasuredInputWindow> m_results;
};

} // namespace ffc
