#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

// OTDU_ADDITION: per-device input templates ('opentdu_data/controllers/*.ini').
// A template binds actions to the channels of the joysticks it matches. The
// bindings are layered on top of the saved (KMAP) bindings, which stay
// untouched; the template picked for each device is stored per profile in
// OpenTDU.ini.

class DeviceJoystick;
struct PlayerConfiguration;

struct InputTemplateBinding {
    std::string ActionName;
    std::string ChannelName;
    bool bInverted = false;
};

struct InputTemplate {
    // Sorted by priority (the most specific rule wins)
    enum eMatchKind : int32_t {
        MK_None = 0,
        MK_Gamepad,       // any joystick with an SDL gamepad mapping
        MK_Name,          // case insensitive substring of the product name
        MK_VendorProduct, // 'vvvv:pppp' (hex)
        MK_GUID           // SDL joystick GUID
    };

    struct MatchRule {
        eMatchKind Kind = MK_None;
        std::string Value;
    };

    std::string ID; // file name (without extension)
    std::string Name;
    std::vector<MatchRule> MatchRules;
    std::vector<InputTemplateBinding> Bindings;

    eMatchKind match( const DeviceJoystick &device ) const;
};

namespace InputTemplates {
    // Device key -> template ID (kNoTemplate when the user declined)
    using Selection = std::map<std::string, std::string>;

    void Load();

    // Most specific first
    std::vector<const InputTemplate *>
    FindMatching( const DeviceJoystick &device );
    const InputTemplate *GetSelected( const DeviceJoystick &device );
    void Select( const DeviceJoystick &device, const InputTemplate *pTemplate );

    Selection GetSelection();
    void SetSelection( const Selection &selection );

    void ApplyToActions( PlayerConfiguration &playerConfig );

    void OnDeviceConnected( const DeviceJoystick &device );
    void OnDeviceDisconnected( const DeviceJoystick &device );
    uint32_t GetDevicesRevision();

    void RequestRebind();
    bool ConsumeRebindRequest();
    void TickPrompt();
    // Key mapping screen 'Defaults': offers the template of the first
    // connected joystick that has one
    bool PromptFromDefaults();

    // OpenTDU.ini
    void ResetSettings();
    bool ParseSetting( const std::string &key, const std::string &value );
    void WriteSettings( std::ostream &stream );
} // namespace InputTemplates
