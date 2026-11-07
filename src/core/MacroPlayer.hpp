#pragma once

#include "Macro.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace ffc {

enum class PlaybackState : uint8_t {
    Stopped = 0,
    Playing = 1,
    Finished = 2,
};

class MacroPlayer {
public:
    using InjectFn = std::function<void(MacroInput const& input, size_t inputIndex)>;

    MacroPlayer() = default;

    void setMacro(MacroData macro, std::string path = "");
    void clearMacro();

    bool hasMacro() const { return m_macro.has_value() && !m_macro->inputs.empty(); }
    MacroData const* macro() const { return m_macro ? &(*m_macro) : nullptr; }
    std::string const& macroPath() const { return m_macroPath; }

    void startPlayback();
    void stopPlayback();
    void onLevelReset(bool autoReplayOnReset);

    PlaybackState state() const { return m_state; }
    bool isPlaying() const { return m_state == PlaybackState::Playing; }
    bool isFinished() const { return m_state == PlaybackState::Finished; }

    void setFrameOffset(int offset) { m_frameOffset = offset; }
    int frameOffset() const { return m_frameOffset; }

    void setLockDeltaToTps(bool lock) { m_lockDelta = lock; }
    bool lockDeltaToTps() const { return m_lockDelta; }

    double effectiveTps() const {
        return (m_macro && m_macro->tps > 0.0) ? m_macro->tps : 240.0;
    }

    size_t currentCursor() const { return m_cursor; }
    size_t totalInputs() const { return m_macro ? m_macro->inputs.size() : 0; }
    int64_t lastTick() const { return m_lastTick; }

    // Called inside GJBaseGameLayer::processCommands on each physics step.
    // Returns the number of macro inputs injected on this tick.
    size_t onProcessTick(int64_t currentProgress, InjectFn const& inject);

private:
    std::optional<MacroData> m_macro;
    std::string m_macroPath;
    PlaybackState m_state = PlaybackState::Stopped;
    size_t m_cursor = 0;
    int m_frameOffset = 0;
    bool m_lockDelta = true;
    int64_t m_lastTick = -1;
};

} // namespace ffc
