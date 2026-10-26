#include "shared.h"
#include "input_templates.h"

#include "config/gs_config.h"
#include "database/port_strings.h"
#include "flash/mng_flash_localize.h"
#include "flash/modal_message_box.h"
#include "input/devices/joystick.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "player_data/opentdu_settings.h"
#include "player_data/user_profile.h"

static constexpr const char *kTemplatesFolderPath = "opentdu_data/controllers";
static constexpr const char *kNoTemplate = "none";
static constexpr const char *kSettingPrefix = "Template.";
static constexpr const char *kInvertedFlag = "Inverted";

static std::vector<InputTemplate> gTemplates;
static InputTemplates::Selection gSelection;
static std::set<std::string> gPromptedDevices;
static std::string gPromptDeviceKey;
static std::string gPromptTemplateID;
static uint32_t gDevicesRevision = 0;
static bool gbRebindRequested = false;

static std::string Trim( const std::string &string ) {
    const size_t first = string.find_first_not_of( " \t\r\n" );
    if ( first == std::string::npos ) {
        return std::string();
    }
    const size_t last = string.find_last_not_of( " \t\r\n" );
    return string.substr( first, last - first + 1 );
}

static std::string Unquote( const std::string &string ) {
    if ( string.size() >= 2 && string.front() == '"' && string.back() == '"' ) {
        return string.substr( 1, string.size() - 2 );
    }
    return string;
}

static std::string ToLower( std::string string ) {
    std::transform(
        string.begin(), string.end(), string.begin(),
        []( const unsigned char c ) { return char( tolower( c ) ); } );
    return string;
}

static std::string GetVendorProductKey( const DeviceJoystick &device ) {
    if ( device.getVendorID() == 0 && device.getProductID() == 0 ) {
        return std::string();
    }

    char key[16];
    snprintf( key, sizeof( key ), "%04x:%04x", device.getVendorID(),
              device.getProductID() );
    return key;
}

static std::string GetGUIDKey( const DeviceJoystick &device ) {
    char guid[33];
    SDL_GUIDToString( SDL_GetJoystickGUIDForID( device.getJoystickID() ), guid,
                      sizeof( guid ) );
    return ToLower( guid );
}

// Identifies a device model (not a unit) in OpenTDU.ini
static std::string GetDeviceKey( const DeviceJoystick &device ) {
    const std::string vendorProductKey = GetVendorProductKey( device );
    return vendorProductKey.empty() ? GetGUIDKey( device ) : vendorProductKey;
}

static const InputTemplate *FindTemplate( const std::string &templateID ) {
    for ( const InputTemplate &inputTemplate : gTemplates ) {
        if ( StringCmpI( inputTemplate.ID.c_str(), templateID.c_str() ) == 0 ) {
            return &inputTemplate;
        }
    }
    return nullptr;
}

static DeviceAction *FindAction( PlayerConfiguration &playerConfig,
                                 const std::string &actionName ) {
    for ( uint32_t i = 0; i < playerConfig.NumActions; i++ ) {
        DeviceAction &action = playerConfig.pActions[i];
        if ( action.pName[0] == '\0' ) {
            break;
        }
        if ( strncmp( action.pName, actionName.c_str(),
                      sizeof( action.pName ) ) == 0 ) {
            return &action;
        }
    }
    return nullptr;
}

static void PrependChannel( DeviceAction &action,
                            InputChannel *pChannel,
                            const bool bInverted ) {
    for ( int32_t i = 0; i < action.NumAddedChannels; i++ ) {
        if ( action.pChannel[i].pChannel == pChannel ) {
            return;
        }
    }

    if ( action.NumAddedChannels >= action.NumChannels ) {
        OTDU_LOG_WARN( "Action '%s' has no free channel left for its input "
                       "template binding '%s'\n",
                       action.pName, pChannel->pName );
        return;
    }

    // Put first: GSDevice::tick picks the force feedback device from the first
    // channel of the steering action
    for ( int32_t i = action.NumAddedChannels; i > 0; i-- ) {
        action.pChannel[i] = action.pChannel[i - 1];
    }

    action.pChannel[0].pChannel = pChannel;
    action.pChannel[0].Inverted = bInverted ? 1 : 0;
    action.pChannel[0].Combination = 0;
    action.NumAddedChannels++;
}

