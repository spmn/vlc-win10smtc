#include <chrono>
#include <stdexcept>

#include "player_model.h"
#include "plugin.h"
#include "vlc_msvc_shims.h"

#define MODULE_STRING PLUGIN_SHORTNAME
#define VLC_DYNAMIC_PLUGIN

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_player.h>
#include <vlc_playlist.h>
#include <vlc_url.h>
#include <libvlc.h>

#include "vlc_glue.h"

namespace
{

// VLC_TICK_INVALID is 0, same as the model's "unknown".
std::chrono::microseconds ToMicroseconds(vlc_tick_t tick)
{
    return std::chrono::microseconds{ US_FROM_VLC_TICK(tick) };
}

class VlcInterface final : public PlaybackCommands
{
public:
    explicit VlcInterface(intf_thread_t* intf)
        : intf_{ intf }
        , playlist_{ vlc_intf_GetMainPlaylist(intf) }
        , player_{ vlc_playlist_GetPlayer(playlist_) }
        , model_{ *this, MakeLogger(intf) }
    {
        WithPlaylistLock([this]
        {
            player_listener_ = vlc_player_AddListener(player_, &kPlayerCallbacks, this);
            playlist_listener_ = vlc_playlist_AddListener(playlist_, &kPlaylistCallbacks, this, true);
            if (player_listener_ && playlist_listener_)
                ReadInitialState();
            else
                RemoveListeners();
        });
        if (!player_listener_ || !playlist_listener_)
            throw std::runtime_error{ "adding the player listeners failed" };
    }

    ~VlcInterface()
    {
        WithPlaylistLock([this] { RemoveListeners(); });
    }

    VlcInterface(const VlcInterface&) = delete;
    VlcInterface& operator=(const VlcInterface&) = delete;

private:
    static VlcInterface& InterfaceFrom(void* data) { return *static_cast<VlcInterface*>(data); }

    void RemoveListeners()
    {
        if (playlist_listener_)
            vlc_playlist_RemoveListener(playlist_, playlist_listener_);
        if (player_listener_)
            vlc_player_RemoveListener(player_, player_listener_);
        playlist_listener_ = nullptr;
        player_listener_ = nullptr;
    }

    // Something may already be playing.
    void ReadInitialState()
    {
        input_item_t* media = vlc_player_GetCurrentMedia(player_);
        if (!media)
            return;

        model_.OpenMedia(ReadMetadata(media));
        model_.SetCanSeek((vlc_player_GetCapabilities(player_) & VLC_PLAYER_CAP_SEEK) != 0);
        model_.SetLength(ToMicroseconds(vlc_player_GetLength(player_)));
        model_.SetPosition(ToMicroseconds(vlc_player_GetTime(player_)));
        model_.SetRate(vlc_player_GetRate(player_));
        ForwardState(vlc_player_GetState(player_));
    }

    void ForwardState(vlc_player_state state)
    {
        switch (state)
        {
        case VLC_PLAYER_STATE_STARTED:  model_.SetPlaybackState(PlaybackState::Opening); break;
        case VLC_PLAYER_STATE_PLAYING:  model_.SetPlaybackState(PlaybackState::Playing); break;
        case VLC_PLAYER_STATE_PAUSED:   model_.SetPlaybackState(PlaybackState::Paused); break;
        case VLC_PLAYER_STATE_STOPPED:  model_.SetStopped(stop_reason_); break;
        case VLC_PLAYER_STATE_STOPPING: break;   // STOPPED follows
        }
    }

    // Player lock held: must not wait on anything a PlaybackCommands call can block in.
    static void OnMediaChanged(vlc_player_t* player, input_item_t* media, void* data)
    {
        auto& self = InterfaceFrom(data);
        if (!media)
        {
            self.model_.CloseMedia();
            return;
        }

        // Between items the player's global state stays PLAYING, so no state event follows.
        self.model_.OpenMedia(ReadMetadata(media));
        self.ForwardState(vlc_player_GetState(player));
    }

    static void OnStateChanged(vlc_player_t*, vlc_player_state state, void* data)
    {
        InterfaceFrom(data).ForwardState(state);
    }

    static void OnStopping(vlc_player_t*, input_item_t*, vlc_player_media_stopping_reason reason, void* data)
    {
        InterfaceFrom(data).stop_reason_ = reason == VLC_PLAYER_MEDIA_STOPPING_EOS ? StopReason::Finished
                                                                          : StopReason::Interrupted;
    }

    static void OnRateChanged(vlc_player_t*, float rate, void* data)
    {
        InterfaceFrom(data).model_.SetRate(rate);
    }

