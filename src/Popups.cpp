#include "Popups.hpp"
#include "ModState.hpp"
#include "Overlay.hpp"
#include "SoundManager.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace geode::prelude;

namespace ffc {

// ============================================================================
// SoundGuidePopup: Slow, clear descending demonstration (12F -> 11F -> ... -> 1F)
// ============================================================================

SoundGuidePopup* SoundGuidePopup::create() {
    auto* ret = new (std::nothrow) SoundGuidePopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SoundGuidePopup::init() {
    if (!Popup::init(360.0f, 240.0f)) return false;

    this->setTitle("Frame Window Sound Guide (Descending)");
    auto content = m_mainLayer->getContentSize();

    auto* subtitle = CCLabelBMFont::create(
        "Demonstrates enabled categories from highest window (12F) down to 1F",
        "chatFont.fnt"
    );
    subtitle->setScale(0.55f);
    subtitle->setColor({200, 220, 255});
    subtitle->setPosition({content.width * 0.5f, content.height - 38.0f});
    m_mainLayer->addChild(subtitle);

    m_previewCircle = CCDrawNode::create();
    m_previewCircle->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    m_previewCircle->setPosition({content.width * 0.5f, content.height * 0.5f + 22.0f});
    m_mainLayer->addChild(m_previewCircle, 2);

    m_previewShortLabel = CCLabelBMFont::create("12F", "bigFont.fnt");
    m_previewShortLabel->setScale(0.62f);
    m_previewShortLabel->setPosition({content.width * 0.5f, content.height * 0.5f + 22.0f});
    m_mainLayer->addChild(m_previewShortLabel, 3);

    m_previewTitleLabel = CCLabelBMFont::create("12-frame window", "goldFont.fnt");
    m_previewTitleLabel->setScale(0.68f);
    m_previewTitleLabel->setPosition({content.width * 0.5f, content.height * 0.5f - 24.0f});
    m_mainLayer->addChild(m_previewTitleLabel, 3);

    m_previewSubLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_previewSubLabel->setScale(0.58f);
    m_previewSubLabel->setColor({210, 210, 210});
    m_previewSubLabel->setPosition({content.width * 0.5f, content.height * 0.5f - 42.0f});
    m_mainLayer->addChild(m_previewSubLabel, 3);

    m_stripContainer = CCNode::create();
    m_stripContainer->setPosition({content.width * 0.5f, 58.0f});
    m_mainLayer->addChild(m_stripContainer, 3);

    auto enabledDesc = ModState::get().categories().enabledCategoriesDescending();
    for (auto const& cat : enabledDesc) {
        m_descendingFrames.push_back(cat.frames);
    }

    auto* ctrlMenu = CCMenu::create();
    ctrlMenu->setPosition({0.0f, 0.0f});
    m_mainLayer->addChild(ctrlMenu, 5);

    auto addBtn = [&](char const* text, float x, float y, float scale, SEL_MenuHandler cb, CCLabelBMFont** outLbl = nullptr) {
        auto* spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", 0.7f);
        spr->setScale(scale);
        if (outLbl) *outLbl = spr->m_label;
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setPosition({x, y});
        ctrlMenu->addChild(btn);
    };

    addBtn("<< Prev", content.width * 0.5f - 115.0f, 25.0f, 0.58f, menu_selector(SoundGuidePopup::onPrevStep));
    addBtn("Pause", content.width * 0.5f - 38.0f, 25.0f, 0.58f, menu_selector(SoundGuidePopup::onPlayPause), &m_playPauseBtnLabel);
    addBtn("Restart", content.width * 0.5f + 38.0f, 25.0f, 0.58f, menu_selector(SoundGuidePopup::onRestartGuide));
    addBtn("Next >>", content.width * 0.5f + 115.0f, 25.0f, 0.58f, menu_selector(SoundGuidePopup::onNextStep));

    m_currentIndex = 0;
    m_playing = !m_descendingFrames.empty();
    renderCurrentStep(true);

    if (m_playing) {
        this->schedule(schedule_selector(SoundGuidePopup::onGuideTick), 1.15f);
    }
    return true;
}

void SoundGuidePopup::onClose(CCObject* sender) {
    this->unschedule(schedule_selector(SoundGuidePopup::onGuideTick));
    Popup::onClose(sender);
}

void SoundGuidePopup::onGuideTick(float) {
    if (!m_playing || m_descendingFrames.empty()) return;
    if (m_currentIndex + 1 < static_cast<int>(m_descendingFrames.size())) {
        ++m_currentIndex;
        renderCurrentStep(true);
    } else {
        m_playing = false;
        if (m_playPauseBtnLabel) m_playPauseBtnLabel->setString("Play");
        this->unschedule(schedule_selector(SoundGuidePopup::onGuideTick));
    }
}

void SoundGuidePopup::renderCurrentStep(bool playAudio) {
    m_previewCircle->clear();
    m_stripContainer->removeAllChildrenWithCleanup(true);

    if (m_descendingFrames.empty()) {
        m_previewShortLabel->setString("--");
        m_previewTitleLabel->setString("No categories enabled");
        m_previewSubLabel->setString("Enable at least one category in Categories & Colors.");
        return;
    }

    m_currentIndex = std::clamp(m_currentIndex, 0, static_cast<int>(m_descendingFrames.size()) - 1);
    int frames = m_descendingFrames[m_currentIndex];
    auto const& cats = ModState::get().categories();
    RGBColor col = cats.colorForFrames(frames);

    float radius = 26.0f;
    constexpr int kSegs = 32;
    CCPoint verts[kSegs];
    for (int i = 0; i < kSegs; ++i) {
        float a = static_cast<float>(i) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(kSegs));
        verts[i] = ccp(std::cos(a) * radius, std::sin(a) * radius);
    }
    ccColor4F fillCol = {col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, 0.26f};
    ccColor4F borderCol = {col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, 0.98f};
    m_previewCircle->drawPolygon(verts, kSegs, fillCol, 2.2f, borderCol);

    m_previewShortLabel->setString(CategoryConfig::labelForFrames(frames).c_str());
    m_previewShortLabel->setColor({col.r, col.g, col.b});
    m_previewShortLabel->stopAllActions();
    m_previewShortLabel->setScale(0.76f);
    m_previewShortLabel->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 0.62f)));

    m_previewTitleLabel->setString(CategoryConfig::guideDescriptionForFrames(frames).c_str());

    float hz = cats.pitchHzForFrames(frames);
    m_previewSubLabel->setString(
        fmt::format(
            "Step {} of {}  |  Pitch: {:.0f} Hz ({})",
            m_currentIndex + 1,
            m_descendingFrames.size(),
            hz,
            frames >= 12 ? "Highest Pitch" : (frames == 1 ? "Lowest Pitch" : "Descending Step")
        ).c_str()
    );

    // Render descending strip indicators
    int n = static_cast<int>(m_descendingFrames.size());
    float spacing = std::min(25.0f, 300.0f / static_cast<float>(std::max(1, n)));
    float startX = -0.5f * static_cast<float>(n - 1) * spacing;
    for (int i = 0; i < n; ++i) {
        int f = m_descendingFrames[i];
        RGBColor c = cats.colorForFrames(f);
        auto* itemLbl = CCLabelBMFont::create(CategoryConfig::labelForFrames(f).c_str(), "bigFont.fnt");
        bool active = (i == m_currentIndex);
        itemLbl->setScale(active ? 0.34f : 0.23f);
        itemLbl->setColor(active ? ccColor3B{c.r, c.g, c.b} : ccColor3B{130, 130, 130});
        itemLbl->setPosition({startX + static_cast<float>(i) * spacing, 0.0f});
        m_stripContainer->addChild(itemLbl);
    }

    if (playAudio) {
        SoundManager::get().playCategoryDing(frames, true);
    }
}

