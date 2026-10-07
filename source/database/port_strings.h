#pragma once

enum ePortString : uint32_t {
    // PCOptions
    PS_DisplayMode = 0,
    PS_FrameRate,
    PS_Reflections,
    PS_VSync,
    PS_FullScreen,
    PS_Borderless,
    PS_Windowed,
    PS_Unlimited,
    PS_High,
    PS_Default,

    // Mods Manager
    PS_ModsFolderEmpty,
    PS_ModEnabled,
    PS_ModDisabled,
    PS_ModBuiltIn,
    PS_ModFiles,
    PS_ModMountedOnMap,
    PS_ModRows,
    PS_ModStrings,

    PS_Count
};

const char *GetPortString( const ePortString id );
