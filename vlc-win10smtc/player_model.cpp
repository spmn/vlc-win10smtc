#include "player_model.h"

#include <utility>

#include "smtc_sink.h"

namespace
{

// Long enough to cover VLC 3's gap between consecutive items.
constexpr auto kStopGrace = std::chrono::milliseconds{ 300 };

// Stops this close to the end count as finished when VLC does not say.
constexpr auto kEndTolerance = std::chrono::seconds{ 1 };

} // namespace

PlayerModel::PlayerModel(PlaybackCommands& commands, Logger log)
    : sink_{ std::make_unique<SmtcSink>(commands, std::move(log)) }
{
}

PlayerModel::~PlayerModel() = default;

template <typename Fn>
void PlayerModel::UpdateAndPost(Fn&& fn)
{
    PlayerState snapshot;
    std::chrono::steady_clock::time_point not_before;
    {
        std::lock_guard lock{ mutex_ };
        const auto now = std::chrono::steady_clock::now();
        fn(now);
        snapshot = state_;
        snapshot.timeline = CurrentTimeline();
        not_before = state_.playback == PlaybackState::Stopped ? stop_deadline_ : now;
    }
    sink_->Post(snapshot, not_before);
}

template <typename Fn>
void PlayerModel::UpdateTimelineAndPost(Fn&& fn)
{
    UpdateAndPost([&](auto now)
    {
        if (state_.playback != PlaybackState::Stopped)
            fn(now);
    });
}

Timeline PlayerModel::CurrentTimeline() const
{
    Timeline timeline = state_.timeline;
    if (state_.playback != PlaybackState::Playing)
        timeline.rate = 0.0;
    return timeline;
}

void PlayerModel::ResetTimeline(std::chrono::steady_clock::time_point now)
{
    state_.timeline = { .rate = 1.0, .anchor = now };
}

// Pin the extrapolated position before a state or rate change.
void PlayerModel::FreezePosition(std::chrono::steady_clock::time_point now)
{
    state_.timeline.position = CurrentTimeline().ExtrapolatePosition(now);
    state_.timeline.anchor = now;
}

void PlayerModel::CloseMedia()
{
    UpdateAndPost([&](auto now)
    {
        state_.playback = PlaybackState::Closed;
        state_.metadata = {};
        ResetTimeline(now);
    });
}

void PlayerModel::OpenMedia(TrackMetadata metadata)
{
    UpdateAndPost([&](auto now)
    {
        state_.playback = PlaybackState::Opening;
        state_.metadata = std::move(metadata);
        ResetTimeline(now);
    });
}

void PlayerModel::SetPlaybackState(PlaybackState playback)
{
    UpdateAndPost([&](auto now)
    {
        FreezePosition(now);
        if (playback == PlaybackState::Opening)
            state_.timeline.position = {};
        state_.playback = playback;
    });
}

void PlayerModel::SetStopped(StopReason reason)
{
    UpdateAndPost([&](auto now)
    {
        if (state_.playback == PlaybackState::Closed)
            return;

        Timeline& timeline = state_.timeline;
        const bool finished = reason == StopReason::Finished
            || (reason == StopReason::Unknown && timeline.duration > std::chrono::microseconds::zero()
                && CurrentTimeline().ExtrapolatePosition(now) >= timeline.duration - kEndTolerance);

        state_.playback = PlaybackState::Stopped;
        timeline.position = finished ? timeline.duration : std::chrono::microseconds::zero();
        timeline.anchor = now;
        stop_deadline_ = now + kStopGrace;
    });
}

void PlayerModel::SetMetadata(TrackMetadata metadata)
{
    UpdateAndPost([&](auto) { state_.metadata = std::move(metadata); });
}

void PlayerModel::SetLength(std::chrono::microseconds length)
{
    UpdateTimelineAndPost([&](auto) { state_.timeline.duration = length; });
}

void PlayerModel::SetPosition(std::chrono::microseconds position)
{
    UpdateTimelineAndPost([&](auto now)
    {
        state_.timeline.position = position;
        state_.timeline.anchor = now;
    });
}

void PlayerModel::SetRate(double rate)
{
    UpdateTimelineAndPost([&](auto now)
    {
        FreezePosition(now);
        state_.timeline.rate = rate;
    });
}

void PlayerModel::SetCanSeek(bool can_seek)
{
    UpdateAndPost([&](auto) { state_.can_seek = can_seek; });
}

void PlayerModel::SetNavigation(bool can_previous, bool can_next)
{
    UpdateAndPost([&](auto)
    {
        state_.can_previous = can_previous;
        state_.can_next = can_next;
    });
}