static bool ParseMatchRule( const std::string &value,
                            InputTemplate::MatchRule &outRule ) {
    const size_t separator = value.find_first_of( " \t" );
    const std::string kind = value.substr( 0, separator );
    const std::string argument =
        ( separator != std::string::npos )
            ? ToLower( Unquote( Trim( value.substr( separator ) ) ) )
            : std::string();

    if ( StringCmpI( kind.c_str(), "Gamepad" ) == 0 ) {
        outRule.Kind = InputTemplate::MK_Gamepad;
    } else if ( StringCmpI( kind.c_str(), "Name" ) == 0 ) {
        outRule.Kind = InputTemplate::MK_Name;
    } else if ( StringCmpI( kind.c_str(), "VidPid" ) == 0 ) {
        outRule.Kind = InputTemplate::MK_VendorProduct;
    } else if ( StringCmpI( kind.c_str(), "Guid" ) == 0 ) {
        outRule.Kind = InputTemplate::MK_GUID;
    } else {
        return false;
    }

    outRule.Value = argument;
    return outRule.Kind == InputTemplate::MK_Gamepad || !argument.empty();
}

static bool LoadTemplate( const std::filesystem::path &path,
                          InputTemplate &outTemplate ) {
    std::ifstream stream( path );
    if ( !stream.is_open() ) {
        return false;
    }

    outTemplate.ID = path.stem().string();
    outTemplate.Name = outTemplate.ID;

    enum eSection { S_None, S_Template, S_Bindings } section = S_None;

    std::string line;
    while ( std::getline( stream, line ) ) {
        const size_t comment = line.find_first_of( ";#" );
        if ( comment != std::string::npos ) {
            line.erase( comment );
        }
        line = Trim( line );
        if ( line.empty() ) {
            continue;
        }

        if ( line.front() == '[' && line.back() == ']' ) {
            const std::string sectionName = line.substr( 1, line.size() - 2 );
            if ( StringCmpI( sectionName.c_str(), "Template" ) == 0 ) {
                section = S_Template;
            } else if ( StringCmpI( sectionName.c_str(), "Bindings" ) == 0 ) {
                section = S_Bindings;
            } else {
                section = S_None;
            }
            continue;
        }

        const size_t equals = line.find( '=' );
        if ( equals == std::string::npos ) {
            continue;
        }

        const std::string key = Trim( line.substr( 0, equals ) );
        const std::string value = Trim( line.substr( equals + 1 ) );

        if ( section == S_Template ) {
            if ( StringCmpI( key.c_str(), "Name" ) == 0 ) {
                outTemplate.Name = Unquote( value );
            } else if ( StringCmpI( key.c_str(), "Match" ) == 0 ) {
                InputTemplate::MatchRule rule;
                if ( ParseMatchRule( value, rule ) ) {
                    outTemplate.MatchRules.push_back( rule );
                } else {
                    OTDU_LOG_WARN( "%s: invalid match rule '%s'\n",
                                   path.string().c_str(), value.c_str() );
                }
            }
        } else if ( section == S_Bindings ) {
            InputTemplateBinding binding;
            binding.ActionName = key;

            const size_t separator = value.find_first_of( " \t" );
            binding.ChannelName = Unquote( value.substr( 0, separator ) );
            binding.bInverted =
                separator != std::string::npos &&
                StringCmpI( Trim( value.substr( separator ) ).c_str(),
                            kInvertedFlag ) == 0;
            outTemplate.Bindings.push_back( binding );
        }
    }

    return !outTemplate.MatchRules.empty() && !outTemplate.Bindings.empty();
}

static bool CanDisplayPrompt() {
    if ( gpFlashMessageBox == nullptr ||
         gpFlashMessageBox->getFlashPlayer() == nullptr ||
         gpFlashMessageBox->getDisplayState() !=
             ModalMessageBox::MMBDS_Closed ||
         gpMngFlashLocalize == nullptr ) {
        return false;
    }

    // Selections are saved per profile
    return !gUserProfileRegister.getActiveProfileName().empty();
}

static void OnTemplatePromptClosed( ePopupAnswer answer, void *, int32_t ) {
    OTDU_LOG_INFO( "Input template prompt for '%s' closed (answer %d, "
                   "profile '%s')\n",
                   gPromptDeviceKey.c_str(), int32_t( answer ),
                   gUserProfileRegister.getActiveProfileName().c_str() );

    if ( answer == ePopupAnswer::PA_Unknown ) {
        // Closed by another prompt; asked again next session
        return;
    }

    gSelection[gPromptDeviceKey] =
        ( answer == ePopupAnswer::PA_Accept ) ? gPromptTemplateID : kNoTemplate;
    OpenTDUSettings::Save();
    InputTemplates::RequestRebind();
}

// The key mapping screen applies (or reverts) the choice when it closes
static void OnDefaultsPromptClosed( ePopupAnswer answer, void *, int32_t ) {
    if ( answer != ePopupAnswer::PA_Unknown ) {
        gSelection[gPromptDeviceKey] = ( answer == ePopupAnswer::PA_Accept )
                                           ? gPromptTemplateID
                                           : kNoTemplate;
    }
}

