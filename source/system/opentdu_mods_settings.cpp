#include "shared.h"
#include "opentdu_mods_settings.h"

#include "database/gs_database.h"
#include "database/port_strings.h"
#include "filesystem/gs_file.h"
#include "filesystem/mod_installer.h"
#include "filesystem/mod_manager.h"
#include "flash/gs_flash.h"
#include "player_data/gs_playerdata.h"

static std::string GetPackStateLabel( const ModPack &pack ) {
    if ( pack.bMandatory ) {
        return GetPortString( PS_ModBuiltIn );
    }
    return GetPortString( pack.bEnabled ? PS_ModEnabled : PS_ModDisabled );
}

static std::string ToNarrow( const std::wstring &text ) {
    std::string narrow;
    narrow.reserve( text.size() );
    for ( const wchar_t character : text ) {
        narrow.push_back( static_cast<char>( character ) );
    }
    return narrow;
}

static std::vector<OptionsDetail> GetPackDetails( const ModPack &pack ) {
    std::vector<OptionsDetail> details;
    details.push_back(
        { ToNarrow( pack.DisplayName ), GetPackStateLabel( pack ) } );

    if ( pack.NumEntries != 0u ) {
        details.push_back( { GetPortString( PS_ModFiles ),
                             std::to_string( pack.NumEntries ) } );
    }
    if ( pack.bMounted ) {
        details.push_back( { GetPortString( PS_ModMountedOnMap ),
                             std::to_string( pack.FilemapIndex ) } );
    }
    if ( !pack.Status.empty() ) {
        details.push_back( { GetPortString( PS_ModStatus ), pack.Status } );
    }

    if ( gpModInstaller != nullptr ) {
        for ( const ModDBInjection &injection :
              gpModInstaller->getDatabaseInjections() ) {
            if ( injection.PackName != pack.DisplayName ) {
                continue;
            }

            if ( !injection.Rows.empty() ) {
                details.push_back(
                    { injection.Table,
                      "+" + std::to_string( injection.Rows.size() ) + " " +
                          GetPortString( PS_ModRows ) } );
            }
            if ( !injection.Strings.empty() ) {
                details.push_back(
                    { injection.Table,
                      "+" + std::to_string( injection.Strings.size() ) + " " +
                          GetPortString( PS_ModStrings ) } );
            }
        }
    }

    return details;
}

std::vector<OptionsTab> OpenTDUModsSettings::buildTabs() {
    std::vector<OptionsTab> tabs( 1 );
    OptionsTab &tab = tabs[0];
    tab.Label = GetPortString( PS_Mods );
    tab.bSplitLayout = true;

    std::vector<std::string> ids;
    for ( const ModPack &pack : gpModManager->getPacks() ) {
        ids.push_back( pack.Id );

        const std::string id = pack.Id;
        OptionsRow row;
        row.Type = eOptionsRowType::ORT_Checkbox;
        row.GetLabel = [id]() {
            const ModPack *pPack = gpModManager->findPack( id.c_str() );
            if ( pPack == nullptr ) {
                return id;
            }
            return pPack->Description.resolve(
                gpPlayerData != nullptr ? gpPlayerData->getLanguage() : "us" );
        };
        row.IsChecked = [id]() {
            const ModPack *pPack = gpModManager->findPack( id.c_str() );
            return pPack != nullptr && pPack->bEnabled;
        };
        row.Step = [id]( int32_t ) {
            const ModPack *pPack = gpModManager->findPack( id.c_str() );
            if ( pPack == nullptr || pPack->bMandatory ) {
                gpFlash->getFlashSound()->playFrontEndSound( "wrong", 0,
                                                             false );
                return;
            }
            gpModManager->setEnabled( id.c_str(), !pPack->bEnabled );
        };
        tab.Rows.push_back( row );
    }

    tab.GetDetails = [ids]( int32_t row ) {
        const ModPack *pPack = ( row >= 0 && row < int32_t( ids.size() ) )
                                   ? gpModManager->findPack( ids[row].c_str() )
                                   : nullptr;
        return pPack != nullptr ? GetPackDetails( *pPack )
                                : std::vector<OptionsDetail>();
    };

    return tabs;
}

void OpenTDUModsSettings::open( OptionsWidgetPage &page ) {
    openedStates.clear();
    for ( const ModPack &pack : gpModManager->getPacks() ) {
        openedStates.emplace_back( pack.Id, bool( pack.bEnabled ) );
    }

    page.open( buildTabs(), [this]() { confirm(); }, [this]() { cancel(); } );
}

void OpenTDUModsSettings::confirm() {
    if ( !gpModManager->hasPendingChanges() ) {
        return;
    }

    gpModManager->save();

    if ( gpDatabase != nullptr && gpPlayerData != nullptr ) {
        gpDatabase->releaseDatabases( /* bRetainBuffers */ true );
        gpDatabase->reloadDatabases( gpPlayerData->getLanguage(), true );
        gpDatabase->republishFrontEndStrings();
    } else if ( gpFile != nullptr ) {
        gpFile->initCarPacks( false );
    }
}

void OpenTDUModsSettings::cancel() {
    for ( const auto &state : openedStates ) {
        if ( gpModManager->isEnabled( state.first.c_str() ) != state.second ) {
            gpModManager->setEnabled( state.first.c_str(), state.second );
        }
    }
    gpModManager->setPendingChanges( false );
}
