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
    PS_NightLights,
    PS_Low,
    PS_Ultra,
    PS_HDRDisplay,

    // Mods Manager
    PS_ModsFolderEmpty,
    PS_ModEnabled,
    PS_ModDisabled,
    PS_ModBuiltIn,
    PS_ModFiles,
    PS_ModMountedOnMap,
    PS_ModRows,
    PS_ModStrings,

    // Input templates
    PS_InputTemplatePrompt,
    PS_InputTemplateDefaultsPrompt,

    // OpenTDU options
    PS_OpenTDUOptions,
    PS_OpenTDUControlsHelp,
    PS_OpenTDUGameSettingsHelp,
    PS_OpenTDUVideoHelp,
    PS_Quality,
    PS_HDR,
    PS_HDRPaperWhite,
    PS_HDRPeakBrightness,
    PS_HDRHighlights,
    PS_SoftHighlights,
    PS_Auto,
    PS_Mods,
    PS_OpenTDUModsHelp,
    PS_ModStatus,

    PS_Count
};

const char *GetPortString( const ePortString id );
uint64_t GetPortStringHashcode( const ePortString id );
