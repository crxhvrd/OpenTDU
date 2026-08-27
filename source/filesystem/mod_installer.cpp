#include "shared.h"
#include "mod_installer.h"

#include "file_direct_access.h"
#include "player_data/gs_playerdata.h"

static ModInstaller gModInstaller = {};
ModInstaller *gpModInstaller = &gModInstaller; // DAT_0147aef0

static constexpr const char *kContentDescFilename = "contentdesc.xml";

ModInstaller::ModInstaller() : contentDescSize( 0 ) { pDescription[0] = '\0'; }

ModInstaller::~ModInstaller() {}

void ModInstaller::reset() {
    // FUN_008a3750
    for ( FoundCar &car : foundCars ) {
        car = FoundCar();
    }

    installedContent.clear();
    dbInjections.clear();
    contentDescBuffer.clear();
    contentDescSize = 0;
    pDescription[0] = '\0';
}

static void GetActiveLanguage( char *pOutLanguage, const size_t size ) {
    OTDU_ASSERT( pOutLanguage != nullptr && size > 1 );

    const char *pLanguage =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLanguage() : "us";
    strncpy( pOutLanguage, pLanguage, size - 1 );
    pOutLanguage[size - 1] = '\0';
}

FoundCar *ModInstaller::findCarSlot( const uint32_t databaseId,
                                     const char *pEnvName ) {
    // FUN_008a3520
    if ( pEnvName == nullptr ) {
        return nullptr;
    }

    for ( FoundCar &car : foundCars ) {
        if ( car.DatabaseID == databaseId &&
             StringNCmpI( car.CardealerSpotName, pEnvName, 0x20 ) == 0 ) {
            return &car;
        }
    }

    return nullptr;
}

FoundCar *ModInstaller::allocateCarSlot() {
    // FUN_008a3580
    for ( FoundCar &car : foundCars ) {
        if ( car.DatabaseID == 0 ) {
            return &car;
        }
    }

    return nullptr;
}

const FoundCar *ModInstaller::findCar( const uint32_t databaseId,
                                       const char *pEnvName ) const {
    return const_cast<ModInstaller *>( this )->findCarSlot( databaseId,
                                                            pEnvName );
}

void ModInstaller::onAddVehicle( const char *pLanguage,
                                 const uint32_t databaseId,
                                 const char *pEnvName ) {
    // FUN_008a36d0
    if ( pEnvName == nullptr || databaseId == 0 ) {
        OTDU_LOG_WARN( "ADDVEHICLE needs both envname and database_id\n" );
        return;
    }

    OTDU_LOG_INFO( "OnAddVehicule(env=%s,id=%u,lang=%s)\n", pEnvName,
                   databaseId, pLanguage );

    if ( findCarSlot( databaseId, pEnvName ) != nullptr ) {
        OTDU_LOG_INFO( "==>Already Added\n" );
        return;
    }

    FoundCar *pCar = allocateCarSlot();
    if ( pCar == nullptr ) {
        OTDU_LOG_WARN( "**WARNING** ==>no more Slot Available\n" );
        return;
    }

    *pCar = FoundCar();
    pCar->DatabaseID = databaseId;
    strncpy( pCar->CardealerSpotName, pEnvName,
             sizeof( pCar->CardealerSpotName ) - 1 );
}

void ModInstaller::onUnlockVehicle( const char *pLanguage,
                                    const uint32_t databaseId,
                                    const char *pEnvName ) {
    // FUN_008a3660
    if ( pEnvName == nullptr || databaseId == 0 ) {
        OTDU_LOG_WARN( "UNLOCKVEHICLE needs both envname and database_id\n" );
        return;
    }

    OTDU_LOG_INFO( "OnUnlockVehicule(env=%s,id=%u,lang=%s)\n", pEnvName,
                   databaseId, pLanguage );

    FoundCar *pCar = findCarSlot( databaseId, pEnvName );
    if ( pCar == nullptr ) {
        pCar = allocateCarSlot();
        if ( pCar == nullptr ) {
            OTDU_LOG_WARN( "**WARNING** ==>no more Slot Available\n" );
            return;
        }
    }

    *pCar = FoundCar();
    pCar->DatabaseID = databaseId;
    pCar->bUnlocked = true;
    strncpy( pCar->CardealerSpotName, pEnvName,
             sizeof( pCar->CardealerSpotName ) - 1 );
}

