#include "smtc_sink.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <objidl.h>
#include <shcore.h>
#include <shlwapi.h>
#include <systemmediatransportcontrolsinterop.h>

#include "host_icon.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Storage::Streams;

namespace
{

constexpr auto kPositionTolerance = std::chrono::milliseconds{ 100 };

// Folds the burst of separate events at track start into one push.
constexpr auto kCoalesceWindow = std::chrono::milliseconds{ 30 };

MediaPlaybackStatus ToSmtcStatus(PlaybackState state)
{
    switch (state)
    {
    case PlaybackState::Opening: return MediaPlaybackStatus::Changing;
    case PlaybackState::Playing: return MediaPlaybackStatus::Playing;
    case PlaybackState::Paused:  return MediaPlaybackStatus::Paused;
    case PlaybackState::Stopped: return MediaPlaybackStatus::Stopped;
    case PlaybackState::Closed:  break;
    }
    return MediaPlaybackStatus::Closed;
}

const char* ToLogName(PlaybackState state)
{
    switch (state)
    {
    case PlaybackState::Opening: return "opening";
    case PlaybackState::Playing: return "playing";
    case PlaybackState::Paused:  return "paused";
    case PlaybackState::Stopped: return "stopped";
    case PlaybackState::Closed:  break;
    }
    return "closed";
}

// For use inside a catch block.
std::string CurrentExceptionMessage()
{
    try
    {
        throw;
    }
    catch (const winrt::hresult_error& ex)
    {
        return std::format("{} (0x{:08X})", winrt::to_string(ex.message()), static_cast<unsigned>(ex.code().value));
    }
    catch (const std::exception& ex)
    {
        return ex.what();
    }
    catch (...)
    {
        return "unknown exception";
    }
}

bool NeedsTimelinePush(const Timeline& pushed, const Timeline& next)
{
    if (pushed.duration != next.duration || pushed.rate != next.rate)
        return true;

    const auto now = std::chrono::steady_clock::now();
    return std::chrono::abs(pushed.ExtrapolatePosition(now) - next.ExtrapolatePosition(now)) > kPositionTolerance;
}

// Synchronous, unlike the WinRT writers, which may not be awaited on this thread.
RandomAccessStreamReference MakeArtworkStream(std::span<const std::byte> bytes)
{
    winrt::com_ptr<IStream> memory;
    memory.attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
    if (!memory)
        throw std::bad_alloc{};

    IRandomAccessStream stream{ nullptr };
    winrt::check_hresult(CreateRandomAccessStreamOverStream(memory.get(), BSOS_DEFAULT,
                                                             winrt::guid_of<IRandomAccessStream>(), winrt::put_abi(stream)));
    return RandomAccessStreamReference::CreateFromStream(stream);
}

std::vector<std::byte> ReadFileBytes(const std::string& utf8_path)
{
    const std::filesystem::path path{ std::wstring_view{ winrt::to_hstring(utf8_path) } };
    std::ifstream file{ path, std::ios::binary };
    if (!file)
        throw std::runtime_error{ "cannot open " + utf8_path };

    std::vector<std::byte> bytes(static_cast<size_t>(std::filesystem::file_size(path)));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error{ "cannot read " + utf8_path };
    return bytes;
}

void PumpMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// A null handle would make the worker's wait fail instantly and spin.
winrt::handle CreateWakeEvent()
{
    winrt::handle event{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
    if (!event)
        throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "CreateEvent" };
    return event;
}

} // namespace

SmtcSink::SmtcSink(PlaybackCommands& commands, Logger log)
    : commands_{ commands }
    , log_{ std::move(log) }
    , wake_{ CreateWakeEvent() }
    , worker_{ [this](std::stop_token stop) { RunWorker(std::move(stop)); } }
{
}

SmtcSink::~SmtcSink() = default;

void SmtcSink::Post(const PlayerState& state, std::chrono::steady_clock::time_point not_before)
{
    {
        std::lock_guard lock{ mutex_ };
        pending_ = state;
        deadline_ = std::max(not_before, std::chrono::steady_clock::now() + kCoalesceWindow);
        dirty_ = true;
    }
    SetEvent(wake_.get());
}

