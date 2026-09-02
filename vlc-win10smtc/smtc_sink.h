#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#include "host_window.h"
#include "playback_commands.h"
#include "player_state.h"

// Mirrors PlayerState onto the System Media Transport Controls, pushing only what changed, and
// forwards button presses to PlaybackCommands. The worker is a single-threaded apartment that owns
// the controls' window and pumps its messages, so all WinRT work and the delegates run on it.
class SmtcSink
{
public:
    SmtcSink(PlaybackCommands& commands, Logger log);
    ~SmtcSink();

    SmtcSink(const SmtcSink&) = delete;
    SmtcSink& operator=(const SmtcSink&) = delete;

    // Any thread. Only the latest post is applied, and not before `not_before`.
    void Post(const PlayerState& state, std::chrono::steady_clock::time_point not_before);

private:
    void RunWorker(std::stop_token stop);
    void ConnectControls();
    void DisconnectControls() noexcept;
    void HideControls();
    std::optional<PlayerState> TakePendingIfDue(DWORD& wait_ms);
    void PushChanges(const PlayerState& next);
    void PushMetadata(const TrackMetadata& metadata);
    void PushTimeline(const Timeline& timeline);
    winrt::Windows::Storage::Streams::RandomAccessStreamReference MakeThumbnail(const TrackMetadata& meta);

    PlaybackCommands& commands_;
    Logger log_;

    std::unique_ptr<HostWindow> window_;
    winrt::Windows::Media::SystemMediaTransportControls smtc_{ nullptr };
    winrt::Windows::Media::SystemMediaTransportControlsDisplayUpdater display_{ nullptr };
    winrt::Windows::Storage::Streams::RandomAccessStreamReference default_art_{ nullptr };
    winrt::event_token button_token_{};
    winrt::event_token position_token_{};

    // What the system shows. Closed means disabled with a cleared display.
    std::optional<PlayerState> pushed_;

    std::mutex mutex_;
    PlayerState pending_;
    std::chrono::steady_clock::time_point deadline_{};
    bool dirty_ = false;
    winrt::handle wake_;   // set by Post and by the stop request

    // Last member: the thread stops before the rest is destroyed.
    std::jthread worker_;
};