static void DisplayTemplatePrompt( const DeviceJoystick &device,
                                   const InputTemplate &inputTemplate,
                                   const ePortString message,
                                   PopupAnswerCallback_t pAnswerCallback ) {
    gPromptDeviceKey = GetDeviceKey( device );
    gPromptTemplateID = inputTemplate.ID;

    gpMngFlashLocalize->setSysVar( "DEVICE", device.getProductName() );
    gpMngFlashLocalize->setSysVar( "TEMPLATE", inputTemplate.Name.c_str() );
    TestDrive::DisplayModalPrompt( true, GetPortStringHashcode( message ), 0,
                                   pAnswerCallback, nullptr, nullptr, 0,
                                   GetPortString( message ), true, false,
                                   MPT_Warning, gpFlashMessageBox );
    gpMngFlashLocalize->clearAllSysVar();
}

InputTemplate::eMatchKind
InputTemplate::match( const DeviceJoystick &device ) const {
    eMatchKind bestMatch = MK_None;
    for ( const MatchRule &rule : MatchRules ) {
        bool bMatch = false;
        switch ( rule.Kind ) {
        case MK_Gamepad:
            bMatch = device.isGamepad();
            break;
        case MK_Name:
            bMatch = ToLower( device.getProductName() ).find( rule.Value ) !=
                     std::string::npos;
            break;
        case MK_VendorProduct:
            bMatch = GetVendorProductKey( device ) == rule.Value;
            break;
        case MK_GUID:
            bMatch = GetGUIDKey( device ) == rule.Value;
            break;
        default:
            break;
        }

        if ( bMatch && rule.Kind > bestMatch ) {
            bestMatch = rule.Kind;
        }
    }
    return bestMatch;
}

namespace InputTemplates {
    void Load() {
        gTemplates.clear();

        std::error_code errorCode;
        if ( !std::filesystem::is_directory( kTemplatesFolderPath,
                                             errorCode ) ) {
            OTDU_LOG_WARN( "'%s' does not exist; no input template available\n",
                           kTemplatesFolderPath );
            return;
        }

        std::vector<std::filesystem::path> paths;
        for ( const auto &entry : std::filesystem::directory_iterator(
                  kTemplatesFolderPath, errorCode ) ) {
            if ( entry.is_regular_file() &&
                 StringCmpI( entry.path().extension().string().c_str(),
                             ".ini" ) == 0 ) {
                paths.push_back( entry.path() );
            }
        }
        std::sort( paths.begin(), paths.end() );

        for ( const std::filesystem::path &path : paths ) {
            InputTemplate inputTemplate;
            if ( !LoadTemplate( path, inputTemplate ) ) {
                OTDU_LOG_WARN( "Skipping input template '%s' (no match rule "
                               "or binding)\n",
                               path.string().c_str() );
                continue;
            }

            OTDU_LOG_INFO( "Input template '%s' (%s): %zu binding(s)\n",
                           inputTemplate.Name.c_str(), inputTemplate.ID.c_str(),
                           inputTemplate.Bindings.size() );
            gTemplates.push_back( std::move( inputTemplate ) );
        }
    }

    std::vector<const InputTemplate *>
    FindMatching( const DeviceJoystick &device ) {
        std::vector<std::pair<InputTemplate::eMatchKind, const InputTemplate *>>
            matches;
        for ( const InputTemplate &inputTemplate : gTemplates ) {
            const InputTemplate::eMatchKind matchKind =
                inputTemplate.match( device );
            if ( matchKind != InputTemplate::MK_None ) {
                matches.emplace_back( matchKind, &inputTemplate );
            }
        }

        std::stable_sort(
            matches.begin(), matches.end(),
            []( const auto &a, const auto &b ) { return a.first > b.first; } );

        std::vector<const InputTemplate *> templates;
        for ( const auto &match : matches ) {
            templates.push_back( match.second );
        }
        return templates;
    }

    const InputTemplate *GetSelected( const DeviceJoystick &device ) {
        auto it = gSelection.find( GetDeviceKey( device ) );
        if ( it == gSelection.end() ) {
            return nullptr;
        }

        const InputTemplate *pTemplate = FindTemplate( it->second );
        return ( pTemplate != nullptr &&
                 pTemplate->match( device ) != InputTemplate::MK_None )
                   ? pTemplate
                   : nullptr;
    }

    void Select( const DeviceJoystick &device,
                 const InputTemplate *pTemplate ) {
        gSelection[GetDeviceKey( device )] =
            ( pTemplate != nullptr ) ? pTemplate->ID : kNoTemplate;
    }

