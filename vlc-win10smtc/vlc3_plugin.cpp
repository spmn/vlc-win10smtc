#include <chrono>
#include <initializer_list>
#include <mutex>
#include <utility>

#include "player_model.h"
#include "plugin.h"
#include "vlc_msvc_shims.h"

#define MODULE_STRING PLUGIN_SHORTNAME
#define __PLUGIN__

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_input.h>
#include <vlc_url.h>
#include <libvlc.h>

#include "vlc_glue.h"

namespace
{

class VlcInterface final : public PlaybackCommands
{
public:
    explicit VlcInterface(intf_thread_t* intf)
        : intf_{ intf }
        , playlist_{ pl_Get(intf) }
        , model_{ *this, MakeLogger(intf) }
    {
        var_AddCallback(playlist_, "input-current", OnCurrentInputChanged, this);
        var_AddCallback(playlist_, "item-change", OnItemChanged, this);
        var_AddCallback(playlist_, "playlist-item-deleted", OnItemDeleted, this);
        for (const char* name : kPlaylistChanges)
            var_AddCallback(playlist_, name, OnPlayOrderChanged, this);

        // Something may already be playing.
        WithCurrentInput([this](input_thread_t* input) { ReplaceInput(input); });
    }

    ~VlcInterface()
    {
        for (const char* name : kPlaylistChanges)
            var_DelCallback(playlist_, name, OnPlayOrderChanged, this);
        var_DelCallback(playlist_, "playlist-item-deleted", OnItemDeleted, this);
        var_DelCallback(playlist_, "item-change", OnItemChanged, this);
        var_DelCallback(playlist_, "input-current", OnCurrentInputChanged, this);
        ReplaceInput(nullptr);
    }

    VlcInterface(const VlcInterface&) = delete;
    VlcInterface& operator=(const VlcInterface&) = delete;

private:
    // The input being followed and its item, both held.
    struct Current
    {
        input_thread_t* input = nullptr;
        input_item_t* item = nullptr;
    };

    static constexpr const char* kPlaylistChanges[] = { "playlist-item-append", "loop", "random" };

    static VlcInterface& InterfaceFrom(void* data) { return *static_cast<VlcInterface*>(data); }

    static int OnCurrentInputChanged(vlc_object_t*, const char*, vlc_value_t, vlc_value_t newval, void* data)
    {
        InterfaceFrom(data).ReplaceInput(static_cast<input_thread_t*>(newval.p_address));
        return VLC_SUCCESS;
    }

    // The play order is rebuilt lazily, so both stay on until the next item.
    static int OnPlayOrderChanged(vlc_object_t*, const char*, vlc_value_t, vlc_value_t, void* data)
    {
        InterfaceFrom(data).model_.SetNavigation(true, true);
        return VLC_SUCCESS;
    }

    // Under the playlist lock, and before the playlist forgets its current item.
    static int OnItemDeleted(vlc_object_t*, const char*, vlc_value_t, vlc_value_t newval, void* data)
    {
        auto& self = InterfaceFrom(data);
        self.model_.SetNavigation(true, true);
        if (newval.p_address == playlist_CurrentPlayingItem(self.playlist_))
            self.model_.CloseMedia();
        return VLC_SUCCESS;
    }

    static int OnInputEvent(vlc_object_t* object, const char*, vlc_value_t, vlc_value_t newval, void* data)
    {
        InterfaceFrom(data).HandleInputEvent(reinterpret_cast<input_thread_t*>(object), static_cast<int>(newval.i_int));
        return VLC_SUCCESS;
    }

    static int OnItemChanged(vlc_object_t*, const char*, vlc_value_t, vlc_value_t newval, void* data)
    {
        InterfaceFrom(data).RefreshMetadata(static_cast<input_item_t*>(newval.p_address));
        return VLC_SUCCESS;
    }

    // Playlist thread, lock released: the only safe place to drop the input callback, which
    // deadlocks in WaitUnused from inside itself. A null input keeps the last state; Play replays it.
    void ReplaceInput(input_thread_t* next)
    {
        std::lock_guard serialize{ replace_mutex_ };   // the constructor races the playlist thread

        Current previous;
        {
            std::lock_guard lock{ current_mutex_ };
            previous = std::exchange(current_, Current{});
        }
        if (previous.input)
        {
            var_DelCallback(previous.input, "intf-event", OnInputEvent, this);
            input_item_Release(previous.item);
            vlc_object_release(previous.input);
        }

        if (!next)
            return;

        input_item_t* item = input_item_Hold(input_GetItem(next));
        {
            std::lock_guard lock{ current_mutex_ };
            current_ = { static_cast<input_thread_t*>(vlc_object_hold(next)), item };
        }
        model_.OpenMedia(ReadMetadata(item));
        ReadNavigationFromPlaylist();
        var_AddCallback(next, "intf-event", OnInputEvent, this);

        // The input has been running since before the callback existed.
        for (int event : { INPUT_EVENT_LENGTH, INPUT_EVENT_RATE, INPUT_EVENT_POSITION, INPUT_EVENT_STATE })
            HandleInputEvent(next, event);
    }

