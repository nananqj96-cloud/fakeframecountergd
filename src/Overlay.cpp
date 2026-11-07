#include "Overlay.hpp"
#include "ModState.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace geode::prelude;

namespace ffc {

namespace {
constexpr char const* kOverlayNodeID = "nananqj96.fake-frame-counter/overlay";
constexpr char const* kWorldContainerID = "nananqj96.fake-frame-counter/world-markers";
constexpr size_t kMaxWorldMarkers = 140;
} // namespace

FFCOverlay* FFCOverlay::create(PlayLayer* pl) {
    auto* ret = new (std::nothrow) FFCOverlay();
    if (ret && ret->init(pl)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

FFCOverlay* FFCOverlay::find(PlayLayer* pl) {
    if (!pl) return nullptr;
    return typeinfo_cast<FFCOverlay*>(pl->getChildByID(kOverlayNodeID));
}

FFCOverlay* FFCOverlay::ensure(PlayLayer* pl) {
    if (!pl) return nullptr;
    if (auto* existing = find(pl)) {
        return existing;
    }
    auto* ov = create(pl);
    if (ov) {
        pl->addChild(ov, 1050);
    }
    return ov;
}

FFCOverlay::~FFCOverlay() {
    if (m_worldContainer && m_worldContainer->getParent()) {
        m_worldContainer->removeFromParentAndCleanup(true);
    }
    m_worldContainer = nullptr;
}

bool FFCOverlay::init(PlayLayer* pl) {
    if (!CCNode::init()) return false;
    m_playLayer = pl;
    this->setID(kOverlayNodeID);

    if (pl && pl->m_objectLayer) {
        if (auto* oldWorld = pl->m_objectLayer->getChildByID(kWorldContainerID)) {
            oldWorld->removeFromParentAndCleanup(true);
        }
        m_worldContainer = CCNode::create();
        m_worldContainer->setID(kWorldContainerID);
        pl->m_objectLayer->addChild(m_worldContainer, 9999);
    }

    m_counterContainer = CCNode::create();
    this->addChild(m_counterContainer, 10);

    m_counterBg = CCDrawNode::create();
    m_counterBg->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    m_counterContainer->addChild(m_counterBg, 0);

    m_totalLabel = CCLabelBMFont::create("Total: 0", "bigFont.fnt");
    m_totalLabel->setAnchorPoint({1.0f, 1.0f});
    m_counterContainer->addChild(m_totalLabel, 2);

    m_scanStatusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_scanStatusLabel->setAnchorPoint({0.5f, 1.0f});
    m_scanStatusLabel->setScale(0.65f);
    m_scanStatusLabel->setColor({120, 235, 255});
    m_scanStatusLabel->setVisible(false);
    this->addChild(m_scanStatusLabel, 15);

    refreshCounterDisplay();
    return true;
}

void FFCOverlay::onReset() {
    m_markers.clear();
    if (m_worldContainer) {
        m_worldContainer->removeAllChildrenWithCleanup(true);
    }
    refreshCounterDisplay();
}

void FFCOverlay::onMomentAccepted(MomentResult const& moment) {
    spawnWorldMarker(moment);
    refreshCounterDisplay();
}

void FFCOverlay::spawnWorldMarker(MomentResult const& moment) {
    if (!m_playLayer) return;
    if (!m_worldContainer && m_playLayer->m_objectLayer) {
        m_worldContainer = CCNode::create();
        m_worldContainer->setID(kWorldContainerID);
        m_playLayer->m_objectLayer->addChild(m_worldContainer, 9999);
    }
    if (!m_worldContainer) return;

    float scaleMult = static_cast<float>(Mod::get()->getSettingValue<double>("circle-scale"));
    scaleMult = std::clamp(scaleMult, 0.4f, 2.5f);

    auto* markerNode = CCNode::create();
    markerNode->setPosition({moment.x, moment.y});

    auto* draw = CCDrawNode::create();
    draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});

    float radius = 10.5f * scaleMult;
    float borderThickness = std::max(0.95f, 1.45f * scaleMult);
    ccColor4F fillCol = {
        moment.color.r / 255.0f,
        moment.color.g / 255.0f,
        moment.color.b / 255.0f,
        0.22f
    };
    ccColor4F borderCol = {
        moment.color.r / 255.0f,
        moment.color.g / 255.0f,
        moment.color.b / 255.0f,
        0.95f
    };

    constexpr int kSegments = 24;
    CCPoint verts[kSegments];
    for (int i = 0; i < kSegments; ++i) {
        float angle = static_cast<float>(i) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(kSegments));
        verts[i] = ccp(std::cos(angle) * radius, std::sin(angle) * radius);
    }
    draw->drawPolygon(verts, kSegments, fillCol, borderThickness, borderCol);

