#include "shared.h"
#include "saved_mailbox.h"

#include "core/ed_store.h"
#include "core/mutex.h"
#include "database/gs_database.h"
#include "flash/flash_callbacks_register.h"
#include "flash/mng_flash_localize.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/game_mechanism_player.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "gs_playerdata.h"
#include "player_data_interface.h"
#include "render/draw_list.h"
#include "saved_garage.h"
#include "saved_news.h"
#include "world/gs_world.h"
#include "world/mng_sector_name.h"
#include "world/streaming_manager.h"

static constexpr int32_t kNumNewsRanks = 10;
static constexpr size_t kNewsRankNameLength = 0x10;

uint32_t GetMailLanguageCode( const char *pLanguage ) {
    return uint32_t( int32_t( pLanguage[0] ) << 8 | int32_t( pLanguage[1] ) );
}

void MailMessage::clear() {
    // FUN_0088e780
    Kind = 0;
    Category = 0;
    Date = 0u;
    Title[0] = '\0';

    memset( Body, 0, sizeof( Body ) );
    memset( Sender, 0, sizeof( Sender ) );

    Language = 0u;
    Index = -1;
    Hashcode = 0ull;

    Param0 = 0u;
    Param1 = 0u;
    Param2 = 0u;

    memset( Blob0, 0, sizeof( Blob0 ) );
    memset( Blob1, 0, sizeof( Blob1 ) );

    bRead = false;
    bFlagged = false;
}

void MailMessage::copyFrom( const MailMessage &other ) {
    // FUN_00895800 / FUN_008956c0
    clear();

    Language = other.Language;
    if ( Language == 0xffffffffu ) {
        Language = GetMailLanguageCode( gpPlayerData->getLanguage() );
    }

    Kind = other.Kind;
    Category = other.Category;
    Date = other.Date;

    strncpy( Title, other.Title, kTitleLength );
    Title[kTitleLength - 1u] = '\0';

    strncpy( Body, other.Body, kBodyLength );
    Body[0x76f] = '\0';

    memcpy( Sender, other.Sender, kSenderLength );
    Index = other.Index;

    Hashcode = other.Hashcode;

    strcpy( (char *)Blob1, (const char *)other.Blob1 );
    strcpy( (char *)Blob0, (const char *)other.Blob0 );

    Param0 = other.Param0;
    Param1 = other.Param1;
    Param2 = other.Param2;

    bRead = other.bRead;
    bFlagged = other.bFlagged;
}

bool MailMessage::isVisible() const {
    // FUN_00895880
    const uint32_t currentLanguage =
        GetMailLanguageCode( gpPlayerData->getLanguage() );

    switch ( Language ) {
    case 0x6765: // ge
    case 0x6672: // fr
    case 0x6974: // it
    case 0x7370: // sp
    case 0x7573: // us
        return true;
    case 0x6368: // ch
    case 0x6a61: // ja
    case 0x6b6f: // ko
        return Language == currentLanguage;
    default:
        return false;
    }
}

void MailMessage::deserialize( edStore &stream ) {
    // FUN_0088f730
    Kind = stream.readByte();
    Category = stream.readByte();

    stream.readBufferNoAlloc( Title, kTitleLength );
    Title[kTitleLength - 1u] = '\0';

    stream.readBufferNoAlloc( Body, kBodyLength );
    Body[kBodyLength - 1u] = '\0';

    stream.readBufferNoAlloc( Sender, kSenderLength );
    Sender[kSenderLength - 1u] = '\0';

    Index = stream.readInteger();
    Hashcode = stream.readULongLong();
    Date = stream.readUShort();

    Param0 = stream.readULong();
    Param1 = stream.readULong();
    Param2 = stream.readULong();

    stream.readBufferNoAlloc( Blob0, sizeof( Blob0 ) );
    stream.readBufferNoAlloc( Blob1, sizeof( Blob1 ) );

    bRead = stream.readByte() == 1u;
    bFlagged = stream.readByte() == 1u;

    Language = stream.readULong();

    stream.readInteger();
}

