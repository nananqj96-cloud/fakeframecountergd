#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

#include <vector>

namespace ffc {

class SoundGuidePopup : public geode::Popup {
protected:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

    void onGuideTick(float dt);
    void renderCurrentStep(bool playAudio);

    void onPlayPause(cocos2d::CCObject* sender);
    void onRestartGuide(cocos2d::CCObject* sender);
    void onPrevStep(cocos2d::CCObject* sender);
    void onNextStep(cocos2d::CCObject* sender);

    std::vector<int> m_descendingFrames;
    int m_currentIndex = 0;
    bool m_playing = true;

    cocos2d::CCDrawNode* m_previewCircle = nullptr;
    cocos2d::CCLabelBMFont* m_previewShortLabel = nullptr;
    cocos2d::CCLabelBMFont* m_previewTitleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_previewSubLabel = nullptr;
    cocos2d::CCLabelBMFont* m_playPauseBtnLabel = nullptr;
    cocos2d::CCNode* m_stripContainer = nullptr;

public:
    static SoundGuidePopup* create();
};

class CategoryConfigPopup : public geode::Popup {
protected:
    bool init() override;
    void rebuildList();

    void onPrevPage(cocos2d::CCObject* sender);
    void onNextPage(cocos2d::CCObject* sender);
    void onToggleCategory(cocos2d::CCObject* sender);
    void onPreviewDing(cocos2d::CCObject* sender);
    void onPickCustomSound(cocos2d::CCObject* sender);
    void onCycleCustomColor(cocos2d::CCObject* sender);
    void onDeleteCustomCategory(cocos2d::CCObject* sender);
    void onAddCustomCategory(cocos2d::CCObject* sender);
    void onEnableAll(cocos2d::CCObject* sender);
    void onEnableTightOnly(cocos2d::CCObject* sender);
    void onResetCategories(cocos2d::CCObject* sender);

    int m_page = 0;
    cocos2d::CCNode* m_listContainer = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    geode::TextInput* m_customFrameInput = nullptr;
    geode::async::TaskHolder<geode::utils::file::PickResult> m_soundPickerHolder;

public:
    static CategoryConfigPopup* create();
};

class FFCPopup : public geode::Popup {
protected:
    bool init() override;
    void refreshUI();

    void onToggleEnabled(cocos2d::CCObject* sender);
    void onCycleMode(cocos2d::CCObject* sender);
    void onCycleSensitivity(cocos2d::CCObject* sender);
    void onCycleReleaseFilter(cocos2d::CCObject* sender);
    void onDensityAdjust(cocos2d::CCObject* sender);
    void onSpacingAdjust(cocos2d::CCObject* sender);
    void onToggleSound(cocos2d::CCObject* sender);
    void onVolumeAdjust(cocos2d::CCObject* sender);

    void onImportMacro(cocos2d::CCObject* sender);
    void onPlayMacro(cocos2d::CCObject* sender);
    void onStopMacro(cocos2d::CCObject* sender);
    void onOffsetAdjust(cocos2d::CCObject* sender);
    void onRunRealScan(cocos2d::CCObject* sender);

    void onOpenCategories(cocos2d::CCObject* sender);
    void onOpenSoundGuide(cocos2d::CCObject* sender);
    void onOpenGeodeSettings(cocos2d::CCObject* sender);
    void onOpenHelp(cocos2d::CCObject* sender);

    cocos2d::CCLabelBMFont* m_enabledLabel = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_modeNoteLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sensLabel = nullptr;
    cocos2d::CCLabelBMFont* m_releaseLabel = nullptr;
    cocos2d::CCLabelBMFont* m_densityLabel = nullptr;
    cocos2d::CCLabelBMFont* m_spacingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_soundLabel = nullptr;
    cocos2d::CCLabelBMFont* m_volumeLabel = nullptr;
    geode::TextInput* m_volumeInput = nullptr;

    cocos2d::CCLabelBMFont* m_macroFileLabel = nullptr;
    cocos2d::CCLabelBMFont* m_macroDetailsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_macroStatusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_offsetLabel = nullptr;

    geode::async::TaskHolder<geode::utils::file::PickResult> m_macroPickerHolder;

public:
    static FFCPopup* create();
};

} // namespace ffc
