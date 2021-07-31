#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_input.h>


struct intf_sys_t
{
    intf_sys_t(const intf_sys_t&) = delete;
    void operator=(const intf_sys_t&) = delete;

    explicit intf_sys_t(intf_thread_t* intf) :
        intf{ intf },
        playlist{ pl_Get(intf) },
        input{ nullptr }
    {
    }

    intf_thread_t* intf;
    playlist_t* playlist;
    input_thread_t* input;
};


int InputEvent(vlc_object_t* object, char const* cmd,
    vlc_value_t oldval, vlc_value_t newval, void* data)
{
    VLC_UNUSED(cmd);
    VLC_UNUSED(oldval);

    intf_thread_t* intf = (intf_thread_t*)data;
    intf_sys_t* sys = intf->p_sys;
    input_thread_t* input = (input_thread_t*)object;

    if (newval.i_int == INPUT_EVENT_STATE) {
        input_state_e state = (input_state_e)var_GetInteger(input, "state");

        msg_Dbg(input, "New input state: %d", state);
    }
    else if (newval.i_int == INPUT_EVENT_DEAD) {
        assert(sys->input);
        vlc_object_release(sys->input);
        sys->input = nullptr;

        msg_Dbg(input, "Input died");
    }

    return VLC_SUCCESS;
}

int PlaylistEvent(vlc_object_t* object, char const* cmd,
    vlc_value_t oldval, vlc_value_t newval, void* data)
{
    VLC_UNUSED(object); VLC_UNUSED(cmd); VLC_UNUSED(oldval);

    intf_thread_t* intf = (intf_thread_t*)data;
    intf_sys_t* sys = intf->p_sys;
    input_thread_t* input = (input_thread_t*)newval.p_address;

    assert(input);

    sys->input = (input_thread_t*)vlc_object_hold(input);
    var_AddCallback(input, "intf-event", InputEvent, intf);

    return VLC_SUCCESS;
}

int Open(vlc_object_t* object)
{
    intf_thread_t* intf = (intf_thread_t*)object;
    intf_sys_t* sys = new intf_sys_t(intf);

    intf->p_sys = sys;

    if (!sys)
        return VLC_EGENERIC;

    var_AddCallback(sys->playlist, "input-current", PlaylistEvent, intf);

    return VLC_SUCCESS;
}

void Close(vlc_object_t* object)
{
    intf_thread_t* intf = (intf_thread_t*)object;
    intf_sys_t* sys = intf->p_sys;

    assert(!sys->input);

    var_DelCallback(sys->playlist, "input-current", PlaylistEvent, intf);
    delete intf->p_sys;
}

vlc_module_begin()
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_shortname("win10smtc")
    set_description("Windows 10 SMTC integration")
    set_capability("interface", 0)
    set_callbacks(Open, Close)
vlc_module_end()