void MailMessage::serialize( edStore &stream ) const {
    // FUN_0088f620
    stream.writeByte( uint8_t( Kind ) );
    stream.writeByte( uint8_t( Category ) );

    stream.writeBuffer( Title, kTitleLength );
    stream.writeBuffer( Body, kBodyLength );
    stream.writeBuffer( Sender, kSenderLength );

    stream.writeInteger( Index );
    stream.writeULongLong( Hashcode );
    stream.writeUShort( Date );

    stream.writeInteger( int32_t( Param0 ) );
    stream.writeInteger( int32_t( Param1 ) );
    stream.writeInteger( int32_t( Param2 ) );

    stream.writeBuffer( Blob0, sizeof( Blob0 ) );
    stream.writeBuffer( Blob1, sizeof( Blob1 ) );

    stream.writeByte( bRead ? 1u : 0u );
    stream.writeByte( bFlagged ? 1u : 0u );

    stream.writeInteger( int32_t( Language ) );

    // A constant (EOF?)
    stream.writeInteger( 0xc103 );
}

static void FormatNewsDate( char *pDest,
                            const size_t destLength,
                            const uint16_t packedDate ) {
    // FUN_007190a0
    int32_t year = 0;
    int32_t month = 0;
    int32_t day = 0;
    UnpackSaveDate( packedDate, year, month, day );
    FormatSaveDate( pDest, destLength, year, month, day );
}

static inline size_t GetUTF8LeadLength( const uint8_t leadByte ) {
    if ( ( leadByte & 0x80u ) == 0u ) {
        return 1;
    }
    if ( ( leadByte & 0xe0u ) == 0xc0u ) {
        return 2;
    }
    return ( ( leadByte & 0xf0u ) == 0xe0u ) ? 3 : 1;
}

static inline SpotMap *FindNewsSpot( const uint64_t hashcode ) {
    Spots &spots = gpScenaric->getSpotList();
    return spots.getWorldSpotChecked( spots.getSpotMapIndex( hashcode ) );
}

static int32_t GetNewsSpotDisplayName( const SpotMap *pSpot ) {
    switch ( pSpot->Type ) {
    case ST_House: {
        Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
        return pGarage->isHouseOwned( pSpot->Hashcode )
                   ? PlayerGameMech::SDN_Home
                   : PlayerGameMech::SDN_ForSale;
    }
    case ST_CarDealer:
        return PlayerGameMech::SDN_CarDealer;
    case ST_Goodies:
        return PlayerGameMech::SDN_Goodies;
    case ST_AfterMarket:
        return PlayerGameMech::SDN_Tuning;
    case ST_Club:
        return PlayerGameMech::SDN_Club;
    case ST_DriveIn:
        return PlayerGameMech::SDN_DriveIn;
    case ST_Challenge:
        return -1;
    case ST_Unknown8:
        switch ( pSpot->SubType ) {
        case 0:
            return PlayerGameMech::SDN_CarWash;
        case 1:
            return PlayerGameMech::SDN_CarPaint;
        case 2:
        case 3:
            return PlayerGameMech::SDN_CarRental;
        case 4:
            return PlayerGameMech::SDN_Estates;
        case 9:
            return PlayerGameMech::SDN_CarPaintDeluxe;
        default:
            return -1;
        }
    default:
        return PlayerGameMech::SDN_BikeDealer;
    }
}