void SmtcSink::RunWorker(std::stop_token stop)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
    catch (...)
    {
        log_(LogLevel::Error, "SMTC: apartment init failed: " + CurrentExceptionMessage());
        return;
    }

    std::stop_callback on_stop{ stop, [this] { SetEvent(wake_.get()); } };
    try
    {
        ConnectControls();

        while (!stop.stop_requested())
        {
            DWORD wait_ms = INFINITE;
            if (std::optional<PlayerState> due = TakePendingIfDue(wait_ms))
            {
                // A failed push leaves `pushed_` alone, so the next one retries the difference.
                try
                {
                    PushChanges(*due);
                }
                catch (...)
                {
                    log_(LogLevel::Warning, "SMTC: push failed: " + CurrentExceptionMessage());
                }
                continue;
            }

            HANDLE handles[] = { wake_.get() };
            if (MsgWaitForMultipleObjectsEx(1, handles, wait_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0 + 1)
                PumpMessages();
        }
    }
    catch (...)
    {
        log_(LogLevel::Error, "SMTC: worker stopped: " + CurrentExceptionMessage());
    }

    DisconnectControls();
    winrt::uninit_apartment();
}

// The pending state once its deadline has passed; otherwise how long to wait for it.
std::optional<PlayerState> SmtcSink::TakePendingIfDue(DWORD& wait_ms)
{
    std::lock_guard lock{ mutex_ };
    if (!dirty_)
        return std::nullopt;

    const auto remaining = deadline_ - std::chrono::steady_clock::now();
    if (remaining > std::chrono::steady_clock::duration::zero())
    {
        wait_ms = static_cast<DWORD>(std::chrono::ceil<std::chrono::milliseconds>(remaining).count());
        return std::nullopt;
    }

    dirty_ = false;
    return pending_;
}

void SmtcSink::DisconnectControls() noexcept
{
    try
    {
        if (smtc_)
        {
            if (button_token_)
                smtc_.ButtonPressed(button_token_);
            if (position_token_)
                smtc_.PlaybackPositionChangeRequested(position_token_);

            HideControls();
        }
    }
    catch (...)
    {
    }

    default_art_ = nullptr;
    display_ = nullptr;
    smtc_ = nullptr;
    window_.reset();
}

void SmtcSink::HideControls()
{
    display_.ClearAll();
    smtc_.PlaybackStatus(MediaPlaybackStatus::Closed);
    smtc_.IsEnabled(false);
}

void SmtcSink::ConnectControls()
{
    window_ = std::make_unique<HostWindow>();

    const auto interop = winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
    winrt::check_hresult(interop->GetForWindow(window_->GetHandle(), winrt::guid_of<SystemMediaTransportControls>(),
                                               winrt::put_abi(smtc_)));
    display_ = smtc_.DisplayUpdater();

    button_token_ = smtc_.ButtonPressed([this](auto&&, const SystemMediaTransportControlsButtonPressedEventArgs& args)
    {
        switch (args.Button())
        {
        case SystemMediaTransportControlsButton::Play:        commands_.Play(); break;
        case SystemMediaTransportControlsButton::Pause:       commands_.Pause(); break;
        case SystemMediaTransportControlsButton::Stop:        commands_.Stop(); break;
        case SystemMediaTransportControlsButton::Next:        commands_.Next(); break;
        case SystemMediaTransportControlsButton::Previous:    commands_.Previous(); break;
        case SystemMediaTransportControlsButton::FastForward: commands_.SkipForward(); break;
        case SystemMediaTransportControlsButton::Rewind:      commands_.SkipBackward(); break;
        default: break;
        }
    });

    position_token_ = smtc_.PlaybackPositionChangeRequested([this](auto&&, const PlaybackPositionChangeRequestedEventArgs& args)
    {
        commands_.SeekTo(std::chrono::duration_cast<std::chrono::microseconds>(args.RequestedPlaybackPosition()));
    });

    smtc_.IsPlayEnabled(true);
    smtc_.IsPauseEnabled(true);
    smtc_.IsStopEnabled(true);
    HideControls();

    if (const std::vector<std::byte> icon = ReadHostIcon(); !icon.empty())
    {
        default_art_ = MakeArtworkStream(icon);
        log_(LogLevel::Debug, std::format("SMTC: default artwork {} bytes", icon.size()));
    }
}

