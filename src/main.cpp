#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include "ModState.hpp"
#include "Overlay.hpp"
#include "Popups.hpp"

using namespace geode::prelude;
using namespace ffc;

namespace {

PreInputPlayerState capturePreInputState(PlayerObject* p, bool down) {
    PreInputPlayerState pre{};
    if (!p) return pre;
    pre.valid = true;
    pre.wasOnGround = p->m_isOnGround || p->m_isOnGround2;
    pre.wasDashing = (p->m_dashRing != nullptr);
    pre.wasTouchingRing = down && (p->m_touchingRings != nullptr && p->m_touchingRings->count() > 0);
    return pre;
}

} // namespace

$on_mod(Loaded) {
    ModState::get().syncFromGeodeSettings();

    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) {
        ModState::get().syncFromGeodeSettings();
        if (auto* pl = PlayLayer::get()) {
            if (auto* ov = FFCOverlay::find(pl)) {
                ov->refreshCounterDisplay();
                ov->updateOverlay();
            }
        }
    });

    listenForKeybindSettingPresses("open-menu-keybind", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        if (auto* pop = FFCPopup::create()) {
            pop->show();
        }
    });

    listenForKeybindSettingPresses("toggle-overlay-keybind", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        ModState::get().toggleOverlayHiddenByKey();
        bool hidden = ModState::get().isOverlayHiddenByKey();
        if (auto* pl = PlayLayer::get()) {
            if (auto* ov = FFCOverlay::find(pl)) {
                ov->refreshCounterDisplay();
                ov->updateOverlay();
            }
        }
        Notification::create(
            hidden ? "FFC Overlay Hidden" : "FFC Overlay Visible",
            NotificationIcon::Info,
            0.6f
        )->show();
    });
}

class $modify(FFCBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (isHalfTick) return;

        auto* pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) return;

        auto& state = ModState::get();
        if (state.isResettingLevel() || !pl->m_started || pl->m_isPaused) return;

        int64_t tick = static_cast<int64_t>(this->m_gameState.m_currentProgress / 2);

        if (state.macroPlayer().isPlaying() && !pl->m_playerDied && !pl->m_levelEndAnimationStarted) {
            auto due = state.macroPlayer().collectDueInputs(tick);
            for (auto const& act : due) {
                PlayerObject* p = (act.player2 && pl->m_player2) ? pl->m_player2 : pl->m_player1;
                PreInputPlayerState pre = capturePreInputState(p, act.down);

                state.setInjectingInput(true);
                this->handleButton(act.down, static_cast<int>(act.button), !act.player2);
                state.setInjectingInput(false);

                state.processPlayerInput(pl, p, act.down, static_cast<int>(act.button), act.player2, act.macroIndex, pre);
            }
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto* pl = PlayLayer::get();
        bool isActivePlayLayer = (pl && static_cast<GJBaseGameLayer*>(pl) == this);
        bool isP2 = !isPlayer1;
        PlayerObject* p = isActivePlayLayer ? ((isP2 && pl->m_player2) ? pl->m_player2 : pl->m_player1) : nullptr;
        PreInputPlayerState pre = capturePreInputState(p, down);

        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        auto& state = ModState::get();
        if (!isActivePlayLayer || state.isResettingLevel() || state.isInjectingInput() ||
            state.macroPlayer().isPlaying()) {
            return;
        }
        if (!pl->m_started || pl->m_isPaused) return;

        state.processPlayerInput(pl, p, down, button, isP2, std::nullopt, pre);
    }
};

class $modify(FFCPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto& state = ModState::get();
        state.syncFromGeodeSettings();
        state.resetAttemptCounts();
        FFCOverlay::ensure(this);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& state = ModState::get();
        if (state.isResettingLevel()) {
            return;
        }

        if (auto* ov = FFCOverlay::ensure(this)) {
            ov->updateOverlay();
        }
    }

    void resetLevel() {
        auto& state = ModState::get();
        bool wasResetting = state.isResettingLevel();
        state.setResettingLevel(true);
        PlayLayer::resetLevel();
        state.setResettingLevel(wasResetting);

        state.resetAttemptCounts();
        bool autoReplay = Mod::get()->getSettingValue<bool>("macro-auto-replay");
        state.macroPlayer().onLevelReset(autoReplay);

        if (auto* ov = FFCOverlay::find(this)) {
            ov->onReset();
        }
    }

    void onQuit() {
        auto& state = ModState::get();
        state.stopExperimentalRealScan(this, false);
        state.stopMacroPlaybackInLevel(this);
        PlayLayer::onQuit();
    }
};

class $modify(FFCPauseLayer, PauseLayer) {
    void onOpenFFCPopup(CCObject*) {
        if (auto* pop = FFCPopup::create()) {
            pop->show();
        }
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto* spr = ButtonSprite::create("FFC", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        spr->setScale(0.62f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FFCPauseLayer::onOpenFFCPopup));
        btn->setID("nananqj96.fake-frame-counter/pause-button");

        if (auto* rightMenu = this->getChildByID("right-button-menu")) {
            rightMenu->addChild(btn);
            rightMenu->updateLayout();
        } else {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto* fallbackMenu = CCMenu::create();
            fallbackMenu->setPosition({0.0f, 0.0f});
            btn->setPosition({winSize.width - 42.0f, 92.0f});
            fallbackMenu->addChild(btn);
            this->addChild(fallbackMenu, 10);
        }
    }
};
