#include "shared.h"
#include "opentdu_settings.h"

#include "filesystem/gs_file.h"
#include "gs_timer.h"
#include "player_data/user_profile.h"
#include "render/gs_render.h"
#include "render/mng_env_map.h"
#include "system/ed_sys.h"
#include "system/mng_options.h"

#include "config/gs_config.h"

static constexpr const char *kSettingsFilename = "OpenTDU.ini";

// Profile value kept while -maxfps overrides it, so saving does not persist the
// command line value
static int32_t gProfileMaxFrameRate = 0;

static constexpr const char *kDisplayModeNames[int32_t(
    eDisplayMode::DM_Count )] = { "Windowed", "Fullscreen", "Borderless" };

static bool GetSettingsPath( std::filesystem::path &outPath ) {
    const std::string &profileName =
        gUserProfileRegister.getActiveProfileName();
    if ( gpFile == nullptr || profileName.empty() ) {
        return false;
    }

    outPath = std::filesystem::path( gpFile->getSaveFolder() );
    outPath /= "savegame";
    outPath /= profileName;
    outPath /= kSettingsFilename;
    return true;
}

// TODO: Share with gs_config
static std::string Trim( const std::string &string ) {
    const size_t first = string.find_first_not_of( " \t\r\n" );
    if ( first == std::string::npos ) {
        return std::string();
    }
    const size_t last = string.find_last_not_of( " \t\r\n" );
    return string.substr( first, last - first + 1 );
}

static void ApplySetting( const std::string &key, const std::string &value ) {
    if ( StringCmpI( key.c_str(), "DisplayMode" ) == 0 ) {
        for ( int32_t i = 0; i < int32_t( eDisplayMode::DM_Count ); i++ ) {
            if ( StringCmpI( value.c_str(), kDisplayModeNames[i] ) == 0 ) {
                ApplyDisplayMode( eDisplayMode( i ) );
                return;
            }
        }
        OTDU_LOG_WARN( "%s: unknown display mode '%s'\n", kSettingsFilename,
                       value.c_str() );
    } else if ( StringCmpI( key.c_str(), "MaxFrameRate" ) == 0 ) {
        gProfileMaxFrameRate = std::max( atoi( value.c_str() ), 0 );
        if ( !IsMaxFrameRateForced() ) {
            SetMaxFrameRate( gProfileMaxFrameRate );
        }
    } else if ( StringCmpI( key.c_str(), "DenseReflections" ) == 0 ) {
        gbDebugEnvMapDenseReflection = atoi( value.c_str() ) != 0;
    } else if ( StringCmpI( key.c_str(), "VSync" ) == 0 ) {
        if ( gpRender != nullptr && gpConfig != nullptr ) {
            gpRender->setVSyncMode(
                ( atoi( value.c_str() ) != 0 )
                    ? eVSyncMode( Clamp( gpConfig->NbVBL, 1, 4 ) )
                    : eVSyncMode::VSM_Immediate );
        }
    }
}

namespace OpenTDUSettings {
    void Load() {
        std::filesystem::path settingsPath;
        if ( !GetSettingsPath( settingsPath ) ) {
            return;
        }

        gProfileMaxFrameRate = IsMaxFrameRateForced() ? 0 : GetMaxFrameRate();

        std::ifstream stream( settingsPath );
        if ( !stream.is_open() ) {
            return;
        }

        const eVSyncMode vsyncModeBeforeLoad = ( gpRender != nullptr )
                                                   ? gpRender->getVSyncMode()
                                                   : eVSyncMode::VSM_Immediate;

        std::string line;
        while ( std::getline( stream, line ) ) {
            const size_t comment = line.find_first_of( ";#" );
            if ( comment != std::string::npos ) {
                line.erase( comment );
            }

            const size_t equals = line.find( '=' );
            if ( equals == std::string::npos ) {
                continue;
            }

            ApplySetting( Trim( line.substr( 0, equals ) ),
                          Trim( line.substr( equals + 1 ) ) );
        }

        if ( gpRender != nullptr && gpRender->isInitialized() &&
             gpConfig != nullptr &&
             ( !gEdSys.isWindowStateApplied( gpConfig->FullscreenMode,
                                             bool( gpConfig->bWindowed ) ) ||
               gpRender->getVSyncMode() != vsyncModeBeforeLoad ) ) {
            gpRender->resize(
                gWindowWidth, gWindowHeight, uint32_t( gPickedRefreshRate ),
                gpRender->getAAMethod(), gpRender->isHDREnabled() );
        }
    }

    void Save() {
        std::filesystem::path settingsPath;
        if ( !GetSettingsPath( settingsPath ) ) {
            return;
        }

        std::ofstream stream( settingsPath, std::ofstream::trunc );
        if ( !stream.is_open() ) {
            OTDU_LOG_ERROR( "Failed to write '%s'\n",
                            settingsPath.string().c_str() );
            return;
        }

        stream << "[Video]\n";
        stream << "DisplayMode = "
               << kDisplayModeNames[int32_t( GetActiveDisplayMode() )] << "\n";
        stream << "MaxFrameRate = "
               << ( IsMaxFrameRateForced() ? gProfileMaxFrameRate
                                           : GetMaxFrameRate() )
               << "\n";
        stream << "DenseReflections = "
               << ( gbDebugEnvMapDenseReflection ? 1 : 0 ) << "\n";
        if ( gpRender != nullptr ) {
            stream << "VSync = "
                   << ( gpRender->getVSyncMode() != eVSyncMode::VSM_Immediate
                            ? 1
                            : 0 )
                   << "\n";
        }
    }
} // namespace OpenTDUSettings
