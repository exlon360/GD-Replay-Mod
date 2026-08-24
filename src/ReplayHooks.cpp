#include "ReplaySession.hpp"
#include "ReplayUI.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelPage.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using namespace replay_mod;

namespace {

void showPausedReplayControls(PlayLayer* layer, CCNode* parent) {
    if (!layer || !parent) return;
    auto const mode = ReplaySession::get().mode();
    if (mode != SessionMode::Paused && mode != SessionMode::DeathPaused &&
        mode != SessionMode::Finished) {
        return;
    }
    if (parent->getChildByID("xviongd.replay-mod/replay-viewer-overlay")) return;
    if (auto overlay = ReplayPlaybackOverlay::create(layer)) {
        parent->addChild(overlay, 1000);
    }
}

} // namespace

class $modify(ReplayLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        bool launchingQueuedReplay = false;
    };

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (m_playBtnMenu && m_level) {
            auto button = createReplayEntryButton(
                this,
                menu_selector(ReplayLevelInfoLayer::onReplayViewer)
            );
            placeReplayEntryButton(m_playBtnMenu, button, m_playSprite);
        }
        return true;
    }

    void onReplayViewer(CCObject*) {
        if (!m_level) return;
        auto launch = [self = Ref(this)] {
            if (self->m_level) self->launchQueuedReplay();
        };
        if (auto popup = ReplayViewerPopup::create(m_level, std::move(launch))) popup->show();
    }

    void launchQueuedReplay() {
        m_fields->launchingQueuedReplay = true;
        this->onPlay(nullptr);
    }

    void onPlay(CCObject* sender) {
        auto const keepQueuedReplay = m_fields->launchingQueuedReplay;
        m_fields->launchingQueuedReplay = false;
        if (!keepQueuedReplay) ReplaySession::get().cancelQueuedPlayback();
        LevelInfoLayer::onPlay(sender);
    }
};

class $modify(ReplayLevelPage, LevelPage) {
    struct Fields {
        bool launchingQueuedReplay = false;
    };

    bool init(GJGameLevel* level) {
        if (!LevelPage::init(level)) return false;
        if (m_levelMenu && m_level) {
            auto button = createReplayEntryButton(
                this,
                menu_selector(ReplayLevelPage::onReplayViewer)
            );
            placeReplayEntryButton(m_levelMenu, button);
        }
        return true;
    }

    void onReplayViewer(CCObject*) {
        if (!m_level) return;
        auto launch = [self = Ref(this)] {
            if (self->m_level) self->launchQueuedReplay();
        };
        if (auto popup = ReplayViewerPopup::create(m_level, std::move(launch))) popup->show();
    }

    void launchQueuedReplay() {
        m_fields->launchingQueuedReplay = true;
        this->onPlay(nullptr);
    }

    void onPlay(CCObject* sender) {
        auto const keepQueuedReplay = m_fields->launchingQueuedReplay;
        m_fields->launchingQueuedReplay = false;
        if (!keepQueuedReplay) ReplaySession::get().cancelQueuedPlayback();
        LevelPage::onPlay(sender);
    }
};