void SoundGuidePopup::onPlayPause(CCObject*) {
    if (m_descendingFrames.empty()) return;
    m_playing = !m_playing;
    if (m_playPauseBtnLabel) {
        m_playPauseBtnLabel->setString(m_playing ? "Pause" : "Play");
    }
    this->unschedule(schedule_selector(SoundGuidePopup::onGuideTick));
    if (m_playing) {
        if (m_currentIndex + 1 >= static_cast<int>(m_descendingFrames.size())) {
            m_currentIndex = 0;
            renderCurrentStep(true);
        }
        this->schedule(schedule_selector(SoundGuidePopup::onGuideTick), 1.15f);
    }
}

void SoundGuidePopup::onRestartGuide(CCObject*) {
    if (m_descendingFrames.empty()) return;
    m_currentIndex = 0;
    m_playing = true;
    if (m_playPauseBtnLabel) m_playPauseBtnLabel->setString("Pause");
    this->unschedule(schedule_selector(SoundGuidePopup::onGuideTick));
    renderCurrentStep(true);
    this->schedule(schedule_selector(SoundGuidePopup::onGuideTick), 1.15f);
}

void SoundGuidePopup::onPrevStep(CCObject*) {
    if (m_descendingFrames.empty()) return;
    m_playing = false;
    if (m_playPauseBtnLabel) m_playPauseBtnLabel->setString("Play");
    this->unschedule(schedule_selector(SoundGuidePopup::onGuideTick));
    m_currentIndex = std::max(0, m_currentIndex - 1);
    renderCurrentStep(true);
}

void SoundGuidePopup::onNextStep(CCObject*) {
    if (m_descendingFrames.empty()) return;
    m_playing = false;
    if (m_playPauseBtnLabel) m_playPauseBtnLabel->setString("Play");
    this->unschedule(schedule_selector(SoundGuidePopup::onGuideTick));
    m_currentIndex = std::min(static_cast<int>(m_descendingFrames.size()) - 1, m_currentIndex + 1);
    renderCurrentStep(true);
}

// ============================================================================
// CategoryConfigPopup: Enable/Disable 1F-12F, Add Custom NF, Colors & Sounds
// ============================================================================