void MailMessage::fillNewsLayout( FlashPlayer *pMovie ) const {
    // FUN_00893df0
    if ( pMovie == nullptr ) {
        return;
    }

    pMovie->setVariableValue( "/:news_layout", Category );

    uint64_t headingHashcode = 0ull;
    switch ( Kind ) {
    case 1:
        headingHashcode = 0x3c05302ull;
        break;
    case 2:
        headingHashcode = 0x37fa082ull;
        break;
    case 4:
        headingHashcode = 0x379d422ull;
        break;
    case 6:
        headingHashcode = 0x3488af2ull;
        break;
    default:
        break;
    }

    if ( headingHashcode != 0ull ) {
        pMovie->setVariableValue(
            "/:NewsHeading",
            gpDatabase->getStringByHashcode( 0, headingHashcode ) );
    } else {
        pMovie->setVariableValueNonLinked( "/:NewsHeading", "ERR:NEWS:1" );
    }

    char dateString[0x20];
    FormatNewsDate( dateString, sizeof( dateString ), Date );
    pMovie->setVariableValueNonLinked( "/:NewsDate", dateString );

    pMovie->setVariableValue( "/:NewsTitle", Title );

    const size_t initialLength = GetUTF8LeadLength( uint8_t( Body[0] ) );
    char initial[8];
    strncpy( initial, Body, initialLength );
    initial[initialLength] = '\0';
    pMovie->setVariableValueNonLinked( "/:NewsInitial", initial );
    pMovie->setVariableValue( "/:NewsText", Body + initialLength );

    if ( Category == 3 ) {
        for ( int32_t i = 0; i < kNumNewsRanks; i++ ) {
            char path[0x8c];
            snprintf( path, sizeof( path ),
                      "ss_menu/news/news_layout/ranks/%d/:name", i + 1 );
            pMovie->setVariableValue( path, Sender + i * kNewsRankNameLength );

            snprintf( path, sizeof( path ), "ss_menu/news/news_layout/ranks/%d",
                      i + 1 );
            pMovie->showFrame( ( Index == i ) ? "up" : "default", path, false );
        }
    }

    if ( Category == 4 ) {
        SpotMap *pSpot = FindNewsSpot( Hashcode );
        if ( pSpot != nullptr ) {
            const char *pDistrict = "Ohau";
            if ( gpMngSectorName != nullptr ) {
                pDistrict = gpMngSectorName->getSectorName(
                    Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f ) );
            }

            char address[0x20];
            strncpy( address, pDistrict, sizeof( address ) - 1 );
            address[sizeof( address ) - 1] = '\0';
            pMovie->setVariableValueNonLinked( "/:Adress", address );

            const int32_t displayName = GetNewsSpotDisplayName( pSpot );
            if ( displayName != -1 ) {
                pMovie->setVariableValueNonLinked(
                    "/:WhatSpot", GetSpotDisplayName( displayName ) );
            }
        }
    }

    if ( Category == 6 ) {
        int32_t clubMembers = 0;
        int32_t clubReputation = 0;
        int32_t clubLevel = 0;
        sscanf( Sender, "%d/%d/%d", &clubMembers, &clubReputation, &clubLevel );
        pMovie->setVariableValue( "/:ClubMembers", clubMembers );
        pMovie->setVariableValue( "/:ClubRep", clubReputation );
        pMovie->setVariableValue( "/:ClubLevel", clubLevel );
        pMovie->setVariableValueNonLinked(
            "/:WhatSpot", GetSpotDisplayName( PlayerGameMech::SDN_Club ) );
        pMovie->setVariableValueNonLinked( "/:ClubDescription",
                                           (const char *)Blob1 );
        pMovie->setVariableValueNonLinked( "/:ClubName", (const char *)Blob0 );
        pMovie->setVariableValue( "/:Answered", bFlagged ? 1 : 0 );
    }

    if ( Category == 2 || Category == 3 || Category == 5 ) {
        gFlashCallbacksRegister.removeAllSynonyms(
            &MailMessage::PictureDrawCallback );
    }
}

void MailMessage::locateOnMap( FlashPlayer *pMovie ) const {
    // FUN_008951d0
    SpotMap *pSpot = FindNewsSpot( Hashcode );

    float worldWidth = -1.0f;
    float worldHeight = -1.0f;
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming != nullptr ) {
        worldWidth =
            float( int32_t( pStreaming->areaGrid.numAreas *
                            pStreaming->sectorManager.numSectors * 0x300 ) );
        worldHeight =
            float( int32_t( pStreaming->areaGrid.numSubAreas *
                            pStreaming->sectorManager.numSubSectors * 0x300 ) );
    }

    if ( pSpot == nullptr || worldWidth <= 0.0f || worldHeight <= 0.0f ) {
        return;
    }

    FlashClip mapClip;
    if ( !pMovie->getClipPlayer(
             "ss_menu/news/news_layout/spot_datas/location/pict", &mapClip ) ) {
        return;
    }

    FlashRECT mapBounds;
    mapClip.getPropertyViewport( eFlashProperty::FP_BoundingBox, mapBounds );

    FlashClip markerClip;
    if ( !pMovie->getClipPlayer(
             "ss_menu/news/news_layout/spot_datas/location/circle",
             &markerClip ) ) {
        return;
    }

    markerClip.setProperty( eFlashProperty::FP_X,
                            ( pSpot->X / worldWidth ) * mapBounds.Width );
    markerClip.setProperty( eFlashProperty::FP_Y,
                            ( pSpot->Z / worldHeight ) * mapBounds.Height );
}

