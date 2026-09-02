#pragma once

// Included after the VLC headers. Same code for both SDKs but different VLC types behind the
// pointers, hence internal linkage.

#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "player_model.h"

namespace
{

// VLC's strings come from its own C runtime.
struct VlcFree
{
    void operator()(char* p) const noexcept { libvlc_free(p); }
};

using VlcString = std::unique_ptr<char, VlcFree>;

std::string TakeString(VlcString value)
{
    return value ? std::string{ value.get() } : std::string{};
}

bool IsHttpUrl(const char* url)
{
    return _strnicmp(url, "http://", 7) == 0 || _strnicmp(url, "https://", 8) == 0;
}

TrackMetadata ReadMetadata(input_item_t* item)
{
    TrackMetadata meta;
    std::string name = TakeString(VlcString{ input_item_GetTitleFbName(item) });
    std::string now_playing = TakeString(VlcString{ input_item_GetNowPlayingFb(item) });
    meta.artist = TakeString(VlcString{ input_item_GetArtist(item) });
    meta.album = TakeString(VlcString{ input_item_GetAlbum(item) });

    // Streams: the song is in "now playing" and the station is the name.
    if (now_playing.empty())
        meta.title = std::move(name);
    else
    {
        meta.title = std::move(now_playing);
        if (meta.artist.empty())
            meta.artist = std::move(name);
    }

    if (VlcString url{ input_item_GetArtworkURL(item) })
    {
        if (VlcString path{ vlc_uri2path(url.get()) })
            meta.artwork_path = path.get();
        else if (IsHttpUrl(url.get()))
            meta.artwork_url = url.get();
    }
    return meta;
}

Logger MakeLogger(intf_thread_t* intf)
{
    return [intf](LogLevel level, std::string message)
    {
        switch (level)
        {
        case LogLevel::Error:   msg_Err(intf, "%s", message.c_str()); break;
        case LogLevel::Warning: msg_Warn(intf, "%s", message.c_str()); break;
        case LogLevel::Debug:   msg_Dbg(intf, "%s", message.c_str()); break;
        }
    };
}

std::chrono::seconds ReadSkipStep(intf_thread_t* intf)
{
    return std::chrono::seconds{ var_InheritInteger(intf, "extrashort-jump-size") };
}

template <typename Interface>
int OpenInterface(vlc_object_t* object)
{
    auto* intf = reinterpret_cast<intf_thread_t*>(object);
    try
    {
        intf->p_sys = reinterpret_cast<intf_sys_t*>(new Interface(intf));
    }
    catch (const std::exception& ex)
    {
        msg_Err(intf, "SMTC: initialization failed: %s", ex.what());
        return VLC_EGENERIC;
    }
    catch (...)
    {
        msg_Err(intf, "SMTC: initialization failed");
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

template <typename Interface>
void CloseInterface(vlc_object_t* object)
{
    auto* intf = reinterpret_cast<intf_thread_t*>(object);
    delete reinterpret_cast<Interface*>(intf->p_sys);
    intf->p_sys = nullptr;
}

} // namespace