    // Any thread. Metadata written after the demuxer's own pass, such as fetched artwork, only
    // arrives this way; the input never relays it.
    void RefreshMetadata(input_item_t* item)
    {
        std::lock_guard lock{ current_mutex_ };
        if (item == current_.item)
            model_.SetMetadata(ReadMetadata(item));
    }

    bool IsCurrent(input_thread_t* input)
    {
        std::lock_guard lock{ current_mutex_ };
        return input == current_.input;
    }

    // The play order is up to date right after an item starts.
    void ReadNavigationFromPlaylist()
    {
        playlist_Lock(playlist_);
        const int index = playlist_->i_current_index;
        const int count = playlist_->current.i_size;
        playlist_Unlock(playlist_);

        const bool loop = var_GetBool(playlist_, "loop");
        model_.SetNavigation(loop || index > 0, loop || index + 1 < count);
    }

    // Input thread, or the replay in ReplaceInput.
    void HandleInputEvent(input_thread_t* input, int event)
    {
        if (!IsCurrent(input))
            return;   // still in flight from the previous input

        switch (event)
        {
        case INPUT_EVENT_STATE:
            model_.SetCanSeek(var_GetBool(input, "can-seek"));
            switch (static_cast<input_state_e>(var_GetInteger(input, "state")))
            {
            case OPENING_S: model_.SetPlaybackState(PlaybackState::Opening); break;
            case PLAYING_S: model_.SetPlaybackState(PlaybackState::Playing); break;
            case PAUSE_S:   model_.SetPlaybackState(PlaybackState::Paused); break;
            case END_S:     model_.SetStopped(StopReason::Unknown); break;   // no end-of-stream signal in VLC 3
            case ERROR_S:   model_.SetStopped(StopReason::Interrupted); break;
            case INIT_S:    break;
            }
            break;
        case INPUT_EVENT_LENGTH:
            model_.SetLength(std::chrono::microseconds{ var_GetInteger(input, "length") });
            break;
        case INPUT_EVENT_POSITION:
            model_.SetPosition(std::chrono::microseconds{ var_GetInteger(input, "time") });
            break;
        case INPUT_EVENT_RATE:
            model_.SetRate(var_GetFloat(input, "rate"));
            break;
        default:
            break;
        }
    }

    template <typename Fn>
    void WithCurrentInput(Fn&& fn)
    {
        if (input_thread_t* input = playlist_CurrentInput(playlist_))
        {
            fn(input);
            vlc_object_release(input);
        }
    }

    void Play() override { playlist_Play(playlist_); }
    void Pause() override { playlist_Pause(playlist_); }
    void Stop() override { playlist_Stop(playlist_); }
    void Next() override { playlist_Next(playlist_); }
    void Previous() override { playlist_Prev(playlist_); }

    void SkipForward() override { SeekBy(ReadSkipStep(intf_)); }
    void SkipBackward() override { SeekBy(-ReadSkipStep(intf_)); }

    void SeekTo(std::chrono::microseconds position) override
    {
        WithCurrentInput([&](input_thread_t* input) { var_SetInteger(input, "time", position.count()); });
    }

    void SeekBy(std::chrono::microseconds delta)
    {
        WithCurrentInput([&](input_thread_t* input) { var_SetInteger(input, "time-offset", delta.count()); });
    }

    intf_thread_t* const intf_;
    playlist_t* const playlist_;

    std::mutex replace_mutex_;
    std::mutex current_mutex_;
    Current current_;   // guarded by current_mutex_

    // Last member: the SMTC thread stops before the rest is destroyed.
    PlayerModel model_;
};

} // namespace

vlc_module_begin()
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_shortname(PLUGIN_SHORTNAME)
    set_description(PLUGIN_DESCRIPTION)
    set_capability(PLUGIN_CAPABILITY, 0)
    set_callbacks(OpenInterface<VlcInterface>, CloseInterface<VlcInterface>)
vlc_module_end()