    // Subtle center point for precision readability
    draw->drawDot({0.0f, 0.0f}, std::max(1.2f, 1.6f * scaleMult), borderCol);
    markerNode->addChild(draw, 1);

    // Matching colored frame-window label (e.g. "1F", "4F", "12F")
    auto* lbl = CCLabelBMFont::create(moment.label.c_str(), "bigFont.fnt");
    lbl->setColor({moment.color.r, moment.color.g, moment.color.b});
    lbl->setScale(0.30f * scaleMult);
    lbl->setPosition({0.0f, radius + 7.5f * scaleMult});
    markerNode->addChild(lbl, 2);

    // Crisp pop-in animation
    markerNode->setScale(0.65f);
    markerNode->runAction(CCEaseBackOut::create(CCScaleTo::create(0.10f, 1.0f)));

    m_worldContainer->addChild(markerNode);
    m_markers.push_back(WorldMarker{markerNode, moment.x, moment.y, moment.categoryFrames});

    while (m_markers.size() > kMaxWorldMarkers) {
        if (m_markers.front().node) {
            m_markers.front().node->removeFromParentAndCleanup(true);
        }
        m_markers.pop_front();
    }
}

void FFCOverlay::cullOldMarkers() {
    if (!m_playLayer || !m_playLayer->m_player1 || m_markers.empty()) return;
    // Only cull if not in platformer mode (in platformer mode players may backtrack)
    if (m_playLayer->m_isPlatformer) return;

    float px = m_playLayer->m_player1->getPositionX();
    while (!m_markers.empty()) {
        if (px - m_markers.front().worldX > 900.0f) {
            if (m_markers.front().node) {
                m_markers.front().node->removeFromParentAndCleanup(true);
            }
            m_markers.pop_front();
        } else {
            break;
        }
    }
}

