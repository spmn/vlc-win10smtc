#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"

#include <vlc_common.h>
#include <vlc_plugin.h>


int Open(vlc_object_t* object)
{
    VLC_UNUSED(object);
    return VLC_SUCCESS;
}

void Close(vlc_object_t* object)
{
    VLC_UNUSED(object);
}

vlc_module_begin()
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_shortname("win10smtc")
    set_description("Windows 10 SMTC integration")
    set_capability("interface", 0)
    set_callbacks(Open, Close)
vlc_module_end()
