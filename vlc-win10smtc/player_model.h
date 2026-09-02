#pragma once

#include <chrono>
#include <memory>
#include <mutex>

#include "playback_commands.h"
#include "player_state.h"

class SmtcSink;

enum class StopReason
{
    Interrupted,   // rewinds to 0
    Finished,      // stays at the end
    Unknown,       // decided from the position
};

// Thread-safe and never calls back out, so VLC may call in with its locks held.
class PlayerModel
{
public:
    PlayerModel(PlaybackCommands& commands, Logger log);
    ~PlayerModel();

    PlayerModel(const PlayerModel&) = delete;
    PlayerModel& operator=(const PlayerModel&) = delete;

    // Hides the controls.
    void CloseMedia();

    // Resets the timeline; status Opening.
    void OpenMedia(TrackMetadata metadata);

    // Opening, Playing or Paused; Opening resets the position.
    void SetPlaybackState(PlaybackState playback);

    // Deferred briefly so a following OpenMedia cancels it.
    void SetStopped(StopReason reason);

    void SetMetadata(TrackMetadata metadata);
    void SetLength(std::chrono::microseconds length);
    void SetPosition(std::chrono::microseconds position);
    void SetRate(double rate);
    void SetCanSeek(bool can_seek);
    void SetNavigation(bool can_previous, bool can_next);

private:
    template <typename Fn>
    void UpdateAndPost(Fn&& fn);

    // The timeline is pinned while stopped.
    template <typename Fn>
    void UpdateTimelineAndPost(Fn&& fn);

    // Rate 0 unless playing, which stops the consumer's own extrapolation.
    Timeline CurrentTimeline() const;
    void ResetTimeline(std::chrono::steady_clock::time_point now);
    void FreezePosition(std::chrono::steady_clock::time_point now);

    std::mutex mutex_;
    PlayerState state_;   // timeline.rate is VLC's; see CurrentTimeline
    std::chrono::steady_clock::time_point stop_deadline_{};

    // Pointer keeps the WinRT headers out of includers. Last member: its thread stops first.
    std::unique_ptr<SmtcSink> sink_;
};