void ModInstaller::onPostNews( const char *pText, const int32_t moneyReward ) {
    // FUN_008a3bf0
    OTDU_LOG_INFO( "OnPostNews(text=%s,mon=%d)\n", pText, moneyReward );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
}

void ModInstaller::onAddRadioStation( const char *pEnvName,
                                      const EnumerateContextItem &item ) {
    // CXmlInstaller::AddRadioStation
    OTDU_LOG_INFO( "OnAddRadioStation(env=%s,pack=%S)\n", pEnvName,
                   item.DisplayName.c_str() );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: RADIO
}

ModDBInjection &
ModInstaller::getInjectionSlot( const EnumerateContextItem &item,
                                const char *pTable ) {
    for ( ModDBInjection &injection : dbInjections ) {
        if ( injection.PackName == item.DisplayName &&
             StringCmpI( injection.Table.c_str(), pTable ) == 0 ) {
            return injection;
        }
    }

    ModDBInjection injection;
    injection.PackName = item.DisplayName;
    injection.Table = pTable;
    dbInjections.push_back( injection );

    return dbInjections.back();
}

void ModInstaller::onAddDBRow( const EnumerateContextItem &item,
                               const char *pTable,
                               const char *pRow ) {
    if ( pTable == nullptr || *pTable == '\0' || pRow == nullptr ||
         *pRow == '\0' ) {
        OTDU_LOG_WARN( "ADDDBROW needs a table attribute and a row body\n" );
        return;
    }

    if ( strchr( pRow, '\n' ) != nullptr || strchr( pRow, '\r' ) != nullptr ) {
        OTDU_LOG_ERROR( "ADDDBROW for '%s' spans several lines; a row must be "
                        "a single ';'-separated line\n",
                        pTable );
        return;
    }

    OTDU_LOG_INFO( "OnAddDBRow(table=%s,row=%.64s)\n", pTable, pRow );
    getInjectionSlot( item, pTable ).Rows.push_back( pRow );
}

void ModInstaller::onAddDBString( const EnumerateContextItem &item,
                                  const char *pTable,
                                  const char *pHashcode,
                                  const char *pValue ) {
    if ( pTable == nullptr || *pTable == '\0' || pHashcode == nullptr ||
         *pHashcode == '\0' || pValue == nullptr ) {
        OTDU_LOG_WARN(
            "ADDDBSTRING needs table and id attributes and a body\n" );
        return;
    }

    ModDBString string;
    string.Hashcode = strtoull( pHashcode, nullptr, 10 );
    string.Value = pValue;

    if ( string.Hashcode == 0 ) {
        OTDU_LOG_ERROR( "ADDDBSTRING id '%s' is not a valid hashcode\n",
                        pHashcode );
        return;
    }

    if ( string.Value.find_first_of( "{}\r\n" ) != std::string::npos ) {
        OTDU_LOG_ERROR( "ADDDBSTRING %llu contains a brace or a newline\n",
                        string.Hashcode );
        return;
    }

    OTDU_LOG_INFO( "OnAddDBString(table=%s,id=%llu,value=%.64s)\n", pTable,
                   string.Hashcode, string.Value.c_str() );
    getInjectionSlot( item, pTable ).Strings.push_back( string );
}

bool ModInstaller::addInstalledContent( const std::wstring &displayName,
                                        const uint32_t actionMask ) {
    // FUN_008a35c0
    if ( installedContent.size() >= kMaxInstalledContent ) {
        return false;
    }

    InstalledContent content;
    content.DisplayName = displayName;
    content.ActionMask = actionMask;
    installedContent.push_back( content );

    return true;
}