CategoryConfigPopup* CategoryConfigPopup::create() {
    auto* ret = new (std::nothrow) CategoryConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CategoryConfigPopup::init() {
    if (!Popup::init(420.0f, 275.0f)) return false;

    this->setTitle("Frame-Window Categories (1F - 12F+)");
    auto content = m_mainLayer->getContentSize();

    auto* sub = CCLabelBMFont::create(
        "Only enabled categories are generated & counted. Default: 1F-4F Red, 5F-6F Orange, 7F-9F Yellow, 10F-12F Blue.",
        "chatFont.fnt"
    );
    sub->setScale(0.47f);
    sub->setColor({200, 220, 240});
    sub->setPosition({content.width * 0.5f, content.height - 36.0f});
    m_mainLayer->addChild(sub);

    m_listContainer = CCNode::create();
    m_mainLayer->addChild(m_listContainer, 2);

    auto* navMenu = CCMenu::create();
    navMenu->setPosition({0.0f, 0.0f});
    m_mainLayer->addChild(navMenu, 5);

    auto makeBtn = [&](char const* txt, float x, float y, float sc, SEL_MenuHandler cb, char const* bg = "GJ_button_01.png") {
        auto* spr = ButtonSprite::create(txt, "goldFont.fnt", bg, 0.7f);
        spr->setScale(sc);
        auto* item = CCMenuItemSpriteExtra::create(spr, this, cb);
        item->setPosition({x, y});
        navMenu->addChild(item);
        return item;
    };

    makeBtn("< Prev", 48.0f, content.height * 0.5f + 6.0f, 0.52f, menu_selector(CategoryConfigPopup::onPrevPage));
    makeBtn("Next >", content.width - 48.0f, content.height * 0.5f + 6.0f, 0.52f, menu_selector(CategoryConfigPopup::onNextPage));

    m_pageLabel = CCLabelBMFont::create("Page 1/2", "goldFont.fnt");
    m_pageLabel->setScale(0.45f);
    m_pageLabel->setPosition({content.width - 52.0f, 52.0f});
    m_mainLayer->addChild(m_pageLabel, 3);

    // Custom frame-window number input (e.g. 15, 20, 30)
    auto* addLbl = CCLabelBMFont::create("Add Custom Frame #:", "chatFont.fnt");
    addLbl->setScale(0.55f);
    addLbl->setAnchorPoint({0.0f, 0.5f});
    addLbl->setPosition({24.0f, 52.0f});
    m_mainLayer->addChild(addLbl, 3);

    m_customFrameInput = TextInput::create(58.0f, "e.g. 15", "bigFont.fnt");
    m_customFrameInput->setCommonFilter(CommonFilter::Uint);
    m_customFrameInput->setMaxCharCount(3);
    m_customFrameInput->setScale(0.75f);
    m_customFrameInput->setPosition({155.0f, 52.0f});
    m_mainLayer->addChild(m_customFrameInput, 4);

    makeBtn("+ Add NF", 222.0f, 52.0f, 0.52f, menu_selector(CategoryConfigPopup::onAddCustomCategory), "GJ_button_02.png");

    // Bottom preset buttons
    makeBtn("Enable All", content.width * 0.5f - 110.0f, 22.0f, 0.52f, menu_selector(CategoryConfigPopup::onEnableAll));
    makeBtn("1F-6F Only", content.width * 0.5f, 22.0f, 0.52f, menu_selector(CategoryConfigPopup::onEnableTightOnly));
    makeBtn("Reset Defaults", content.width * 0.5f + 110.0f, 22.0f, 0.52f, menu_selector(CategoryConfigPopup::onResetCategories), "GJ_button_06.png");

    rebuildList();
    return true;
}

void CategoryConfigPopup::rebuildList() {
    m_listContainer->removeAllChildrenWithCleanup(true);

    auto content = m_mainLayer->getContentSize();
    auto const& allCats = ModState::get().categories().allCategories();
    constexpr int kPerPage = 6;
    int totalPages = std::max(1, (static_cast<int>(allCats.size()) + kPerPage - 1) / kPerPage);
    m_page = std::clamp(m_page, 0, totalPages - 1);

    if (m_pageLabel) {
        m_pageLabel->setString(fmt::format("Page {}/{}", m_page + 1, totalPages).c_str());
    }

    auto* rowMenu = CCMenu::create();
    rowMenu->setPosition({0.0f, 0.0f});
    m_listContainer->addChild(rowMenu, 3);

    int startIdx = m_page * kPerPage;
    int endIdx = std::min(static_cast<int>(allCats.size()), startIdx + kPerPage);

    float topY = content.height - 62.0f;
    float rowH = 25.0f;

    for (int i = startIdx; i < endIdx; ++i) {
        auto const& cat = allCats[i];
        int row = i - startIdx;
        float y = topY - static_cast<float>(row) * rowH;

        RGBColor col = ModState::get().categories().colorForFrames(cat.frames);

        // Toggle ON/OFF button
        auto* togSpr = ButtonSprite::create(
            cat.enabled ? "ON" : "OFF",
            "goldFont.fnt",
            cat.enabled ? "GJ_button_01.png" : "GJ_button_04.png",
            0.65f
        );
        togSpr->setScale(0.46f);
        auto* togBtn = CCMenuItemSpriteExtra::create(togSpr, this, menu_selector(CategoryConfigPopup::onToggleCategory));
        togBtn->setTag(cat.frames);
        togBtn->setPosition({104.0f, y});
        rowMenu->addChild(togBtn);

        // Category label (e.g. "1F", "12F", "15F") in its active color
        auto* catLbl = CCLabelBMFont::create(cat.label().c_str(), "bigFont.fnt");
        catLbl->setScale(0.42f);
        catLbl->setAnchorPoint({0.0f, 0.5f});
        catLbl->setColor(cat.enabled ? ccColor3B{col.r, col.g, col.b} : ccColor3B{120, 120, 120});
        catLbl->setPosition({132.0f, y});
        m_listContainer->addChild(catLbl, 2);

        // Pitch / Custom sound info text
        float hz = ModState::get().categories().pitchHzForFrames(cat.frames);
        std::string infoStr = cat.hasCustomSound ? "Custom WAV" : fmt::format("{:.0f} Hz", hz);
        auto* infoLbl = CCLabelBMFont::create(infoStr.c_str(), "chatFont.fnt");
        infoLbl->setScale(0.50f);
        infoLbl->setAnchorPoint({0.0f, 0.5f});
        infoLbl->setColor({185, 195, 210});
        infoLbl->setPosition({184.0f, y});
        m_listContainer->addChild(infoLbl, 2);

        // Preview Ding button
        auto* dingSpr = ButtonSprite::create("Ding", "goldFont.fnt", "GJ_button_02.png", 0.65f);
        dingSpr->setScale(0.44f);
        auto* dingBtn = CCMenuItemSpriteExtra::create(dingSpr, this, menu_selector(CategoryConfigPopup::onPreviewDing));
        dingBtn->setTag(cat.frames);
        dingBtn->setPosition({252.0f, y});
        rowMenu->addChild(dingBtn);

        // Custom color cycle button
        auto* colSpr = ButtonSprite::create(
            cat.hasCustomColor ? "Col*" : "Col",
            "goldFont.fnt",
            cat.hasCustomColor ? "GJ_button_05.png" : "GJ_button_04.png",
            0.65f
        );
        colSpr->setScale(0.44f);
        auto* colBtn = CCMenuItemSpriteExtra::create(colSpr, this, menu_selector(CategoryConfigPopup::onCycleCustomColor));
        colBtn->setTag(cat.frames);
        colBtn->setPosition({292.0f, y});
        rowMenu->addChild(colBtn);

        // Custom sound file assignment button
        auto* sndSpr = ButtonSprite::create(
            cat.hasCustomSound ? "Snd*" : "Snd",
            "goldFont.fnt",
            cat.hasCustomSound ? "GJ_button_03.png" : "GJ_button_04.png",
            0.65f
        );
        sndSpr->setScale(0.44f);
        auto* sndBtn = CCMenuItemSpriteExtra::create(sndSpr, this, menu_selector(CategoryConfigPopup::onPickCustomSound));
        sndBtn->setTag(cat.frames);
        sndBtn->setPosition({332.0f, y});
        rowMenu->addChild(sndBtn);

        // Delete button for custom categories (>12F)
        if (!cat.isDefault) {
            auto* delSpr = ButtonSprite::create("X", "goldFont.fnt", "GJ_button_06.png", 0.65f);
            delSpr->setScale(0.42f);
            auto* delBtn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(CategoryConfigPopup::onDeleteCustomCategory));
            delBtn->setTag(cat.frames);
            delBtn->setPosition({364.0f, y});
            rowMenu->addChild(delBtn);
        }
    }
}

