#include "RealAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ffc {

void RealAnalyzer::begin(MacroData const& macro, RealScanConfig const& cfg) {
    m_macro = macro;
    m_cfg = cfg;
    if (!(m_cfg.tps > 0.0)) m_cfg.tps = 240.0;
    if (!(m_cfg.referenceFps > 0.0)) m_cfg.referenceFps = 60.0;
    if (m_cfg.maxWindowFrames < 1) m_cfg.maxWindowFrames = 12;

    double ticksPerFrame = m_cfg.tps / m_cfg.referenceFps;
    m_maxShiftTicks = std::max(1, static_cast<int>(std::ceil(m_cfg.maxWindowFrames * ticksPerFrame)));

    m_results.clear();
    m_refP1.clear();
    m_refP2.clear();
    m_probeList.clear();
    m_probeCursor = 0;
    m_cursor = 0;
    m_refEndTick = 0;
    m_error.clear();

    if (m_macro.inputs.empty()) {
        m_phase = Phase::Failed;
        m_error = "Loaded macro has no inputs to analyze.";
        return;
    }

    for (size_t i = 0; i < m_macro.inputs.size(); ++i) {
        if (!m_cfg.includeReleases && !m_macro.inputs[i].down) continue;
        m_probeList.push_back(i);
    }

    m_phase = Phase::ReferenceRun;
}

void RealAnalyzer::cancel() {
    m_phase = Phase::Idle;
    m_workingInputs.clear();
    m_cursor = 0;
}

void RealAnalyzer::completeWithResults(std::vector<MeasuredInputWindow> results) {
    m_results = std::move(results);
    m_workingInputs.clear();
    m_refP1.clear();
    m_refP2.clear();
    m_probeCursor = m_probeList.size();
    m_error.clear();
    m_phase = Phase::Completed;
}

double RealAnalyzer::progressFraction() const {
    if (m_phase == Phase::Completed) return 1.0;
    if (m_phase == Phase::ReferenceRun) return 0.08;
    if (m_phase == Phase::ProbingInputs) {
        if (m_probeList.empty()) return 1.0;
        return 0.08 + 0.92 * (static_cast<double>(m_probeCursor) / static_cast<double>(m_probeList.size()));
    }
    return 0.0;
}

std::string RealAnalyzer::statusSummary() const {
    char buf[160];
    switch (m_phase) {
        case Phase::Idle:
            return "Idle";
        case Phase::ReferenceRun:
            return "Running reference macro verification...";
        case Phase::ProbingInputs:
            std::snprintf(buf, sizeof(buf), "Experimental Real Scan: probing input %zu / %zu",
                          std::min(m_probeCursor + 1, m_probeList.size()), m_probeList.size());
            return buf;
        case Phase::Completed:
            std::snprintf(buf, sizeof(buf), "Completed (%zu inputs analyzed)", m_results.size());
            return buf;
        case Phase::Failed:
            return "Failed: " + m_error;
    }
    return "";
}

void RealAnalyzer::onProcessCommands(SimulationBackend& backend, int64_t progress) {
    int64_t targetFrame = progress - static_cast<int64_t>(m_cfg.frameOffset);
    while (m_cursor < m_workingInputs.size() && m_workingInputs[m_cursor].frame <= targetFrame) {
        auto const& in = m_workingInputs[m_cursor++];
        backend.injectInput(in.button, in.player2, in.down);
    }
}

bool RealAnalyzer::runTrialFromStart(SimulationBackend& backend, size_t targetIdx, int shiftTicks, int64_t surviveUntilTick) {
    m_workingInputs = m_macro.inputs;
    int64_t origFrame = m_workingInputs[targetIdx].frame;
    int64_t newFrame = std::max<int64_t>(1, origFrame + shiftTicks);

    // Clamp against adjacent inputs of the same player & button so ordering is preserved
    for (size_t i = 0; i < m_workingInputs.size(); ++i) {
        if (i == targetIdx) continue;
        if (m_workingInputs[i].player2 != m_workingInputs[targetIdx].player2 ||
            m_workingInputs[i].button != m_workingInputs[targetIdx].button) {
            continue;
        }
        if (i < targetIdx && newFrame <= m_workingInputs[i].frame) {
            return false;
        }
        if (i > targetIdx && newFrame >= m_workingInputs[i].frame) {
            return false;
        }
    }

    m_workingInputs[targetIdx].frame = newFrame;
    std::stable_sort(m_workingInputs.begin(), m_workingInputs.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.frame < b.frame;
    });

    m_cursor = 0;
    backend.clearRunFlags();
    backend.releaseAllButtons();

    int64_t resumeTick = std::max<int64_t>(
        0,
        std::min(origFrame, newFrame) + static_cast<int64_t>(m_cfg.frameOffset) - 2
    );
    if (resumeTick > 0 && backend.restoreToTick(resumeTick)) {
        while (m_cursor < m_workingInputs.size() &&
               (m_workingInputs[m_cursor].frame + static_cast<int64_t>(m_cfg.frameOffset)) <= resumeTick) {
            ++m_cursor;
        }
    } else {
        backend.restartFromZero();
    }

    int64_t guardLimit = (surviveUntilTick - backend.currentProgress()) + 8;
    int64_t steps = 0;
    int stalledSteps = 0;
    while (backend.currentProgress() < surviveUntilTick && steps < guardLimit) {
        int64_t prevProgress = backend.currentProgress();
        backend.stepSingleTick();
        ++steps;
        if (backend.currentProgress() <= prevProgress) {
            if (++stalledSteps > 4) return false;
        } else {
            stalledSteps = 0;
        }
        if (backend.isDead()) return false;
        if (backend.isLevelComplete()) return true;
    }
    return !backend.isDead();
}