class $modify(ReplayBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (ReplaySession::get().shouldForwardInput(this, down, button, isPlayer1)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto const reachedEnd = ReplaySession::get().beforeProcessCommands(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (!reachedEnd) return;

        if (auto layer = PlayLayer::get();
            layer && static_cast<GJBaseGameLayer*>(layer) ==
                static_cast<GJBaseGameLayer*>(this)) {
            CCNode* parent = layer->m_uiLayer
                ? static_cast<CCNode*>(layer->m_uiLayer)
                : static_cast<CCNode*>(layer);
            showPausedReplayControls(layer, parent);
        }
    }
};

class $modify(ReplayPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto& session = ReplaySession::get();
        auto const preparedQueuedReplay = session.prepareQueuedPlayback(level);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            if (preparedQueuedReplay) session.cancelQueuedPlayback();
            return false;
        }

        auto const hasQueuedReplay = session.onLayerInit(this);
        if (hasQueuedReplay) {
            Loader::get()->queueInMainThread([layer = Ref(this)] {
                if (!layer || PlayLayer::get() != layer.data()) return;

                std::string error;
                if (!ReplaySession::get().startQueuedPlayback(layer, error)) {
                    FLAlertLayer::create("Replay Viewer", error, "OK")->show();
                    layer->onQuit();
                }
            });
        }
        return true;
    }

    void resetLevel() {
        ReplaySession::get().onLayerWillReset(this);
        PlayLayer::resetLevel();
        ReplaySession::get().onLayerDidReset(this);
    }

    void resetLevelFromStart() {
        ReplaySession::get().onLayerWillReset(this);
        PlayLayer::resetLevelFromStart();
        ReplaySession::get().onLayerDidReset(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& session = ReplaySession::get();
        if (session.isPlaybackSession()) {
            PlayLayer::destroyPlayer(player, object);
            session.onPlaybackDeath(this);
            CCNode* parent = m_uiLayer
                ? static_cast<CCNode*>(m_uiLayer)
                : static_cast<CCNode*>(this);
            showPausedReplayControls(this, parent);
            ReplayPlaybackOverlay::refreshActive();
            return;
        }

        session.onRecordingDeath(this);
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto& session = ReplaySession::get();
        if (session.isPlaybackSession()) {
            session.onPlaybackComplete(this);
            CCNode* parent = m_uiLayer
                ? static_cast<CCNode*>(m_uiLayer)
                : static_cast<CCNode*>(this);
            showPausedReplayControls(this, parent);
            ReplayPlaybackOverlay::refreshActive();
            return;
        }

        session.onRecordingComplete(this);
        PlayLayer::levelComplete();
    }

    void onQuit() {
        ReplaySession::get().onLayerWillExit(this);
        PlayLayer::onQuit();
        ReplaySession::get().onLayerDidExit(this);
    }

    void onExit() {
        ReplaySession::get().onLayerWillExit(this);
        PlayLayer::onExit();
        ReplaySession::get().onLayerDidExit(this);
    }
};

class $modify(ReplayPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto& session = ReplaySession::get();
        if (session.mode() != SessionMode::Playback) return;

        session.togglePause();
        showPausedReplayControls(PlayLayer::get(), this);
        ReplayPlaybackOverlay::refreshActive();
    }

    void onResume(CCObject* sender) {
        if (ReplaySession::get().mode() == SessionMode::Paused) {
            ReplaySession::get().togglePause();
        }
        PauseLayer::onResume(sender);
    }

    void keyBackClicked() {
        if (ReplaySession::get().mode() == SessionMode::Paused) {
            ReplaySession::get().togglePause();
        }
        PauseLayer::keyBackClicked();
    }

    void onRestart(CCObject* sender) {
        if (!ReplaySession::get().isPlaybackSession()) {
            PauseLayer::onRestart(sender);
            return;
        }

        std::string error;
        if (!ReplaySession::get().restartPlayback(PlayLayer::get(), error)) {
            FLAlertLayer::create("Replay Viewer", error, "OK")->show();
            return;
        }
        this->onResume(nullptr);
    }

    void onRestartFull(CCObject* sender) {
        if (!ReplaySession::get().isPlaybackSession()) {
            PauseLayer::onRestartFull(sender);
            return;
        }
        this->onRestart(sender);
    }

    void onNormalMode(CCObject* sender) {
        if (ReplaySession::get().isPlaybackSession()) {
            FLAlertLayer::create(
                "Replay Viewer",
                "Exit the replay before switching modes.",
                "OK"
            )->show();
            return;
        }
        PauseLayer::onNormalMode(sender);
    }

    void onPracticeMode(CCObject* sender) {
        if (ReplaySession::get().isPlaybackSession()) {
            FLAlertLayer::create(
                "Replay Viewer",
                "Exit the replay before switching modes.",
                "OK"
            )->show();
            return;
        }
        PauseLayer::onPracticeMode(sender);
    }
};