void CategoryConfigPopup::onPrevPage(CCObject*) {
    if (m_page > 0) {
        --m_page;
        rebuildList();
    }
}

void CategoryConfigPopup::onNextPage(CCObject*) {
    ++m_page;
    rebuildList();
}

void CategoryConfigPopup::onToggleCategory(CCObject* sender) {
    int frames = sender->getTag();
    auto& cats = ModState::get().categories();
    bool nowEn = !cats.isCategoryEnabled(frames);
    // Keep at least one category enabled so live/macro detection always has a valid bucket
    if (!nowEn && cats.enabledCategoriesAscending().size() <= 1) {
        Notification::create("Keep at least 1 category enabled", NotificationIcon::Warning)->show();
        return;
    }
    cats.setCategoryEnabled(frames, nowEn);
    ModState::get().saveCategoryConfig();
    if (auto* pl = PlayLayer::get()) {
        if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
    }
    rebuildList();
}

void CategoryConfigPopup::onPreviewDing(CCObject* sender) {
    int frames = sender->getTag();
    SoundManager::get().playCategoryDing(frames, true);
}

void CategoryConfigPopup::onCycleCustomColor(CCObject* sender) {
    int frames = sender->getTag();
    auto& cats = ModState::get().categories();
    auto const* cat = cats.findCategory(frames);
    if (!cat) return;

    struct PresetPalette {
        RGBColor color;
    };
    static constexpr PresetPalette kPalettes[] = {
        {{255, 59, 48}},   // Red
        {{255, 149, 0}},   // Orange
        {{255, 204, 0}},   // Yellow
        {{52, 199, 89}},   // Green
        {{50, 173, 230}},  // Blue
        {{175, 82, 222}},  // Purple
        {{255, 45, 145}},  // Pink
        {{255, 255, 255}}  // White
    };
    constexpr int kNumPalettes = sizeof(kPalettes) / sizeof(kPalettes[0]);

    if (!cat->hasCustomColor) {
        cats.setCategoryCustomColor(frames, kPalettes[0].color);
    } else {
        int foundIdx = -1;
        for (int i = 0; i < kNumPalettes; ++i) {
            if (cat->customColor == kPalettes[i].color) {
                foundIdx = i;
                break;
            }
        }
        if (foundIdx < 0 || foundIdx + 1 >= kNumPalettes) {
            cats.clearCategoryCustomColor(frames);
        } else {
            cats.setCategoryCustomColor(frames, kPalettes[foundIdx + 1].color);
        }
    }
    ModState::get().saveCategoryConfig();
    if (auto* pl = PlayLayer::get()) {
        if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
    }
    rebuildList();
}

void CategoryConfigPopup::onPickCustomSound(CCObject* sender) {
    int frames = sender->getTag();
    auto& cats = ModState::get().categories();
    auto const* cat = cats.findCategory(frames);
    if (!cat) return;

    // If a custom sound is already set, clicking again clears it back to built-in pitched ding
    if (cat->hasCustomSound) {
        cats.clearCategoryCustomSound(frames);
        ModState::get().saveCategoryConfig();
        Notification::create(fmt::format("Reset {} to built-in pitched ding", CategoryConfig::labelForFrames(frames)), NotificationIcon::Info)->show();
        rebuildList();
        return;
    }

    file::FilePickOptions opts;
    opts.filters.push_back(file::FilePickOptions::Filter{
        .description = "Audio Files (*.wav, *.ogg, *.mp3)",
        .files = {"*.wav", "*.ogg", "*.mp3"}
    });

    m_soundPickerHolder.spawn(
        file::pick(file::PickMode::OpenFile, opts),
        [this, frames](file::PickResult res) {
            if (!res.isOk()) return;
            auto optPath = res.unwrap();
            if (!optPath.has_value()) return;
            std::string pathStr = geode::utils::string::pathToString(*optPath);
            ModState::get().categories().setCategoryCustomSound(frames, pathStr);
            ModState::get().saveCategoryConfig();
            Notification::create(fmt::format("Assigned custom sound to {}", CategoryConfig::labelForFrames(frames)), NotificationIcon::Success)->show();
            this->rebuildList();
        }
    );
}

