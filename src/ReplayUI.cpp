#include "ReplayUI.hpp"

#include "ReplaySession.hpp"
#include "ReplayStore.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace replay_mod {
namespace {

constexpr cocos2d::ccColor3B kPurple { 181, 76, 255 };

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

CCMenuItemSpriteExtra* addButton(
    CCMenu* menu,
    CCObject* target,
    SEL_MenuHandler callback,
    char const* text,
    CCPoint offset,
    ButtonSprite** outputSprite = nullptr,
    int width = 105
) {
    auto sprite = ButtonSprite::create(text, width, 0, .65f, true);
    sprite->setScale(.72f);
    sprite->setColor(kPurple);
    auto button = CCMenuItemSpriteExtra::create(sprite, target, callback);
    menu->addChildAtPosition(button, Anchor::Center, offset);
    if (outputSprite) *outputSprite = sprite;
    return button;
}

std::string runLabel(ReplayRun const& run) {
    return fmt::format(
        "Attempt {}  |  {:.1f}%  |  {} inputs{}",
        run.attempt,
        run.progress,
        run.storedEventCount,
        run.completed ? "  |  COMPLETE" : ""
    );
}

} // namespace

ReplayHud* ReplayHud::create(PlayLayer* layer) {
    auto result = new ReplayHud();
    if (result->init(layer)) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool ReplayHud::init(PlayLayer* layer) {
    if (!CCNode::init()) return false;
    m_layer = layer;

    auto const size = CCDirector::get()->getWinSize();
    this->setContentSize(size);
    this->setAnchorPoint({ 0.0f, 0.0f });

    auto menu = CCMenu::create();
    menu->ignoreAnchorPointForPosition(false);
    menu->setAnchorPoint({ 0.0f, 0.0f });
    menu->setPosition({ 0.0f, 0.0f });
    menu->setContentSize(size);
    this->addChild(menu);

    auto icon = CCSprite::create("replay-icon.png"_spr);
    if (!icon) icon = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
    auto const longestSide = std::max(icon->getContentWidth(), icon->getContentHeight());
    if (longestSide > 0.0f) icon->setScale(31.0f / longestSide);

    auto button = CCMenuItemSpriteExtra::create(
        icon,
        this,
        menu_selector(ReplayHud::onOpen)
    );
    button->setPosition({ 25.0f, size.height - 25.0f });
    menu->addChild(button);

    m_statusLabel = CCLabelBMFont::create("READY", "bigFont.fnt");
    m_statusLabel->setAnchorPoint({ 0.0f, 0.5f });
    m_statusLabel->setScale(.28f);
    m_statusLabel->setColor(kPurple);
    m_statusLabel->setPosition({ 46.0f, size.height - 25.0f });
    this->addChild(m_statusLabel);

    this->scheduleUpdate();
    refresh();
    return true;
}

void ReplayHud::update(float) {
    refresh();
}

void ReplayHud::refresh() {
    auto const status = ReplaySession::get().statusText();
    if (status == m_lastStatus) return;
    m_lastStatus = status;
    m_statusLabel->setString(status.c_str());
}

void ReplayHud::onOpen(CCObject*) {
    if (auto popup = ReplayControlPopup::create(m_layer)) popup->show();
}

ReplayControlPopup* ReplayControlPopup::create(PlayLayer* layer) {
    auto result = new ReplayControlPopup();
    if (result->init(layer)) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool ReplayControlPopup::init(PlayLayer* layer) {
    if (!Popup::init(360.0f, 220.0f)) return false;
    m_layer = layer;
    this->setTitle("Replay Mod");

    m_stateLabel = CCLabelBMFont::create("READY", "bigFont.fnt");
    m_stateLabel->setScale(.42f);
    m_stateLabel->setColor(kPurple);
    m_mainLayer->addChildAtPosition(m_stateLabel, Anchor::Center, { 0.0f, 53.0f });

    auto explanation = CCLabelBMFont::create("INPUT REPLAY - NOT VIDEO", "chatFont.fnt");
    explanation->setScale(.45f);
    explanation->setOpacity(185);
    m_mainLayer->addChildAtPosition(explanation, Anchor::Center, { 0.0f, 34.0f });

    addButton(m_buttonMenu, this, menu_selector(ReplayControlPopup::onLatest), "Play Latest", { -77.0f, 5.0f });
    addButton(m_buttonMenu, this, menu_selector(ReplayControlPopup::onLibrary), "Library", { 77.0f, 5.0f });
    addButton(m_buttonMenu, this, menu_selector(ReplayControlPopup::onPause), "Pause", { -112.0f, -39.0f }, &m_pauseSprite, 92);
    addButton(m_buttonMenu, this, menu_selector(ReplayControlPopup::onSpeed), "Speed 1x", { 0.0f, -39.0f }, &m_speedSprite, 104);
    addButton(m_buttonMenu, this, menu_selector(ReplayControlPopup::onHitboxes), "Hitboxes Off", { 112.0f, -39.0f }, &m_hitboxSprite, 116);
    addButton(m_buttonMenu, this, menu_selector(ReplayControlPopup::onStop), "Stop Replay", { 0.0f, -78.0f }, nullptr, 118);

    refresh();
    return true;
}

void ReplayControlPopup::refresh() {
    auto& session = ReplaySession::get();
    m_stateLabel->setString(session.statusText().c_str());
    m_pauseSprite->setString(session.mode() == SessionMode::Paused ? "Resume" : "Pause");
    m_speedSprite->setString(fmt::format("Speed {:.2g}x", session.speed()).c_str());
    auto const hitboxesEnabled = m_layer && m_layer->m_isDebugDrawEnabled;
    auto const hitboxesVisible = m_layer &&
        (session.isPlaybackSession() || m_layer->m_isPracticeMode);
    m_hitboxSprite->setString(
        hitboxesEnabled ? (hitboxesVisible ? "Hitboxes On" : "Hitboxes Armed") : "Hitboxes Off"
    );
}

void ReplayControlPopup::onLatest(CCObject*) {
    std::string error;
    if (!ReplaySession::get().startLatest(m_layer, error)) {
        FLAlertLayer::create("Replay Mod", error, "OK")->show();
        return;
    }
    Notification::create("Watching latest replay", NotificationIcon::Info, 2.0f)->show();
    this->onClose(nullptr);
}

void ReplayControlPopup::onLibrary(CCObject*) {
    if (auto popup = ReplayLibraryPopup::create(m_layer)) popup->show();
}

void ReplayControlPopup::onPause(CCObject*) {
    if (!ReplaySession::get().togglePause()) {
        Notification::create("Start a replay first", NotificationIcon::Info, 2.0f)->show();
    }
    refresh();
}

void ReplayControlPopup::onSpeed(CCObject*) {
    auto& session = ReplaySession::get();
    auto const before = session.speed();
    auto const after = session.cycleSpeed();
    if (before == after && !session.isPlaybackSession()) {
        Notification::create("Speed controls apply during replay", NotificationIcon::Info, 2.0f)->show();
    }
    refresh();
}

void ReplayControlPopup::onHitboxes(CCObject*) {
    if (m_layer) {
        m_layer->toggleDebugDraw();
        if (m_layer->m_isDebugDrawEnabled &&
            !ReplaySession::get().isPlaybackSession() &&
            !m_layer->m_isPracticeMode) {
            Notification::create(
                "Hitboxes will show during replay, practice, or death",
                NotificationIcon::Info,
                3.0f
            )->show();
        }
    }
    refresh();
}

void ReplayControlPopup::onStop(CCObject*) {
    if (!ReplaySession::get().stopPlayback(m_layer)) {
        Notification::create("No replay is active", NotificationIcon::Info, 2.0f)->show();
        return;
    }
    Notification::create("Replay stopped", NotificationIcon::Success, 2.0f)->show();
    this->onClose(nullptr);
}

ReplayLibraryPopup* ReplayLibraryPopup::create(PlayLayer* layer) {
    auto result = new ReplayLibraryPopup();
    if (result->init(layer)) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool ReplayLibraryPopup::init(PlayLayer* layer) {
    if (!Popup::init(430.0f, 280.0f)) return false;
    m_layer = layer;
    this->setTitle("Saved Replays");
    m_allRuns = ReplayStore::listSummaries();

    m_searchInput = TextInput::create(310.0f, "Search level or attempt...");
    m_searchInput->setScale(.7f);
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setTextAlign(TextInputAlign::Left);
    m_searchInput->setCallback([this](std::string const& value) {
        rebuild(value);
    });
    m_mainLayer->addChildAtPosition(m_searchInput, Anchor::Top, { 0.0f, -49.0f });

    m_scroll = ScrollLayer::create({ 374.0f, 158.0f });
    m_scroll->setStealingTouches(true);
    m_mainLayer->addChildAtPosition(
        m_scroll,
        Anchor::BottomLeft,
        { 28.0f, 45.0f },
        { 0.0f, 0.0f }
    );

    m_countLabel = CCLabelBMFont::create("0 saved attempts", "chatFont.fnt");
    m_countLabel->setScale(.5f);
    m_countLabel->setOpacity(190);
    m_mainLayer->addChildAtPosition(m_countLabel, Anchor::Bottom, { 0.0f, 24.0f });

    rebuild("");
    return true;
}

void ReplayLibraryPopup::rebuild(std::string const& query) {
    auto const normalized = lower(query);
    m_visibleIndices.clear();

    for (std::size_t index = 0; index < m_allRuns.size(); ++index) {
        auto const& run = m_allRuns[index];
        auto const searchable = lower(fmt::format(
            "{} {} attempt {} {}",
            run.levelName,
            run.levelID,
            run.attempt,
            run.completed ? "complete" : ""
        ));
        if (normalized.empty() || searchable.find(normalized) != std::string::npos) {
            m_visibleIndices.push_back(index);
        }
    }

    m_scroll->m_contentLayer->removeAllChildren();
    auto const rowHeight = 38.0f;
    auto const contentHeight = std::max(m_scroll->getContentHeight(), rowHeight * m_visibleIndices.size());
    m_scroll->m_contentLayer->setContentSize({ m_scroll->getContentWidth(), contentHeight });

    for (std::size_t index = 0; index < m_visibleIndices.size(); ++index) {
        auto const& run = m_allRuns[m_visibleIndices[index]];
        auto const compatible = ReplaySession::get().isCompatible(m_layer, run);
        auto row = CCLayerColor::create({ 63, 24, 91, 175 }, m_scroll->getContentWidth() - 4.0f, 34.0f);
        row->setPosition({ 2.0f, contentHeight - rowHeight * (index + 1) + 2.0f });
        m_scroll->m_contentLayer->addChild(row);

        auto levelLabel = CCLabelBMFont::create(run.levelName.c_str(), "bigFont.fnt");
        levelLabel->setAnchorPoint({ 0.0f, 0.5f });
        levelLabel->setScale(.32f);
        levelLabel->limitLabelWidth(125.0f, .32f, .15f);
        levelLabel->setPosition({ 8.0f, 23.0f });
        row->addChild(levelLabel);

        auto detailLabel = CCLabelBMFont::create(runLabel(run).c_str(), "chatFont.fnt");
        detailLabel->setAnchorPoint({ 0.0f, 0.5f });
        detailLabel->setScale(.38f);
        detailLabel->setOpacity(205);
        detailLabel->setPosition({ 8.0f, 10.0f });
        row->addChild(detailLabel);

        auto menu = CCMenu::create();
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({ 0.0f, 0.0f });
        menu->setPosition({ 0.0f, 0.0f });
        menu->setContentSize(row->getContentSize());
        row->addChild(menu);

        auto watchSprite = ButtonSprite::create(
            compatible ? "Watch" : "Incompatible",
            82,
            0,
            .55f,
            true
        );
        watchSprite->setScale(.62f);
        watchSprite->setColor(kPurple);
        auto watchButton = CCMenuItemSpriteExtra::create(
            watchSprite,
            this,
            menu_selector(ReplayLibraryPopup::onWatch)
        );
        watchButton->setTag(static_cast<int>(index));
        watchButton->setPosition({ row->getContentWidth() - 45.0f, 17.0f });
        watchButton->setEnabled(compatible);
        if (!compatible) watchSprite->setOpacity(120);
        menu->addChild(watchButton);
    }

    m_scroll->scrollToTop();
    m_countLabel->setString(fmt::format("{} saved attempt{}", m_visibleIndices.size(), m_visibleIndices.size() == 1 ? "" : "s").c_str());
}

void ReplayLibraryPopup::onWatch(CCObject* sender) {
    auto const index = static_cast<std::size_t>(sender->getTag());
    if (index >= m_visibleIndices.size()) return;

    auto const& summary = m_allRuns[m_visibleIndices[index]];
    auto run = ReplayStore::load(summary.sourcePath);
    if (!run) {
        FLAlertLayer::create("Replay Mod", "That replay file is damaged or incomplete.", "OK")->show();
        return;
    }

    std::string error;
    if (!ReplaySession::get().startRun(m_layer, std::move(*run), error)) {
        FLAlertLayer::create("Replay Mod", error, "OK")->show();
        return;
    }

    Notification::create("Watching saved replay", NotificationIcon::Info, 2.0f)->show();
    this->onClose(nullptr);
}

} // namespace replay_mod
