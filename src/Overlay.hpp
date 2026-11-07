#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include "core/MomentDetector.hpp"

#include <deque>
#include <vector>

namespace ffc {

class FFCOverlay : public cocos2d::CCNode {
public:
    static FFCOverlay* create(PlayLayer* pl);
    static FFCOverlay* ensure(PlayLayer* pl);
    static FFCOverlay* find(PlayLayer* pl);

    bool init(PlayLayer* pl);
    void onMomentAccepted(MomentResult const& moment);
    void onReset();
    void updateOverlay();
    void refreshCounterDisplay();

    ~FFCOverlay() override;

private:
    struct WorldMarker {
        cocos2d::CCNode* node = nullptr;
        float worldX = 0.0f;
        float worldY = 0.0f;
        int categoryFrames = 1;
    };

    void spawnWorldMarker(MomentResult const& moment);
    void cullOldMarkers();

    PlayLayer* m_playLayer = nullptr;
    cocos2d::CCNode* m_worldContainer = nullptr;
    cocos2d::CCNode* m_counterContainer = nullptr;
    cocos2d::CCDrawNode* m_counterBg = nullptr;
    cocos2d::CCLabelBMFont* m_totalLabel = nullptr;
    cocos2d::CCLabelBMFont* m_scanStatusLabel = nullptr;

    struct CounterEntryUI {
        int frames = 0;
        cocos2d::CCLabelBMFont* label = nullptr;
    };
    std::vector<CounterEntryUI> m_counterEntries;
    std::deque<WorldMarker> m_markers;
};

} // namespace ffc