    Selection GetSelection() { return gSelection; }

    void SetSelection( const Selection &selection ) { gSelection = selection; }

    void ApplyToActions( PlayerConfiguration &playerConfig ) {
        for ( uint32_t i = 0; i < gEdDev.MaxNumJoystick; i++ ) {
            DeviceJoystick *pDevice = gEdDev.getConnectedJoystick( i );
            if ( pDevice == nullptr ) {
                continue;
            }

            const InputTemplate *pTemplate = GetSelected( *pDevice );
            if ( pTemplate == nullptr ) {
                continue;
            }

            int32_t numBound = 0;
            for ( const InputTemplateBinding &binding : pTemplate->Bindings ) {
                DeviceAction *pAction =
                    FindAction( playerConfig, binding.ActionName );
                InputChannel *pChannel =
                    pDevice->getChannel( binding.ChannelName );
                if ( pAction != nullptr && pChannel != nullptr ) {
                    PrependChannel( *pAction, pChannel, binding.bInverted );
                    numBound++;
                }
            }

            OTDU_LOG_INFO( "Input template '%s' applied to '%s' (%d/%zu "
                           "binding(s) resolved)\n",
                           pTemplate->Name.c_str(), pDevice->getProductName(),
                           numBound, pTemplate->Bindings.size() );
        }
    }

    void OnDeviceConnected( const DeviceJoystick &device ) {
        gDevicesRevision++;

        const std::vector<const InputTemplate *> templates =
            FindMatching( device );
        OTDU_LOG_INFO( "Joystick connected: '%s' (%s, %zu input template(s))\n",
                       device.getProductName(), GetDeviceKey( device ).c_str(),
                       templates.size() );
    }

    void OnDeviceDisconnected( const DeviceJoystick &device ) {
        gDevicesRevision++;

        OTDU_LOG_INFO( "Joystick disconnected: '%s'\n",
                       device.getProductName() );
    }

    uint32_t GetDevicesRevision() { return gDevicesRevision; }

    void RequestRebind() { gbRebindRequested = true; }

    bool ConsumeRebindRequest() {
        const bool bRequested = gbRebindRequested;
        gbRebindRequested = false;
        return bRequested;
    }

    void TickPrompt() {
        if ( gTemplates.empty() || !CanDisplayPrompt() ) {
            return;
        }

        for ( uint32_t i = 0; i < gEdDev.MaxNumJoystick; i++ ) {
            DeviceJoystick *pDevice = gEdDev.getConnectedJoystick( i );
            if ( pDevice == nullptr ) {
                continue;
            }

            const std::string deviceKey = GetDeviceKey( *pDevice );
            if ( gSelection.count( deviceKey ) != 0 ||
                 gPromptedDevices.count( deviceKey ) != 0 ) {
                continue;
            }

            const std::vector<const InputTemplate *> templates =
                FindMatching( *pDevice );
            if ( templates.empty() ) {
                continue;
            }

            gPromptedDevices.insert( deviceKey );
            DisplayTemplatePrompt( *pDevice, *templates.front(),
                                   PS_InputTemplatePrompt,
                                   OnTemplatePromptClosed );
            return;
        }
    }

    bool PromptFromDefaults() {
        if ( !CanDisplayPrompt() ) {
            return false;
        }

        for ( uint32_t i = 0; i < gEdDev.MaxNumJoystick; i++ ) {
            DeviceJoystick *pDevice = gEdDev.getConnectedJoystick( i );
            if ( pDevice == nullptr ) {
                continue;
            }

            const std::vector<const InputTemplate *> templates =
                FindMatching( *pDevice );
            if ( !templates.empty() ) {
                DisplayTemplatePrompt( *pDevice, *templates.front(),
                                       PS_InputTemplateDefaultsPrompt,
                                       OnDefaultsPromptClosed );
                return true;
            }
        }

        return false;
    }

    void ResetSettings() {
        gSelection.clear();
        gPromptedDevices.clear();
        RequestRebind();
    }

    bool ParseSetting( const std::string &key, const std::string &value ) {
        const size_t prefixLength = strlen( kSettingPrefix );
        if ( StringNCmpI( key.c_str(), kSettingPrefix, prefixLength ) != 0 ||
             key.size() <= prefixLength ) {
            return false;
        }

        gSelection[ToLower( key.substr( prefixLength ) )] = value;
        return true;
    }

    void WriteSettings( std::ostream &stream ) {
        stream << "[Input]\n";
        for ( const auto &entry : gSelection ) {
            stream << kSettingPrefix << entry.first << " = " << entry.second
                   << "\n";
        }
    }
} // namespace InputTemplates
