#pragma once

#define PLUGIN_SHORTNAME        "win10smtc"
#define PLUGIN_CAPABILITY       "interface"
#define PLUGIN_DESCRIPTION      "Windows SMTC integration"
#define PLUGIN_FILE_DESCRIPTION "Windows SMTC integration for VLC"
#define PLUGIN_PRODUCT_NAME     "vlc-win10smtc"
#define PLUGIN_FILENAME         "libwin10smtc_plugin.dll"  // must match TargetName

#define PLUGIN_VERSION_MAJOR 5
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 0

#define PLUGIN_STRINGIZE2(s) #s
#define PLUGIN_STRINGIZE(s) PLUGIN_STRINGIZE2(s)

#define PLUGIN_VERSION_STR                     \
    PLUGIN_STRINGIZE(PLUGIN_VERSION_MAJOR) "." \
    PLUGIN_STRINGIZE(PLUGIN_VERSION_MINOR) "." \
    PLUGIN_STRINGIZE(PLUGIN_VERSION_PATCH)

// CI passes the commit unquoted (MSBuild PluginBuildId); stringizing sidesteps quote escaping.
#ifdef PLUGIN_BUILD_ID_TOKEN
#define PLUGIN_BUILD_ID PLUGIN_STRINGIZE(PLUGIN_BUILD_ID_TOKEN)
#else
#define PLUGIN_BUILD_ID "local"
#endif