void RealAnalyzer::step(SimulationBackend& backend) {
    if (m_phase == Phase::ReferenceRun) {
        m_workingInputs = m_macro.inputs;
        m_cursor = 0;
        m_refP1.clear();
        m_refP2.clear();

        backend.clearRunFlags();
        backend.releaseAllButtons();
        backend.restartFromZero();

        int64_t lastInputFrame = m_macro.inputs.back().frame + static_cast<int64_t>(m_cfg.frameOffset);
        int64_t maxRefTicks = lastInputFrame + static_cast<int64_t>(m_cfg.tps * 1.5);

        m_refP1.push_back(backend.sampleState(false));
        m_refP2.push_back(backend.sampleState(true));

        int64_t steps = 0;
        int stalledSteps = 0;
        while (backend.currentProgress() < maxRefTicks && steps < maxRefTicks + 16) {
            int64_t prevProgress = backend.currentProgress();
            backend.stepSingleTick();
            ++steps;
            if (backend.currentProgress() <= prevProgress) {
                if (++stalledSteps > 4) {
                    m_phase = Phase::Failed;
                    m_error = "Simulation did not advance level progress during Reference Run.";
                    return;
                }
            } else {
                stalledSteps = 0;
            }
            size_t p = static_cast<size_t>( std::max<int64_t>(0, backend.currentProgress()) );
            if (m_refP1.size() <= p) {
                m_refP1.resize(p + 1);
                m_refP2.resize(p + 1);
            }
            m_refP1[p] = backend.sampleState(false);
            m_refP2[p] = backend.sampleState(true);

            if (backend.isDead()) {
                m_phase = Phase::Failed;
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "Reference macro run died at tick %lld. Check macro TPS (%.0f) and Frame Offset (%+d).",
                              static_cast<long long>(backend.currentProgress()), m_cfg.tps, m_cfg.frameOffset);
                m_error = buf;
                return;
            }
            if (backend.isLevelComplete()) {
                break;
            }
        }
        m_refEndTick = backend.currentProgress();
        m_phase = m_probeList.empty() ? Phase::Completed : Phase::ProbingInputs;
        return;
    }

    if (m_phase == Phase::ProbingInputs) {
        if (m_probeCursor >= m_probeList.size()) {
            m_phase = Phase::Completed;
            return;
        }

        size_t idx = m_probeList[m_probeCursor++];
        auto const& in = m_macro.inputs[idx];
        int64_t tickAtInput = in.frame + static_cast<int64_t>(m_cfg.frameOffset);

        // Determine survival horizon: next input for the same player (plus 4 ticks margin) or reference end
        int64_t surviveHorizon = m_refEndTick;
        for (size_t j = idx + 1; j < m_macro.inputs.size(); ++j) {
            if (m_macro.inputs[j].player2 == in.player2) {
                surviveHorizon = std::min(m_refEndTick,
                                          m_macro.inputs[j].frame + static_cast<int64_t>(m_cfg.frameOffset) + 4);
                break;
            }
        }
        surviveHorizon = std::max(surviveHorizon, tickAtInput + static_cast<int64_t>(m_maxShiftTicks) + 4);

        // Binary search latest surviving positive shift [0 .. m_maxShiftTicks]
        int bestRight = 0;
        for (int s = 1; s <= m_maxShiftTicks; ++s) {
            if (runTrialFromStart(backend, idx, +s, surviveHorizon + s)) {
                bestRight = s;
            } else {
                break;
            }
        }

        // Binary search earliest surviving negative shift [-m_maxShiftTicks .. 0]
        int bestLeft = 0;
        int maxLeftBudget = std::max(0, m_maxShiftTicks - bestRight);
        for (int s = 1; s <= maxLeftBudget; ++s) {
            if (runTrialFromStart(backend, idx, -s, surviveHorizon)) {
                bestLeft = -s;
            } else {
                break;
            }
        }

        MeasuredInputWindow mw;
        mw.macroInputIndex = idx;
        mw.frame = in.frame;
        mw.button = in.button;
        mw.player2 = in.player2;
        mw.down = in.down;
        mw.earliestSurvivingShiftTicks = bestLeft;
        mw.latestSurvivingShiftTicks = bestRight;
        mw.windowTicks = (bestRight - bestLeft) + 1;
        mw.windowFrames = static_cast<double>(mw.windowTicks) * (m_cfg.referenceFps / m_cfg.tps);
        mw.cappedAtMax = (mw.windowTicks >= m_maxShiftTicks);
        mw.verified = true;

        size_t snapIdx = static_cast<size_t>(std::clamp<int64_t>(tickAtInput, 0, static_cast<int64_t>(m_refP1.size()) - 1));
        if (!m_refP1.empty()) {
            auto const& snap = in.player2 ? m_refP2[snapIdx] : m_refP1[snapIdx];
            mw.x = snap.x;
            mw.y = snap.y;
            mw.timeSeconds = snap.timeSeconds;
        }
        if (in.hasPosition && mw.x == 0.0f && mw.y == 0.0f) {
            mw.x = in.x;
            mw.y = in.y;
        }

        m_results.push_back(mw);

        if (m_probeCursor >= m_probeList.size()) {
            m_phase = Phase::Completed;
        }
    }
}

MeasuredInputWindow const* RealAnalyzer::findMatchForInput(size_t macroInputIndex) const {
    for (auto const& r : m_results) {
        if (r.macroInputIndex == macroInputIndex && r.verified) {
            return &r;
        }
    }
    return nullptr;
}

} // namespace ffc