    static void OnCapabilitiesChanged(vlc_player_t*, int, int capabilities, void* data)
    {
        InterfaceFrom(data).model_.SetCanSeek((capabilities & VLC_PLAYER_CAP_SEEK) != 0);
    }

    static void OnPositionChanged(vlc_player_t*, vlc_tick_t time, double, void* data)
    {
        InterfaceFrom(data).model_.SetPosition(ToMicroseconds(time));
    }

    static void OnLengthChanged(vlc_player_t*, vlc_tick_t length, void* data)
    {
        InterfaceFrom(data).model_.SetLength(ToMicroseconds(length));
    }

    // The playlist relays the player's own metadata events here and adds the preparser's, such
    // as fetched artwork, which the player never sees.
    static void OnItemsUpdated(vlc_playlist_t*, size_t, vlc_playlist_item_t* const items[], size_t count, void* data)
    {
        auto& self = InterfaceFrom(data);
        input_item_t* current = vlc_player_GetCurrentMedia(self.player_);
        if (!current)
            return;

        for (size_t i = 0; i < count; ++i)
            if (vlc_playlist_item_GetMedia(items[i]) == current)
                self.model_.SetMetadata(ReadMetadata(current));
    }

    static void OnHasPrevChanged(vlc_playlist_t*, bool has_prev, void* data)
    {
        auto& self = InterfaceFrom(data);
        self.can_previous_ = has_prev;
        self.model_.SetNavigation(self.can_previous_, self.can_next_);
    }

    static void OnHasNextChanged(vlc_playlist_t*, bool has_next, void* data)
    {
        auto& self = InterfaceFrom(data);
        self.can_next_ = has_next;
        self.model_.SetNavigation(self.can_previous_, self.can_next_);
    }

    static constexpr vlc_player_cbs kPlayerCallbacks{
        .on_current_media_changed = OnMediaChanged,
        .on_state_changed = OnStateChanged,
        .on_rate_changed = OnRateChanged,
        .on_capabilities_changed = OnCapabilitiesChanged,
        .on_position_changed = OnPositionChanged,
        .on_length_changed = OnLengthChanged,
        .on_stopping_current_media = OnStopping,
    };

    static constexpr vlc_playlist_callbacks kPlaylistCallbacks{
        .on_items_updated = OnItemsUpdated,
        .on_has_prev_changed = OnHasPrevChanged,
        .on_has_next_changed = OnHasNextChanged,
    };

    // The playlist and its player share one lock.
    template <typename Fn>
    void WithPlaylistLock(Fn&& fn)
    {
        vlc_playlist_Lock(playlist_);
        fn();
        vlc_playlist_Unlock(playlist_);
    }

    // Start is a no-op while paused.
    void Play() override
    {
        WithPlaylistLock([this]
        {
            if (vlc_player_GetState(player_) == VLC_PLAYER_STATE_PAUSED)
                vlc_playlist_Resume(playlist_);
            else
                vlc_playlist_Start(playlist_);
        });
    }

    void Pause() override    { WithPlaylistLock([this] { vlc_playlist_Pause(playlist_); }); }
    void Stop() override     { WithPlaylistLock([this] { vlc_playlist_Stop(playlist_); }); }
    void Next() override     { WithPlaylistLock([this] { vlc_playlist_Next(playlist_); }); }
    void Previous() override { WithPlaylistLock([this] { vlc_playlist_Prev(playlist_); }); }

    void SkipForward() override  { SeekBy(ReadSkipStep(intf_)); }
    void SkipBackward() override { SeekBy(-ReadSkipStep(intf_)); }

    void SeekTo(std::chrono::microseconds position) override
    {
        WithPlaylistLock([this, position] { vlc_player_SetTime(player_, VLC_TICK_FROM_US(position.count())); });
    }

    void SeekBy(std::chrono::microseconds delta)
    {
        WithPlaylistLock([this, delta] { vlc_player_JumpTime(player_, VLC_TICK_FROM_US(delta.count())); });
    }

    intf_thread_t* const intf_;
    vlc_playlist_t* const playlist_;
    vlc_player_t* const player_;
    vlc_player_listener_id* player_listener_ = nullptr;
    vlc_playlist_listener_id* playlist_listener_ = nullptr;

    // Callbacks only; serialized by the shared lock.
    StopReason stop_reason_ = StopReason::Interrupted;
    bool can_previous_ = false;
    bool can_next_ = false;

    // Last member: the SMTC thread stops before the rest is destroyed.
    PlayerModel model_;
};

} // namespace

vlc_module_begin()
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_shortname(PLUGIN_SHORTNAME)
    set_description(PLUGIN_DESCRIPTION)
    set_capability(PLUGIN_CAPABILITY, 0)
    set_callbacks(OpenInterface<VlcInterface>, CloseInterface<VlcInterface>)
vlc_module_end()
