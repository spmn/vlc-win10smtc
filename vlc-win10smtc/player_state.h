#pragma once

#include <chrono>
#include <functional>
#include <string>

enum class PlaybackState { Closed, Opening, Playing, Paused, Stopped };

struct TrackMetadata
{
    // UTF-8. At most one of the artwork fields is set.
    std::string title;
    std::string artist;
    std::string album;
    std::string artwork_url;    // http(s), fetched by Windows
    std::string artwork_path;

    bool operator==(const TrackMetadata&) const = default;
};

// A position sample and when it was taken, so consumers extrapolate instead of polling.
struct Timeline
{
    std::chrono::microseconds duration{ 0 };   // 0 while unknown
    std::chrono::microseconds position{ 0 };
    double rate = 0.0;                         // 0 while the bar must not advance
    std::chrono::steady_clock::time_point anchor{};

    std::chrono::microseconds ExtrapolatePosition(std::chrono::steady_clock::time_point now) const;
};

struct PlayerState
{
    PlaybackState playback = PlaybackState::Closed;
    bool can_seek = false;
    bool can_previous = false;
    bool can_next = false;
    TrackMetadata metadata;
    Timeline timeline;
};

enum class LogLevel { Debug, Warning, Error };
using Logger = std::function<void(LogLevel, std::string)>;

inline std::chrono::microseconds Timeline::ExtrapolatePosition(std::chrono::steady_clock::time_point now) const
{
    const auto elapsed = now > anchor ? now - anchor : std::chrono::steady_clock::duration::zero();

    auto advanced = position + std::chrono::duration_cast<std::chrono::microseconds>(elapsed * rate);
    if (advanced < std::chrono::microseconds::zero())
        advanced = std::chrono::microseconds::zero();
    if (duration > std::chrono::microseconds::zero() && advanced > duration)
        advanced = duration;
    return advanced;
}