void SmtcSink::PushChanges(const PlayerState& next)
{
    const bool was_closed = !pushed_ || pushed_->playback == PlaybackState::Closed;

    if (next.playback == PlaybackState::Closed)
    {
        if (!was_closed)
        {
            HideControls();
            log_(LogLevel::Debug, "SMTC: status closed");
        }
        pushed_ = next;
        return;
    }

    if (was_closed || pushed_->can_seek != next.can_seek)
    {
        smtc_.IsFastForwardEnabled(next.can_seek);
        smtc_.IsRewindEnabled(next.can_seek);
    }
    if (was_closed || pushed_->can_previous != next.can_previous || pushed_->can_next != next.can_next)
    {
        smtc_.IsPreviousEnabled(next.can_previous);
        smtc_.IsNextEnabled(next.can_next);
        log_(LogLevel::Debug, std::format("SMTC: previous {} next {}", next.can_previous, next.can_next));
    }

    if (was_closed || pushed_->metadata != next.metadata)
        PushMetadata(next.metadata);

    if (was_closed || NeedsTimelinePush(pushed_->timeline, next.timeline))
        PushTimeline(next.timeline);

    if (was_closed || pushed_->playback != next.playback)
    {
        smtc_.PlaybackStatus(ToSmtcStatus(next.playback));
        log_(LogLevel::Debug, std::format("SMTC: status {}", ToLogName(next.playback)));
    }

    if (was_closed)
        smtc_.IsEnabled(true);

    pushed_ = next;
}

void SmtcSink::PushMetadata(const TrackMetadata& metadata)
{
    // ClearAll() resets Type; without Music the artist line is hidden.
    display_.Type(MediaPlaybackType::Music);

    auto music = display_.MusicProperties();
    music.Title(winrt::to_hstring(metadata.title));
    music.Artist(winrt::to_hstring(metadata.artist));
    music.AlbumTitle(winrt::to_hstring(metadata.album));
    display_.Thumbnail(MakeThumbnail(metadata));
    display_.Update();

    log_(LogLevel::Debug, std::format("SMTC: metadata {} - {}", metadata.artist, metadata.title));
}

void SmtcSink::PushTimeline(const Timeline& timeline)
{
    const auto position = timeline.ExtrapolatePosition(std::chrono::steady_clock::now());
    const bool has_range = timeline.duration > std::chrono::microseconds::zero();

    SystemMediaTransportControlsTimelineProperties props;
    if (has_range)
    {
        const TimeSpan length = timeline.duration;
        props.StartTime(TimeSpan::zero());
        props.MinSeekTime(TimeSpan::zero());
        props.Position(TimeSpan{ position });
        props.EndTime(length);
        props.MaxSeekTime(length);
    }

    // No range hides the bar; the rate goes back to neutral.
    smtc_.UpdateTimelineProperties(props);
    smtc_.PlaybackRate(has_range ? timeline.rate : 1.0);

    log_(LogLevel::Debug, std::format("SMTC: timeline {} us of {} us at {}x",
                                      position.count(), timeline.duration.count(), timeline.rate));
}

RandomAccessStreamReference SmtcSink::MakeThumbnail(const TrackMetadata& meta)
{
    try
    {
        if (!meta.artwork_url.empty())
            return RandomAccessStreamReference::CreateFromUri(Uri{ winrt::to_hstring(meta.artwork_url) });

        if (!meta.artwork_path.empty())
            return MakeArtworkStream(ReadFileBytes(meta.artwork_path));
    }
    catch (...)
    {
        log_(LogLevel::Warning, "SMTC: artwork failed: " + CurrentExceptionMessage());
    }
    return default_art_;
}