void CategoryConfigPopup::onDeleteCustomCategory(CCObject* sender) {
    int frames = sender->getTag();
    if (ModState::get().categories().removeCustomCategory(frames)) {
        ModState::get().saveCategoryConfig();
        if (auto* pl = PlayLayer::get()) {
            if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
        }
        rebuildList();
    }
}

void CategoryConfigPopup::onAddCustomCategory(CCObject*) {
    if (!m_customFrameInput) return;
    auto str = m_customFrameInput->getString();
    if (str.empty()) return;
    auto parsed = geode::utils::numFromString<int>(str);
    if (!parsed.isOk()) return;

    int frames = parsed.unwrap();
    if (frames < 1 || frames > 240) {
        Notification::create("Frame number must be between 1 and 240", NotificationIcon::Warning)->show();
        return;
    }
    if (ModState::get().categories().addCustomCategory(frames)) {
        ModState::get().saveCategoryConfig();
        m_customFrameInput->setString("");
        if (auto* pl = PlayLayer::get()) {
            if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
        }
        // Jump to last page so user sees the newly added category
        m_page = 999;
        rebuildList();
        Notification::create(fmt::format("Added {} category", CategoryConfig::labelForFrames(frames)), NotificationIcon::Success)->show();
    } else {
        Notification::create(fmt::format("{} already exists (enabled it)", CategoryConfig::labelForFrames(frames)), NotificationIcon::Info)->show();
        rebuildList();
    }
}

void CategoryConfigPopup::onEnableAll(CCObject*) {
    auto& cats = ModState::get().categories();
    for (auto const& c : cats.allCategories()) {
        cats.setCategoryEnabled(c.frames, true);
    }
    ModState::get().saveCategoryConfig();
    if (auto* pl = PlayLayer::get()) {
        if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
    }
    rebuildList();
}

void CategoryConfigPopup::onEnableTightOnly(CCObject*) {
    auto& cats = ModState::get().categories();
    for (auto const& c : cats.allCategories()) {
        cats.setCategoryEnabled(c.frames, c.frames >= 1 && c.frames <= 6);
    }
    ModState::get().saveCategoryConfig();
    if (auto* pl = PlayLayer::get()) {
        if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
    }
    rebuildList();
}

void CategoryConfigPopup::onResetCategories(CCObject*) {
    ModState::get().categories().resetToDefaults();
    ModState::get().saveCategoryConfig();
    if (auto* pl = PlayLayer::get()) {
        if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
    }
    m_page = 0;
    rebuildList();
    Notification::create("Reset categories to 1F-12F defaults", NotificationIcon::Info)->show();
}

// ============================================================================
// FFCPopup: Main In-Game Control, Macro Loader/Playback & Settings Menu
// ============================================================================

