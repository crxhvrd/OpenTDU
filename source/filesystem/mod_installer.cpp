#include "shared.h"
#include "mod_installer.h"

#include "database/gs_database.h"
#include "file_direct_access.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_mailbox.h"
#include "player_data/saved_news.h"
#include "player_data/saved_string_table.h"

static ModInstaller gModInstaller = {};
ModInstaller *gpModInstaller = &gModInstaller; // DAT_0147aef0

const std::string &ModDBString::resolve( const char *pLanguage ) const {
    const ModDBStringTranslation *pFallback = nullptr;
    for ( const ModDBStringTranslation &translation : Translations ) {
        if ( pLanguage != nullptr &&
             StringNCmpI( translation.Language, pLanguage, 2 ) == 0 ) {
            return translation.Value;
        }
        if ( StringNCmpI( translation.Language, "us", 2 ) == 0 ) {
            pFallback = &translation;
        }
    }

    return ( pFallback != nullptr ) ? pFallback->Value : Value;
}

static bool IsValidDBStringValue( const std::string &value ) {
    return value.find_first_of( "{}\r\n" ) == std::string::npos;
}

ModInstaller::ModInstaller() : contentDescSize( 0 ), reader( nullptr, true ) {
    pDescription[0] = '\0';
}

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

void ModInstaller::markInjectionsMandatory( const std::wstring &packName ) {
    for ( ModDBInjection &injection : dbInjections ) {
        if ( injection.PackName == packName ) {
            injection.bMandatory = true;
        }
    }
}

static std::string GetTrimmedData( const CMarkup &reader ) {
    static constexpr const char *kXMLSpaces = " \t\r\n";

    const std::string data = reader.getData();
    const size_t begin = data.find_first_not_of( kXMLSpaces );
    if ( begin == std::string::npos ) {
        return "";
    }
    return data.substr( begin,
                        data.find_last_not_of( kXMLSpaces ) - begin + 1 );
}

