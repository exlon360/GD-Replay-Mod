#pragma once

#include "ReplayTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>
#include <optional>

class GJGameLevel;
class PlayLayer;

namespace geode {
class ScrollLayer;
class TextInput;
}

namespace replay_mod {

using ReplayLaunchCallback = std::function<void()>;

CCMenuItemSpriteExtra* createReplayEntryButton(
    cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler callback
);

void placeReplayEntryButton(
    cocos2d::CCMenu* menu,
    CCMenuItemSpriteExtra* replayButton,
    cocos2d::CCSprite* knownPlaySprite = nullptr
);

class ReplayViewerPopup final : public geode::Popup {
public:
    static ReplayViewerPopup* create(GJGameLevel* level, ReplayLaunchCallback launchReplay);

protected:
    bool init(GJGameLevel* level, ReplayLaunchCallback launchReplay);
    void onLatest(cocos2d::CCObject* sender);
    void onLibrary(cocos2d::CCObject* sender);
    void onInfo(cocos2d::CCObject* sender);

private:
    bool queueAndLaunch(ReplayRun const& summary);

    GJGameLevel* m_level = nullptr;
    ReplayLaunchCallback m_launchReplay;
    std::optional<ReplayRun> m_latest;
};

class ReplayLibraryPopup final : public geode::Popup {
public:
    static ReplayLibraryPopup* create(GJGameLevel* level, ReplayLaunchCallback launchReplay);

protected:
    bool init(GJGameLevel* level, ReplayLaunchCallback launchReplay);
    void rebuild(std::string const& query);
    void onWatch(cocos2d::CCObject* sender);

private:
    bool queueAndLaunch(ReplayRun const& summary);

    GJGameLevel* m_level = nullptr;
    ReplayLaunchCallback m_launchReplay;
    geode::TextInput* m_searchInput = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    std::vector<ReplayRun> m_allRuns;
    std::vector<std::size_t> m_visibleIndices;
};

class ReplayPlaybackOverlay final : public cocos2d::CCNode {
public:
    static ReplayPlaybackOverlay* create(PlayLayer* layer);
    static void refreshActive();

    bool init(PlayLayer* layer);
    void onExit() override;
    void refresh();

private:
    void onPause(cocos2d::CCObject* sender);
    void onSlow(cocos2d::CCObject* sender);
    void onFast(cocos2d::CCObject* sender);
    void onRestart(cocos2d::CCObject* sender);
    void onHitboxes(cocos2d::CCObject* sender);
    void onBack(cocos2d::CCObject* sender);

    PlayLayer* m_layer = nullptr;
    cocos2d::CCLabelBMFont* m_stateLabel = nullptr;
    cocos2d::CCLabelBMFont* m_speedLabel = nullptr;
    cocos2d::CCLabelBMFont* m_freezeLabel = nullptr;
    ButtonSprite* m_pauseSprite = nullptr;
    ButtonSprite* m_hitboxSprite = nullptr;
};

} // namespace replay_mod