FFCPopup* FFCPopup::create() {
    auto* ret = new (std::nothrow) FFCPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool FFCPopup::init() {
    if (!Popup::init(430.0f, 285.0f)) return false;

    this->setTitle("Fake Frame Counter");
    auto content = m_mainLayer->getContentSize();

    // Clear, honest disclosure inside the settings popup (never shown on the gameplay HUD)
    m_modeNoteLabel = CCLabelBMFont::create(
        "Fake / Visual Estimate Mode: labels (1F-12F+) are simulated estimates for visual fun.",
        "chatFont.fnt"
    );
    m_modeNoteLabel->setScale(0.49f);
    m_modeNoteLabel->setColor({255, 225, 130});
    m_modeNoteLabel->setPosition({content.width * 0.5f, content.height - 34.0f});
    m_mainLayer->addChild(m_modeNoteLabel, 2);

    auto* menu = CCMenu::create();
    menu->setPosition({0.0f, 0.0f});
    m_mainLayer->addChild(menu, 4);

    auto addRowBtn = [&](char const* txt, float x, float y, float sc, SEL_MenuHandler cb,
                         CCLabelBMFont** outLbl = nullptr, char const* bg = "GJ_button_01.png", int tag = 0) {
        auto* spr = ButtonSprite::create(txt, "goldFont.fnt", bg, 0.7f);
        spr->setScale(sc);
        if (outLbl) *outLbl = spr->m_label;
        auto* item = CCMenuItemSpriteExtra::create(spr, this, cb);
        item->setTag(tag);
        item->setPosition({x, y});
        menu->addChild(item);
        return item;
    };

    auto addStaticLabel = [&](char const* txt, float x, float y, float sc = 0.54f, ccColor3B col = {225, 235, 245}) {
        auto* l = CCLabelBMFont::create(txt, "chatFont.fnt");
        l->setAnchorPoint({0.0f, 0.5f});
        l->setScale(sc);
        l->setColor(col);
        l->setPosition({x, y});
        m_mainLayer->addChild(l, 2);
        return l;
    };

    // Left column: Detection, Mode & Audio settings
    float leftX = 22.0f;
    float row1Y = content.height - 58.0f;
    float rowStep = 25.0f;

    addStaticLabel("Mod Status:", leftX, row1Y);
    addRowBtn("Enabled", 142.0f, row1Y, 0.48f, menu_selector(FFCPopup::onToggleEnabled), &m_enabledLabel);

    addStaticLabel("Mode:", leftX, row1Y - rowStep);
    addRowBtn("Fake Estimate", 142.0f, row1Y - rowStep, 0.46f, menu_selector(FFCPopup::onCycleMode), &m_modeLabel, "GJ_button_02.png");

    addStaticLabel("Sensitivity:", leftX, row1Y - rowStep * 2.0f);
    addRowBtn("Balanced", 142.0f, row1Y - rowStep * 2.0f, 0.48f, menu_selector(FFCPopup::onCycleSensitivity), &m_sensLabel);

    addStaticLabel("Releases:", leftX, row1Y - rowStep * 3.0f);
    addRowBtn("Smart", 142.0f, row1Y - rowStep * 3.0f, 0.48f, menu_selector(FFCPopup::onCycleReleaseFilter), &m_releaseLabel);

    addStaticLabel("Max Density:", leftX, row1Y - rowStep * 4.0f);
    addRowBtn("-", 112.0f, row1Y - rowStep * 4.0f, 0.45f, menu_selector(FFCPopup::onDensityAdjust), nullptr, "GJ_button_04.png", -1);
    m_densityLabel = CCLabelBMFont::create("8/s", "bigFont.fnt");
    m_densityLabel->setScale(0.32f);
    m_densityLabel->setPosition({145.0f, row1Y - rowStep * 4.0f});
    m_mainLayer->addChild(m_densityLabel, 3);
    addRowBtn("+", 178.0f, row1Y - rowStep * 4.0f, 0.45f, menu_selector(FFCPopup::onDensityAdjust), nullptr, "GJ_button_04.png", 1);

    addStaticLabel("Min Spacing:", leftX, row1Y - rowStep * 5.0f);
    addRowBtn("-", 112.0f, row1Y - rowStep * 5.0f, 0.45f, menu_selector(FFCPopup::onSpacingAdjust), nullptr, "GJ_button_04.png", -1);
    m_spacingLabel = CCLabelBMFont::create("18u", "bigFont.fnt");
    m_spacingLabel->setScale(0.32f);
    m_spacingLabel->setPosition({145.0f, row1Y - rowStep * 5.0f});
    m_mainLayer->addChild(m_spacingLabel, 3);
    addRowBtn("+", 178.0f, row1Y - rowStep * 5.0f, 0.45f, menu_selector(FFCPopup::onSpacingAdjust), nullptr, "GJ_button_04.png", 1);

    addStaticLabel("Ding Sound:", leftX, row1Y - rowStep * 6.0f);
    addRowBtn("ON", 98.0f, row1Y - rowStep * 6.0f, 0.45f, menu_selector(FFCPopup::onToggleSound), &m_soundLabel);
    addRowBtn("-", 134.0f, row1Y - rowStep * 6.0f, 0.43f, menu_selector(FFCPopup::onVolumeAdjust), nullptr, "GJ_button_04.png", -1);
    m_volumeLabel = CCLabelBMFont::create("85%", "bigFont.fnt");
    m_volumeLabel->setScale(0.28f);
    m_volumeLabel->setPosition({162.0f, row1Y - rowStep * 6.0f});
    m_mainLayer->addChild(m_volumeLabel, 3);
    addRowBtn("+", 190.0f, row1Y - rowStep * 6.0f, 0.43f, menu_selector(FFCPopup::onVolumeAdjust), nullptr, "GJ_button_04.png", 1);

    // Right column: Mega Hack Macro Import & Playback
    float rightX = 222.0f;
    auto* macroHeader = CCLabelBMFont::create("Mega Hack Macro Playback", "goldFont.fnt");
    macroHeader->setScale(0.50f);
    macroHeader->setPosition({316.0f, row1Y});
    m_mainLayer->addChild(macroHeader, 3);

    m_macroFileLabel = CCLabelBMFont::create("No macro loaded", "chatFont.fnt");
    m_macroFileLabel->setScale(0.52f);
    m_macroFileLabel->setColor({210, 235, 255});
    m_macroFileLabel->setPosition({316.0f, row1Y - 20.0f});
    m_mainLayer->addChild(m_macroFileLabel, 3);

    m_macroDetailsLabel = CCLabelBMFont::create("Supports .mhr, .gdr2, .gdr, .json, .zbf", "chatFont.fnt");
    m_macroDetailsLabel->setScale(0.46f);
    m_macroDetailsLabel->setColor({170, 185, 205});
    m_macroDetailsLabel->setPosition({316.0f, row1Y - 36.0f});
    m_mainLayer->addChild(m_macroDetailsLabel, 3);

    m_macroStatusLabel = CCLabelBMFont::create("State: Idle", "chatFont.fnt");
    m_macroStatusLabel->setScale(0.50f);
    m_macroStatusLabel->setColor({150, 255, 170});
    m_macroStatusLabel->setPosition({316.0f, row1Y - 52.0f});
    m_mainLayer->addChild(m_macroStatusLabel, 3);

    addRowBtn("Import Macro", 272.0f, row1Y - 76.0f, 0.48f, menu_selector(FFCPopup::onImportMacro), nullptr, "GJ_button_02.png");
    addRowBtn("Play / Replay", 362.0f, row1Y - 76.0f, 0.48f, menu_selector(FFCPopup::onPlayMacro), nullptr, "GJ_button_01.png");

    addRowBtn("Stop Playback", 272.0f, row1Y - 104.0f, 0.46f, menu_selector(FFCPopup::onStopMacro), nullptr, "GJ_button_06.png");
    addRowBtn("Exp. Real Scan", 362.0f, row1Y - 104.0f, 0.46f, menu_selector(FFCPopup::onRunRealScan), nullptr, "GJ_button_05.png");

    addStaticLabel("Tick Offset:", rightX + 12.0f, row1Y - 132.0f);
    addRowBtn("-", 315.0f, row1Y - 132.0f, 0.44f, menu_selector(FFCPopup::onOffsetAdjust), nullptr, "GJ_button_04.png", -1);
    m_offsetLabel = CCLabelBMFont::create("0", "bigFont.fnt");
    m_offsetLabel->setScale(0.32f);
    m_offsetLabel->setPosition({344.0f, row1Y - 132.0f});
    m_mainLayer->addChild(m_offsetLabel, 3);
    addRowBtn("+", 373.0f, row1Y - 132.0f, 0.44f, menu_selector(FFCPopup::onOffsetAdjust), nullptr, "GJ_button_04.png", 1);

    // Bottom navigation bar
    float botY = 23.0f;
    addRowBtn("Categories (1F-12F+)", 78.0f, botY, 0.48f, menu_selector(FFCPopup::onOpenCategories), nullptr, "GJ_button_02.png");
    addRowBtn("Sound Guide (12F->1F)", 196.0f, botY, 0.48f, menu_selector(FFCPopup::onOpenSoundGuide), nullptr, "GJ_button_03.png");
    addRowBtn("Full Settings", 302.0f, botY, 0.48f, menu_selector(FFCPopup::onOpenGeodeSettings), nullptr, "GJ_button_01.png");
    addRowBtn("Help / Info", 384.0f, botY, 0.48f, menu_selector(FFCPopup::onOpenHelp), nullptr, "GJ_button_04.png");

    refreshUI();
    return true;
}

void FFCPopup::refreshUI() {
    auto& state = ModState::get();
    state.syncFromGeodeSettings();

    if (m_enabledLabel) {
        m_enabledLabel->setString(state.isModEnabled() ? "Enabled" : "Disabled");
    }
    if (m_modeLabel) {
        m_modeLabel->setString(state.isExperimentalRealMode() ? "Exp. Real Scan" : "Fake Estimate");
    }
    if (m_modeNoteLabel) {
        if (state.isExperimentalRealMode()) {
            m_modeNoteLabel->setString("Experimental Real Mode: requires running 'Exp. Real Scan' from 0% with a macro.");
            m_modeNoteLabel->setColor({140, 230, 255});
        } else {
            m_modeNoteLabel->setString("Fake / Visual Estimate Mode: labels (1F-12F+) are simulated estimates for visual fun.");
            m_modeNoteLabel->setColor({255, 225, 130});
        }
    }

    auto const& dcfg = state.detector().config();
    if (m_sensLabel) {
        switch (dcfg.sensitivity) {
            case SensitivityPreset::Selective:  m_sensLabel->setString("Selective"); break;
            case SensitivityPreset::Balanced:   m_sensLabel->setString("Balanced"); break;
            case SensitivityPreset::Permissive: m_sensLabel->setString("Permissive"); break;
        }
    }
    if (m_releaseLabel) {
        switch (dcfg.releaseFilter) {
            case ReleaseFilterMode::SmartGamemode: m_releaseLabel->setString("Smart"); break;
            case ReleaseFilterMode::PressesOnly:   m_releaseLabel->setString("Press Only"); break;
            case ReleaseFilterMode::AllReleases:   m_releaseLabel->setString("All Rel."); break;
        }
    }
    if (m_densityLabel) {
        m_densityLabel->setString(fmt::format("{:.0f}/s", dcfg.maxMomentsPerSecond).c_str());
    }
    if (m_spacingLabel) {
        m_spacingLabel->setString(fmt::format("{:.0f}u", dcfg.minDistanceUnits).c_str());
    }

    bool sndEn = Mod::get()->getSettingValue<bool>("sound-enabled");
    int volPct = static_cast<int>(std::round(Mod::get()->getSettingValue<double>("sound-volume") * 100.0));
    if (m_soundLabel) m_soundLabel->setString(sndEn ? "ON" : "MUTE");
    if (m_volumeLabel) m_volumeLabel->setString(fmt::format("{}%", volPct).c_str());

    auto const& mp = state.macroPlayer();
    if (mp.hasMacro() && mp.macro()) {
        m_macroFileLabel->setString(mp.macro()->name.c_str());
        m_macroDetailsLabel->setString(
            fmt::format("{} | {} inputs | {:.0f} TPS",
                macroFormatName(mp.macro()->format),
                mp.macro()->inputs.size(),
                mp.effectiveTps()
            ).c_str()
        );
    } else {
        m_macroFileLabel->setString("No macro loaded");
        m_macroDetailsLabel->setString("Supports .mhr, .gdr2, .gdr, .json, .zbf");
    }

    if (m_macroStatusLabel) {
        if (state.realAnalyzer().isActive()) {
            m_macroStatusLabel->setString(state.realAnalyzer().statusText().c_str());
        } else {
            m_macroStatusLabel->setString(mp.statusSummary().c_str());
        }
    }
    if (m_offsetLabel) {
        int off = mp.frameOffset();
        m_offsetLabel->setString(fmt::format("{:+d}", off).c_str());
    }
}

void FFCPopup::onToggleEnabled(CCObject*) {
    auto& state = ModState::get();
    state.setModEnabled(!state.isModEnabled());
    if (auto* pl = PlayLayer::get()) {
        if (auto* ov = FFCOverlay::find(pl)) ov->refreshCounterDisplay();
    }
    refreshUI();
}

void FFCPopup::onCycleMode(CCObject*) {
    auto& state = ModState::get();
    state.setExperimentalRealMode(!state.isExperimentalRealMode());
    refreshUI();
}

void FFCPopup::onCycleSensitivity(CCObject*) {
    auto cur = Mod::get()->getSettingValue<std::string>("sensitivity-preset");
    std::string next = "Balanced";
    if (cur == "Selective") next = "Balanced";
    else if (cur == "Balanced") next = "Permissive";
    else next = "Selective";
    Mod::get()->setSettingValue<std::string>("sensitivity-preset", next);
    refreshUI();
}

void FFCPopup::onCycleReleaseFilter(CCObject*) {
    auto cur = Mod::get()->getSettingValue<std::string>("release-filter");
    std::string next = "Smart (Wave/Ship/Dash/Robot)";
    if (cur.find("Smart") != std::string::npos) next = "Presses Only";
    else if (cur == "Presses Only") next = "All Releases";
    else next = "Smart (Wave/Ship/Dash/Robot)";
    Mod::get()->setSettingValue<std::string>("release-filter", next);
    refreshUI();
}

void FFCPopup::onDensityAdjust(CCObject* sender) {
    int dir = sender->getTag();
    double cur = Mod::get()->getSettingValue<double>("max-moments-per-second");
    double next = std::clamp(cur + static_cast<double>(dir), 1.0, 25.0);
    Mod::get()->setSettingValue<double>("max-moments-per-second", next);
    refreshUI();
}

void FFCPopup::onSpacingAdjust(CCObject* sender) {
    int dir = sender->getTag();
    double cur = Mod::get()->getSettingValue<double>("min-distance-units");
    double next = std::clamp(cur + static_cast<double>(dir) * 4.0, 0.0, 90.0);
    Mod::get()->setSettingValue<double>("min-distance-units", next);
    refreshUI();
}

void FFCPopup::onToggleSound(CCObject*) {
    bool cur = Mod::get()->getSettingValue<bool>("sound-enabled");
    Mod::get()->setSettingValue<bool>("sound-enabled", !cur);
    refreshUI();
}

void FFCPopup::onVolumeAdjust(CCObject* sender) {
    int dir = sender->getTag();
    double cur = Mod::get()->getSettingValue<double>("sound-volume");
    double next = std::clamp(std::round((cur + static_cast<double>(dir) * 0.10) * 20.0) / 20.0, 0.0, 1.0);
    Mod::get()->setSettingValue<double>("sound-volume", next);
    refreshUI();
    SoundManager::get().playCategoryDing(12, true);
}

void FFCPopup::onImportMacro(CCObject*) {
    file::FilePickOptions opts;
    opts.filters.push_back(file::FilePickOptions::Filter{
        .description = "GD & Mega Hack Macros (*.mhr, *.gdr2, *.gdr, *.json, *.zbf, *.re3, *.xd, *.txt)",
        .files = {"*.mhr", "*.gdr2", "*.gdr", "*.json", "*.zbf", "*.re3", "*.xd", "*.txt"}
    });

    m_macroPickerHolder.spawn(
        file::pick(file::PickMode::OpenFile, opts),
        [this](file::PickResult res) {
            if (!res.isOk()) {
                Notification::create("Macro file dialog failed", NotificationIcon::Error)->show();
                return;
            }
            auto optPath = res.unwrap();
            if (!optPath.has_value()) return;

            std::string err;
            if (ModState::get().macroPlayer().loadFromFile(*optPath, err)) {
                auto const* m = ModState::get().macroPlayer().macro();
                Notification::create(
                    fmt::format("Loaded {} ({} inputs)", m->name, m->inputs.size()),
                    NotificationIcon::Success
                )->show();
            } else {
                FLAlertLayer::create(
                    "Macro Import Error",
                    fmt::format("Could not load macro file:\n<cr>{}</c>", err),
                    "OK"
                )->show();
            }
            this->refreshUI();
        }
    );
}

void FFCPopup::onPlayMacro(CCObject*) {
    auto* pl = PlayLayer::get();
    std::string err;
    if (!ModState::get().startMacroPlaybackInLevel(pl, err)) {
        FLAlertLayer::create("Macro Playback", err, "OK")->show();
        return;
    }
    Notification::create("Macro playback started from 0% (resume level to watch)", NotificationIcon::Success)->show();
    refreshUI();
}

void FFCPopup::onStopMacro(CCObject*) {
    auto* pl = PlayLayer::get();
    ModState::get().stopMacroPlaybackInLevel(pl);
    ModState::get().stopExperimentalRealScan(pl);
    Notification::create("Macro playback stopped", NotificationIcon::Info)->show();
    refreshUI();
}

void FFCPopup::onOffsetAdjust(CCObject* sender) {
    int dir = sender->getTag();
    int cur = static_cast<int>(Mod::get()->getSettingValue<int64_t>("macro-frame-offset"));
    int next = std::clamp(cur + dir, -10, 10);
    Mod::get()->setSettingValue<int64_t>("macro-frame-offset", next);
    refreshUI();
}

void FFCPopup::onRunRealScan(CCObject*) {
    auto* pl = PlayLayer::get();
    std::string err;
    ModState::get().setExperimentalRealMode(true);
    if (!ModState::get().startExperimentalRealScan(pl, err)) {
        FLAlertLayer::create("Experimental Real Scan", err, "OK")->show();
        return;
    }
    Notification::create("Experimental Real Scan started (unpause level to run)", NotificationIcon::Info)->show();
    refreshUI();
}

void FFCPopup::onOpenCategories(CCObject*) {
    if (auto* pop = CategoryConfigPopup::create()) {
        pop->show();
    }
}

void FFCPopup::onOpenSoundGuide(CCObject*) {
    if (auto* pop = SoundGuidePopup::create()) {
        pop->show();
    }
}

void FFCPopup::onOpenGeodeSettings(CCObject*) {
    geode::openSettingsPopup(Mod::get());
}

void FFCPopup::onOpenHelp(CCObject*) {
    FLAlertLayer::create(
        "Fake Frame Counter - Info",
        "<cy>Fake / Visual Estimate Mode (Primary):</c>\n"
        "Generates plausible simulated frame-window labels (<cr>1F</c>-<cb>12F+</c>) from obstacle clearance, speed, mini/gamemode state, and click cadence. Gameplay HUD omits disclaimer text to stay clean.\n\n"
        "<cg>Anti-Spam Filter:</c>\n"
        "Deduplicates held/same-tick/dual-mirror events, ignores non-meaningful releases on Cube/Ball/UFO/Spider, enforces minimum spatial spacing, and caps sustained spam while preserving rapid bursts.\n\n"
        "<co>Experimental Real Scan:</c>\n"
        "Uses deterministic macro step-perturbation from 0% (non-platformer, no StartPos) to measure survival windows.",
        "OK"
    )->show();
}

} // namespace ffc