static inline const char *NullIfEmpty( const std::string &value ) {
    return value.empty() ? nullptr : value.c_str();
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

void ModInstaller::onPostNews( const char *pEnvName,
                               const char *pText,
                               const int32_t moneyReward ) {
    // FUN_008a3bf0
    OTDU_LOG_INFO( "OnPostNews(text=%s,mon=%d)\n", pText, moneyReward );

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    if ( pEnvName == nullptr || pSave == nullptr || !pSave->isReady() ) {
        return;
    }

    SavedStringTable *pStringTable = pSave->getStringTable();
    const char *pPostedValue = pStringTable->findValue( pEnvName );
    if ( pPostedValue != nullptr ) {
        OTDU_LOG_DEBUG( "News %s already posted", pPostedValue );
        return;
    }
    pStringTable->setValue( pEnvName, "1" );

    const char *pTitle = gpDatabase->getStringByHashcode( 0, 0x34e5752ull );
    if ( pTitle == nullptr ) {
        pTitle = "#MarketPlace Info";
    }

    const time_t nowSeconds = time( nullptr );
    const struct tm *pNow = localtime( &nowSeconds );

    MailMessage message;
    message.Language = GetMailLanguageCode( gpPlayerData->getLanguage() );
    message.Kind = 1;
    message.Category = 1;
    if ( pNow != nullptr ) {
        message.Date = PackSaveDate( uint32_t( pNow->tm_year + 1900 ),
                                     uint32_t( pNow->tm_mon + 1 ),
                                     uint32_t( pNow->tm_mday ) );
    }

    strncpy( message.Title, pTitle, MailMessage::kTitleLength );
    message.Title[MailMessage::kTitleLength - 1u] = '\0';
    strncpy( message.Body, pText, MailMessage::kBodyLength );
    message.Body[0x76f] = '\0';

    pSave->getMailbox()->post( message );

    if ( moneyReward != 0 ) {
        Driver *pDriver = pSave->getDriver();
        pDriver->setMoneyAndNotify( int32_t( pDriver->getMoney() ) +
                                    moneyReward );
    }

    gpPlayerData->requestSave( 2 );
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

void ModInstaller::readDBStringTranslations( ModDBString &string ) {
    while ( reader.findElem( nullptr ) ) {
        const std::string language = reader.getTagName();
        if ( language.size() != 2 ) {
            OTDU_LOG_WARN( "ADDDBSTRING %llu: <%s> is not a language code\n",
                           string.Hashcode, language.c_str() );
            continue;
        }

        ModDBStringTranslation translation;
        translation.Language[0] = (char)tolower( language[0] );
        translation.Language[1] = (char)tolower( language[1] );
        translation.Value = GetTrimmedData( reader );

        if ( !IsValidDBStringValue( translation.Value ) ) {
            OTDU_LOG_ERROR( "ADDDBSTRING %llu <%s> contains a brace or a "
                            "newline\n",
                            string.Hashcode, translation.Language );
            continue;
        }

        string.Translations.push_back( translation );
    }
}

void ModInstaller::onAddDBString( const EnumerateContextItem &item,
                                  const char *pTable,
                                  const char *pHashcode,
                                  const char *pValue ) {
    if ( pTable == nullptr || *pTable == '\0' || pHashcode == nullptr ||
         *pHashcode == '\0' ) {
        OTDU_LOG_WARN( "ADDDBSTRING needs table and id attributes\n" );
        return;
    }

    ModDBString string;
    string.Hashcode = strtoull( pHashcode, nullptr, 10 );
    string.Value = ( pValue != nullptr ) ? pValue : "";

    if ( string.Hashcode == 0 ) {
        OTDU_LOG_ERROR( "ADDDBSTRING id '%s' is not a valid hashcode\n",
                        pHashcode );
        return;
    }

    if ( !IsValidDBStringValue( string.Value ) ) {
        OTDU_LOG_ERROR( "ADDDBSTRING %llu contains a brace or a newline\n",
                        string.Hashcode );
        return;
    }

    readDBStringTranslations( string );

    if ( string.Value.empty() ) {
        if ( string.Translations.empty() ) {
            OTDU_LOG_WARN( "ADDDBSTRING %llu has neither a body nor a "
                           "translation\n",
                           string.Hashcode );
            return;
        }
        string.Value = string.resolve( "us" );
    }

    OTDU_LOG_INFO( "OnAddDBString(table=%s,id=%llu,value=%.64s,"
                   "translations=%zu)\n",
                   pTable, string.Hashcode, string.Value.c_str(),
                   string.Translations.size() );
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
        const std::string actionName = reader.getAttrib( "name" );
        const std::string envName = reader.getAttrib( "envname" );
        const std::string databaseIdText = reader.getAttrib( "database_id" );
        const std::string licenceFlagsText = reader.getAttrib( "licenceflags" );
        const std::string moneyText = reader.getAttrib( "mon" );
        const std::string table = reader.getAttrib( "table" );
        const std::string hashcodeText = reader.getAttrib( "id" );
        const std::string actionBody = GetTrimmedData( reader );

        const char *pActionName = actionName.c_str();
        const char *pEnvName = envName.c_str();
        const char *pTable = NullIfEmpty( table );
        const char *pHashcodeText = NullIfEmpty( hashcodeText );
        const char *pActionBody = actionBody.c_str();

        const bool bHasName = !actionName.empty();
        const bool bHasEnvName = !envName.empty();
        const bool bHasLicenceFlags = !licenceFlagsText.empty();

        const uint32_t databaseId = (uint32_t)atoi( databaseIdText.c_str() );
        const uint32_t actionLicenceFlags =
            bHasLicenceFlags ? (uint32_t)atoi( licenceFlagsText.c_str() ) : 0;
        const int32_t moneyReward = atoi( moneyText.c_str() );

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
                const std::string news = GetTrimmedData( reader );
                if ( bApplyContent && bHasEnvName && !news.empty() ) {
                    onPostNews( pEnvName, news.c_str(), moneyReward );
                }
            }
        } else if ( StringCmpI( pActionName, "DESC" ) == 0 ) {
            if ( reader.findElem( pLanguage ) || reader.findElem( "us" ) ) {
                const std::string desc = GetTrimmedData( reader );
                if ( !desc.empty() ) {
                    strncpy( pDescription, desc.c_str(),
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

    const std::string contentDesc( contentDescBuffer.data(), contentDescSize );
    if ( !reader.setDoc( contentDesc.c_str() ) ) {
        OTDU_LOG_ERROR( "Failed to parse '%s': %s\n", descPath.string().c_str(),
                        reader.getError().c_str() );
        return CDR_Invalid;
    }

    if ( checkContentFromMem() == CDR_Invalid ) {
        OTDU_LOG_ERROR( "'%s' has no <CONTENTDESC> root\n",
                        descPath.string().c_str() );
        return CDR_Invalid;
    }

    return loadFromMem( 0, item, bApplyContent );
}
