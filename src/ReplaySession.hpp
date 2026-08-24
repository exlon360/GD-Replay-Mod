#pragma once

#include "ReplayTypes.hpp"

#include <Geode/Geode.hpp>

#include <optional>
#include <string>

class GJBaseGameLayer;
class GJGameLevel;
class PlayLayer;

namespace replay_mod {

enum class SessionMode {
    Idle,
    Recording,
    ArmedPlayback,
    Playback,
    Paused,
    Finished,
};

class ReplaySession final {
public:
    static ReplaySession& get();

    void onLayerInit(PlayLayer* layer);
    void onLayerWillReset(PlayLayer* layer);
    void onLayerDidReset(PlayLayer* layer);
    void onLayerWillExit(PlayLayer* layer);
    void onLayerDidExit(PlayLayer* layer);
    void onRecordingDeath(PlayLayer* layer);
    void onRecordingComplete(PlayLayer* layer);
    void onPlaybackDeath(PlayLayer* layer);
    void onPlaybackComplete(PlayLayer* layer);

    bool shouldForwardInput(GJBaseGameLayer* layer, bool down, int button, bool player1);
    bool beforeProcessCommands(GJBaseGameLayer* layer);

    bool startLatest(PlayLayer* layer, std::string& error);
    bool startRun(PlayLayer* layer, ReplayRun run, std::string& error);
    bool isCompatible(PlayLayer* layer, ReplayRun const& run) const;
    bool stopPlayback(PlayLayer* layer);
    bool togglePause();
    float cycleSpeed();

    SessionMode mode() const;
    bool isPlaybackSession() const;
    bool isActivelyPlaying() const;
    float speed() const;
    std::string statusText() const;

private:
    ReplaySession() = default;

    void beginRecording(PlayLayer* layer);
    void finalizeRecording(PlayLayer* layer, bool completed);
    void restorePlaybackEnvironment(PlayLayer* layer);
    void restorePlaybackSafety(PlayLayer* layer);
    void restoreLevelSaveFlag();
    void restoreTimeScale();
    void finishPlayback(PlayLayer* layer);
    void releaseHeldButtons(GJBaseGameLayer* layer);
    static std::int64_t nowMs();
    static std::uint64_t levelFingerprint(PlayLayer* layer);
    std::uint64_t fingerprintFor(PlayLayer* layer) const;

    PlayLayer* m_layer = nullptr;
    SessionMode m_mode = SessionMode::Idle;
    ReplayRun m_currentRun;
    std::optional<ReplayRun> m_pendingRun;
    std::size_t m_playbackIndex = 0;
    std::int64_t m_startStep = 0;
    double m_startTimestamp = 0.0;
    std::int64_t m_lastRecordedStep = -1;
    std::uint32_t m_sameStepOrder = 0;
    bool m_injecting = false;
    bool m_heldButtons[2][4] {};
    bool m_recordingLimitNotified = false;
    int m_resetDepth = 0;
    bool m_exitPrepared = false;
    std::uint64_t m_levelFingerprint = 0;
    bool m_originalPractice = false;
    bool m_hasPracticeOverride = false;
    bool m_originalDontSave = false;
    bool m_hasDontSaveOverride = false;
    GJGameLevel* m_overriddenLevel = nullptr;
    float m_previousTimeScale = 1.0f;
    bool m_hasTimeScaleOverride = false;
    float m_speed = 1.0f;
};

} // namespace replay_mod