void MailMessage::PictureDrawCallback( FlashRECT * /*pViewport*/,
                                       float /*depth*/,
                                       DrawList *pDrawList,
                                       void *pUserData,
                                       FlashResource *pResource,
                                       const char * /*pClipName*/ ) {
    // LAB_00893dd0 / FUN_00893d10
    const MailMessage *pMessage = static_cast<const MailMessage *>( pUserData );
    if ( pMessage == nullptr || pMessage->pPicture == nullptr ||
         pMessage->pPictureMovie == nullptr ) {
        return;
    }

    FlashClip pictureClip;
    if ( !pMessage->pPictureMovie->getClipPlayer(
             "ss_menu/news/news_layout/pict", &pictureClip ) ) {
        return;
    }

    FlashRECT viewport;
    pictureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );

    pDrawList->setActiveMaterial( pMessage->pPicture );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}

SavedMailbox::SavedMailbox() : Notifiable(), Messages() {
    // FUN_004160c0
    clear();
}

SavedMailbox::~SavedMailbox() {}

void SavedMailbox::clear() {
    // FUN_004160c0
    for ( MailMessage &message : Messages ) {
        message.pPictureMovie = nullptr;
        message.clear();
    }
}

void SavedMailbox::serialize( edStore &stream ) const {
    // FUN_00890d50
    for ( const MailMessage &message : Messages ) {
        message.serialize( stream );
    }
}

void SavedMailbox::deserialize( edStore &stream ) {
    // FUN_00890d90
    for ( MailMessage &message : Messages ) {
        message.deserialize( stream );
    }

    notify( 0u );
}

int32_t SavedMailbox::getNumVisibleMessages() const {
    // FUN_00895920
    int32_t numMessages = 0;
    for ( const MailMessage &message : Messages ) {
        if ( message.Category == 0 || !message.isVisible() ) {
            break;
        }
        numMessages++;
    }

    return numMessages;
}

MailMessage *SavedMailbox::getVisibleMessage( int32_t index ) {
    // FUN_00895980
    for ( MailMessage &message : Messages ) {
        if ( message.Category == 0 || !message.isVisible() ) {
            continue;
        }

        if ( index == 0 ) {
            return &message;
        }
        index--;
    }

    return &Messages[0];
}

void SavedMailbox::removeVisibleMessage( const int32_t index ) {
    // FUN_00895a70
    for ( int32_t i = getFreeSlotIndex( index ); i < kNumMessages - 1; i++ ) {
        Messages[i] = Messages[i + 1];
    }

    Messages[kNumMessages - 1].clear();

    notify( 0u );
}

void SavedMailbox::detachPictures() {
    // FUN_00895190
    gFlashCallbacksRegister.removeAllSynonyms(
        &MailMessage::PictureDrawCallback );

    for ( MailMessage &message : Messages ) {
        message.pPictureMovie = nullptr;
    }
}

int32_t SavedMailbox::getFreeSlotIndex( int32_t numToSkip ) const {
    // FUN_00895a00
    for ( int32_t i = 0; i < kNumMessages; i++ ) {
        const MailMessage &message = Messages[i];
        if ( message.Category == 0 || message.isVisible() ) {
            if ( numToSkip == 0 ) {
                return i;
            }
            numToSkip--;
        }
    }

    return 0;
}

void SavedMailbox::post( const MailMessage &message ) {
    // FUN_00895af0
    {
        ScopedMutexLock lock( gpPlayerData->getMutex() );

        MailMessage *pSlot = nullptr;
        const int32_t numMessages = getNumVisibleMessages();
        if ( numMessages < kNumMessages ) {
            int32_t slotIndex = getFreeSlotIndex( numMessages );
            if ( slotIndex < 0 || slotIndex >= kNumMessages ) {
                slotIndex = 0;
            }
            pSlot = &Messages[slotIndex];
        } else {
            for ( int32_t i = 0; i < kNumMessages - 1; i++ ) {
                Messages[i] = Messages[i + 1];
            }
            pSlot = &Messages[kNumMessages - 1];
        }

        pSlot->copyFrom( message );
    }

    gpScenaric->setMailboxUpdated( true );

    notify( 0u );
}