void FFCOverlay::refreshCounterDisplay() {
    if (!m_counterContainer || !m_totalLabel || !m_counterBg) return;

    auto& state = ModState::get();
    bool modEn = state.isModEnabled() && !state.isOverlayHiddenByKey();
    bool showCounter = modEn && Mod::get()->getSettingValue<bool>("show-counter");
    bool showCircles = modEn && Mod::get()->getSettingValue<bool>("show-circles");

    if (m_worldContainer) {
        m_worldContainer->setVisible(showCircles);
    }
    m_counterContainer->setVisible(showCounter);
    if (!showCounter) return;

    float counterScale = static_cast<float>(Mod::get()->getSettingValue<double>("counter-scale"));
    counterScale = std::clamp(counterScale, 0.4f, 2.0f);
    bool hideZero = Mod::get()->getSettingValue<bool>("counter-hide-zero");

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float rightX = winSize.width - 10.0f;
    float topY = winSize.height - 36.0f;

    // Clear old category labels and rebuild cleanly
    for (auto& entry : m_counterEntries) {
        if (entry.label) {
            entry.label->removeFromParentAndCleanup(true);
        }
    }
    m_counterEntries.clear();

    m_totalLabel->setString(fmt::format("Total: {}", state.totalMomentsThisAttempt()).c_str());
    m_totalLabel->setScale(0.31f * counterScale);
    m_totalLabel->setPosition({rightX, topY});
    m_totalLabel->setColor({255, 255, 255});

    auto enabledCats = state.categories().enabledCategoriesAscending();
    std::vector<FrameCategory> visibleCats;
    visibleCats.reserve(enabledCats.size());
    for (auto const& cat : enabledCats) {
        int c = state.countForCategory(cat.frames);
        if (hideZero && c == 0) continue;
        visibleCats.push_back(cat);
    }

    int numVisible = static_cast<int>(visibleCats.size());
    bool twoColumns = (numVisible > 6);
    int rowsPerCol = twoColumns ? ((numVisible + 1) / 2) : numVisible;

    float rowHeight = 11.5f * counterScale;
    float colWidth = 54.0f * counterScale;
    float startY = topY - (14.5f * counterScale);

    for (int idx = 0; idx < numVisible; ++idx) {
        auto const& cat = visibleCats[idx];
        int count = state.countForCategory(cat.frames);
        RGBColor col = state.categories().colorForFrames(cat.frames);

        int colIdx = twoColumns ? (idx / rowsPerCol) : 0; // 0 = left column, 1 = right column
        int rowIdx = twoColumns ? (idx % rowsPerCol) : idx;

        float xPos = twoColumns
            ? (colIdx == 0 ? (rightX - colWidth) : rightX)
            : rightX;
        float yPos = startY - static_cast<float>(rowIdx) * rowHeight;

        auto* lbl = CCLabelBMFont::create(
            fmt::format("{}: {}", CategoryConfig::labelForFrames(cat.frames), count).c_str(),
            "bigFont.fnt"
        );
        lbl->setAnchorPoint({1.0f, 1.0f});
        lbl->setScale(0.255f * counterScale);
        lbl->setColor({col.r, col.g, col.b});
        lbl->setPosition({xPos, yPos});
        m_counterContainer->addChild(lbl, 2);
        m_counterEntries.push_back({cat.frames, lbl});
    }

    // Draw subtle dark translucent background box behind the top-right counter
    m_counterBg->clear();
    float boxWidth = (twoColumns ? (colWidth * 2.0f + 8.0f * counterScale) : (colWidth + 14.0f * counterScale));
    boxWidth = std::max(boxWidth, m_totalLabel->getScaledContentSize().width + 12.0f * counterScale);
    float boxHeight = (16.5f * counterScale) + static_cast<float>(rowsPerCol) * rowHeight + (4.0f * counterScale);

    CCPoint bgVerts[4] = {
        ccp(rightX - boxWidth, topY - boxHeight + 3.0f * counterScale),
        ccp(rightX + 4.0f * counterScale, topY - boxHeight + 3.0f * counterScale),
        ccp(rightX + 4.0f * counterScale, topY + 3.0f * counterScale),
        ccp(rightX - boxWidth, topY + 3.0f * counterScale)
    };
    m_counterBg->drawPolygon(bgVerts, 4, {0.0f, 0.0f, 0.0f, 0.36f}, 0.8f, {1.0f, 1.0f, 1.0f, 0.14f});
}

void FFCOverlay::updateOverlay() {
    auto& state = ModState::get();
    bool modEn = state.isModEnabled() && !state.isOverlayHiddenByKey();
    bool showCircles = modEn && Mod::get()->getSettingValue<bool>("show-circles");
    bool showCounter = modEn && Mod::get()->getSettingValue<bool>("show-counter");

    if (m_worldContainer) {
        m_worldContainer->setVisible(showCircles);
    }
    if (m_counterContainer) {
        m_counterContainer->setVisible(showCounter);
    }

    if (m_scanStatusLabel) {
        if (state.realAnalyzer().isActive()) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            m_scanStatusLabel->setPosition({winSize.width * 0.5f, winSize.height - 14.0f});
            m_scanStatusLabel->setString(state.realAnalyzer().statusText().c_str());
            m_scanStatusLabel->setVisible(true);
        } else {
            m_scanStatusLabel->setVisible(false);
        }
    }

    cullOldMarkers();
}

} // namespace ffc
