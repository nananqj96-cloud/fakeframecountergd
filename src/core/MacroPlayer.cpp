#include "MacroPlayer.hpp"

namespace ffc {

void MacroPlayer::setMacro(MacroData macro, std::string path) {
    m_macro = std::move(macro);
    m_macroPath = std::move(path);
    m_state = PlaybackState::Stopped;
    m_cursor = 0;
    m_lastTick = -1;
}

void MacroPlayer::clearMacro() {
    m_macro.reset();
    m_macroPath.clear();
    m_state = PlaybackState::Stopped;
    m_cursor = 0;
    m_lastTick = -1;
}

void MacroPlayer::startPlayback() {
    if (!hasMacro()) {
        m_state = PlaybackState::Stopped;
        return;
    }
    m_cursor = 0;
    m_lastTick = -1;
    m_state = PlaybackState::Playing;
}

void MacroPlayer::stopPlayback() {
    m_state = PlaybackState::Stopped;
    m_cursor = 0;
    m_lastTick = -1;
}

void MacroPlayer::onLevelReset(bool autoReplayOnReset) {
    m_cursor = 0;
    m_lastTick = -1;
    if (m_state == PlaybackState::Playing || (m_state == PlaybackState::Finished && autoReplayOnReset)) {
        if (hasMacro()) {
            m_state = PlaybackState::Playing;
        }
    }
}

size_t MacroPlayer::onProcessTick(int64_t currentProgress, InjectFn const& inject) {
    if (m_state != PlaybackState::Playing || !hasMacro()) {
        return 0;
    }

    m_lastTick = currentProgress;
    int64_t targetMacroFrame = currentProgress - static_cast<int64_t>(m_frameOffset);

    size_t injectedCount = 0;
    auto const& inputs = m_macro->inputs;
    while (m_cursor < inputs.size()) {
        auto const& in = inputs[m_cursor];
        if (in.frame > targetMacroFrame) {
            break;
        }
        size_t idx = m_cursor++;
        if (inject) {
            inject(in, idx);
        }
        ++injectedCount;
    }

    if (m_cursor >= inputs.size()) {
        m_state = PlaybackState::Finished;
    }

    return injectedCount;
}

} // namespace ffc