eContentDescResult ModInstaller::checkContentFromMem() {
    // FUN_008a39b0
    reader.resetPos();
    return reader.findElem( "CONTENTDESC" ) ? CDR_Valid : CDR_Invalid;
}

eContentDescResult ModInstaller::loadFromMem( const uint32_t licenceFlags,
                                              const EnumerateContextItem &item,
                                              const bool bApplyContent ) {
    // FUN_008a3ee0
    char pLanguage[32] = {};
    GetActiveLanguage( pLanguage, sizeof( pLanguage ) );

    reader.resetPos();
    if ( !reader.findElem( "CONTENTDESC" ) ) {
        return CDR_Invalid;
    }

    pDescription[0] = '\0';

    uint32_t actionMask = 0;
    eContentDescResult result = CDR_NoVehicle;

    OTDU_LOG_INFO(
        "=====In CXmlInstaller::LoadFromMem(licence_flags_in=%u)=======\n",
        licenceFlags );

    reader.intoElem();
    while ( reader.findElem( "ACTION" ) ) {
        const char *pActionName = reader.getAttrib( "name" );
        const char *pEnvName = reader.getAttrib( "envname" );
        const char *pDatabaseIdText = reader.getAttrib( "database_id" );
        const char *pLicenceFlagsText = reader.getAttrib( "licenceflags" );
        const char *pMoneyText = reader.getAttrib( "mon" );
        const char *pTable = reader.getAttrib( "table" );
        const char *pHashcodeText = reader.getAttrib( "id" );
        const char *pActionBody = reader.getData();

        const bool bHasName =
            ( pActionName != nullptr && *pActionName != '\0' );
        const bool bHasEnvName = ( pEnvName != nullptr && *pEnvName != '\0' );
        const bool bHasLicenceFlags =
            ( pLicenceFlagsText != nullptr && *pLicenceFlagsText != '\0' );

        const uint32_t databaseId = ( pDatabaseIdText != nullptr )
                                        ? (uint32_t)atoi( pDatabaseIdText )
                                        : 0;
        const uint32_t actionLicenceFlags =
            bHasLicenceFlags ? (uint32_t)atoi( pLicenceFlagsText ) : 0;
        const int32_t moneyReward =
            ( pMoneyText != nullptr ) ? atoi( pMoneyText ) : 0;

        OTDU_LOG_INFO( " found ACTION %s , envname=%s database_id=%u "
                       "licenceflags(%s=%u)  mon=%u\n",
                       bHasName ? pActionName : "NULL",
                       bHasEnvName ? pEnvName : "NULL", databaseId,
                       bHasLicenceFlags ? "yes" : "no", actionLicenceFlags,
                       moneyReward );

        if ( !bHasName ||
             ( bHasLicenceFlags &&
               ( actionLicenceFlags & licenceFlags ) != actionLicenceFlags ) ) {
            OTDU_LOG_INFO( "=>Action Skipped\n" );
            continue;
        }

        reader.intoElem();

        if ( StringCmpI( pActionName, "ADDVEHICLE" ) == 0 ) {
            onAddVehicle( pLanguage, databaseId,
                          bHasEnvName ? pEnvName : nullptr );
            result = CDR_Valid;

            // UNLOCKVEHICLE and ADDVEHICLE are mutually exclusive in the mask.
            if ( ( actionMask & CA_UnlockVehicle ) == 0 ) {
                actionMask |= CA_AddVehicle;
            }
        } else if ( StringCmpI( pActionName, "UNLOCKVEHICLE" ) == 0 ) {
            onUnlockVehicle( pLanguage, databaseId,
                             bHasEnvName ? pEnvName : nullptr );
            actionMask = ( actionMask & ~CA_AddVehicle ) | CA_UnlockVehicle;
        } else if ( StringCmpI( pActionName, "POSTNEWS" ) == 0 ) {
            if ( reader.findElem( pLanguage ) || reader.findElem( "us" ) ) {
                const char *pNews = reader.getData();
                if ( bApplyContent && bHasEnvName && pNews != nullptr &&
                     *pNews != '\0' ) {
                    onPostNews( pNews, moneyReward );
                }
            }
        } else if ( StringCmpI( pActionName, "DESC" ) == 0 ) {
            if ( reader.findElem( pLanguage ) || reader.findElem( "us" ) ) {
                const char *pDesc = reader.getData();
                if ( pDesc != nullptr && *pDesc != '\0' ) {
                    strncpy( pDescription, pDesc,
                             kMaxContentDescriptionLength - 1 );
                    pDescription[kMaxContentDescriptionLength - 1] = '\0';
                    OTDU_LOG_INFO( "OnDesc(%s)\n", pDescription );
                }
            }
        } else if ( StringCmpI( pActionName, "ADDRADIOSTATION" ) == 0 ) {
            onAddRadioStation( bHasEnvName ? pEnvName : nullptr, item );
            actionMask |= CA_AddRadioStation;
        } else if ( StringCmpI( pActionName, "ADDDBSTRING" ) == 0 ) {
            onAddDBString( item, pTable, pHashcodeText, pActionBody );
        } else if ( StringCmpI( pActionName, "ADDDBROW" ) == 0 ) {
            onAddDBRow( item, pTable, pActionBody );
        } else {
            OTDU_LOG_WARN( "Unknown ACTION '%s' in %s\n", pActionName,
                           kContentDescFilename );
        }

        reader.outOfElem();
    }
    reader.outOfElem();

    OTDU_LOG_INFO(
        "==============================================================\n" );
    addInstalledContent( item.DisplayName, actionMask );

    return result;
}

