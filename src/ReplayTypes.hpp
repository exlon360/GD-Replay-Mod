#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace replay_mod {

inline constexpr std::size_t kMaximumReplayEvents = 500'000;
inline constexpr std::int64_t kMaximumReplayDurationSteps = 10'000'000;
inline constexpr std::int64_t kReplayEndToleranceSteps = 8;

struct InputEvent {
    std::int64_t step = 0;
    std::uint32_t order = 0;
    double timestamp = 0.0;
    int button = 1;
    bool player1 = true;
    bool down = false;
};

struct ReplayRun {
    int formatVersion = 2;
    int levelID = 0;
    std::uint64_t levelHash = 0;
    std::string levelName;
    int attempt = 0;
    std::int64_t createdAtMs = 0;
    std::int64_t durationSteps = 0;
    double progress = 0.0;
    bool completed = false;
    bool platformer = false;
    bool practice = false;
    std::uint64_t randomSeed = 0;
    std::uint64_t replayRandomSeed = 0;
    std::size_t storedEventCount = 0;
    std::vector<InputEvent> events;
    std::filesystem::path sourcePath;
};

struct SaveResult {
    bool ok = false;
    std::filesystem::path path;
    std::string error;
};

} // namespace replay_mod