bool ModInstaller::HasContentDesc( const EnumerateContextItem &item ) {
    std::error_code errorCode;
    std::filesystem::path descPath( item.Filename );
    descPath /= kContentDescFilename;

    return std::filesystem::is_regular_file( descPath, errorCode );
}

eContentDescResult
ModInstaller::loadContentDesc( const EnumerateContextItem &item,
                               const bool bApplyContent ) {
    // FUN_008a48e0
    contentDescBuffer.clear();
    contentDescSize = 0;

    std::filesystem::path descPath( item.Filename );
    descPath /= kContentDescFilename;

    FileDirectAccess file;
    file.bNeedName = false;

    if ( !file.openFile( descPath.string().c_str(), true, false ) ) {
        return CDR_Invalid;
    }

    const uint32_t fileSize = file.getFileSize();
    if ( fileSize == 0 || fileSize >= kMaxContentDescSize ) {
        OTDU_LOG_ERROR( "'%s' is %u bytes; the limit is %u\n",
                        descPath.string().c_str(), fileSize,
                        kMaxContentDescSize );
        file.close();
        return CDR_Invalid;
    }

    contentDescBuffer.resize( fileSize );
    const uint32_t numBytesRead =
        file.readFromFile( contentDescBuffer.data(), fileSize );
    file.close();

    if ( numBytesRead != fileSize ) {
        OTDU_LOG_ERROR( "Failed to read '%s'\n", descPath.string().c_str() );
        contentDescBuffer.clear();
        return CDR_Invalid;
    }

    contentDescSize = fileSize;

    if ( !reader.parse( contentDescBuffer.data(), contentDescSize ) ) {
        OTDU_LOG_ERROR( "Failed to parse '%s'\n", descPath.string().c_str() );
        return CDR_Invalid;
    }

    if ( checkContentFromMem() == CDR_Invalid ) {
        OTDU_LOG_ERROR( "'%s' has no <CONTENTDESC> root\n",
                        descPath.string().c_str() );
        return CDR_Invalid;
    }

    return loadFromMem( 0, item, bApplyContent );
}
