#include "shared.h"
#include "mng_sector_name.h"

#include "world/gs_world.h"
#include "world/streaming_manager.h"
#include "world/sector_manager.h"
#include "world/gs_mng_resources.h"

#include "flash/mng_fade.h"
#include "render/gs_render.h"
#include "render/mng_font_players.h"

MngSectorName *gpMngSectorName = nullptr; // DAT_010e8000

static const char *pDefaultSectorName = "Ohau"; // DAT_00f9c20c

static bool gbSectorTablesUpgraded = false; // BOOL_010e8004
static const char **gpSectorNameTable = nullptr;
static const uint32_t **gpSectorAreaTable = nullptr;

static constexpr float kFadeInDelay = 0.5f;         // FLOAT_00f41674
static constexpr float kFadeRate = 2.0f;            // DAT_00f416b0
static constexpr float kMaxAlpha = 255.0f;          // DAT_00f41688
static constexpr float kFadeOutDuration = 3.0f;     // DAT_00f416b4
static constexpr float kHoldDuration = 1.5f;        // DAT_00f416b8
static constexpr float kNameRightEdge = 0.9453125f; // DAT_00f41b40
static constexpr float kNameTop = 0.16145833f;      // DAT_00f41b44

MngSectorName::MngSectorName()
    : Manager(),
      numSectors( 0 ),
      pSectors( nullptr ),
      pAreas( nullptr ),
      numAreaCols( 0 ),
      numAreaRows( 0 ),
      numSectorColRow( 0 ),
      pCurrentSector( nullptr ),
      state( SNS_Inactive ),
      fadeTime( 0.0f ),
      startTime( 0.0f ) {
    // FUN_009d8ed9 (inlined)
    bEnabled = true;
    numSectors = 0;
    pSectors = nullptr;
    pAreas = nullptr;
    pCurrentSector = nullptr;

    // The original only registers the first instance, and only that instance
    // gets its display string initialized.
    if ( gpMngSectorName == nullptr ) {
        gpMngSectorName = this;
        displayName[0] = '<';
        displayName[1] = '\0';
    }
}

MngSectorName::~MngSectorName() {
    // FUN_0042b520 (inlined)
    gpMngSectorName = nullptr;
}

bool MngSectorName::initialize( TestDriveGameInstance * ) {
    // FUN_006c4740
    StreamingManager *pStreaming = gpWorld->getStreamingManager();

    const uint8_t *pBlob =
        static_cast<const uint8_t *>( pStreaming->getSectorNameStream() );
    if ( pBlob == nullptr ) {
        pSectors = nullptr;
        numSectors = 0;
        return true;
    }

    numAreaCols =
        static_cast<int32_t>( pStreaming->areaGrid.numAreas ); // +0x76fd4
    numAreaRows =
        static_cast<int32_t>( pStreaming->areaGrid.numSubAreas ); // +0x76fd0
    numSectorColRow =
        static_cast<int32_t>( pStreaming->areaGrid.numSectors ); // +0x76fdc

    numSectors = *reinterpret_cast<const uint32_t *>( pBlob + 0x20 );

    // Blob layout: Sector name offsets at +0x30, then the per-area-cell
    // offsets (the name table is rounded up to a multiple of 4 entries).
    const uint32_t *pNameOffsets =
        reinterpret_cast<const uint32_t *>( pBlob + 0x30 );
    uint32_t areaTableStart = numSectors;
    if ( ( numSectors & 3 ) != 0 ) {
        areaTableStart += 4 - ( numSectors & 3 );
    }
    const uint32_t *pAreaOffsets = pNameOffsets + areaTableStart;

    if ( !gbSectorTablesUpgraded ) {
        gpSectorNameTable = new const char *[numSectors];
        for ( uint32_t i = 0; i < numSectors; i++ ) {
            gpSectorNameTable[i] =
                reinterpret_cast<const char *>( pBlob + pNameOffsets[i] );
        }

        const uint32_t numAreaCells =
            static_cast<uint32_t>( numAreaRows * numAreaCols );
        gpSectorAreaTable = new const uint32_t *[numAreaCells];
        for ( uint32_t i = 0; i < numAreaCells; i++ ) {
            gpSectorAreaTable[i] =
                reinterpret_cast<const uint32_t *>( pBlob + pAreaOffsets[i] );
        }

        gbSectorTablesUpgraded = true;
    }

    pSectors = gpSectorNameTable;
    pAreas = gpSectorAreaTable;
    return true;
}

void MngSectorName::reset() {
    // FUN_006c4720
    pCurrentSector = nullptr;
    state = SNS_Inactive;
    startTime = 2.0f; // DAT_00f416b0
}

const char *
MngSectorName::getSectorName( const Eigen::Vector4f &worldPosition ) const {
    // FUN_006c4c00
    if ( pAreas == nullptr || pSectors == nullptr ) {
        return pDefaultSectorName;
    }

    GridPos2D sectorCell;
    GridPos2D areaCell;
    gpWorld->getStreamingManager()->sectorManager.computeCell(
        worldPosition, sectorCell, areaCell );
    return getSectorNameAtCell( areaCell, sectorCell );
}

const char *
MngSectorName::getSectorNameAtCell( const GridPos2D &areaCell,
                                    const GridPos2D &sectorCell ) const {
    // FUN_006c46a0
    const uint32_t sectorNameIndex =
        pAreas[numAreaCols * areaCell.posY + areaCell.posX]
              [numSectorColRow * sectorCell.posY + sectorCell.posX];
    if ( sectorNameIndex < numSectors ) {
        return pSectors[sectorNameIndex];
    }
    return pDefaultSectorName;
}

void MngSectorName::drawNames( float deltaTime ) {
    // FUN_006c4ac0
    if ( !bEnabled ) {
        return;
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    const Area *pArea = pStreaming->areaGrid.pCurrentArea;
    const Sector *pSector = pStreaming->sectorManager.pCurrentSector;
    if ( pArea == nullptr || pSector == nullptr || pSectors == nullptr ) {
        return;
    }

    const char *pSectorName =
        getSectorNameAtCell( pArea->coords, pSector->installedSector );
    if ( pSectorName != pCurrentSector && state == SNS_Inactive &&
         gpMngFade != nullptr &&
         ( gpMngFade->getTransitionFlags() & 1 ) == 0 ) {
        pCurrentSector = pSectorName;
        if ( pSectorName == nullptr ) {
            displayName[0] = '\0';
        } else {
            displayName[0] = '<';
            strncpy( &displayName[1], pSectorName, sizeof( displayName ) - 2 );
        }
        fadeTime = startTime + deltaTime + kFadeInDelay;
        state = SNS_FadeIn;
        startTime = 0.0f;
    }

    switch ( state ) {
    case SNS_FadeIn:
        tickFadeIn( deltaTime );
        break;
    case SNS_FadeOut:
        tickFadeOut( deltaTime );
        break;
    case SNS_WaitFadeOut:
        tickWaitFadeOut( deltaTime );
        break;
    default:
        break;
    }
}

void MngSectorName::drawDisplayName( uint8_t alpha ) {
    gpMngResources->getFontResources().selectFont( "TDU_BIG" );

    const float y = (float)gpRender->getHeight() * kNameTop;
    const float x = (float)gpRender->getWidth() * kNameRightEdge -
                    gpMngFontPlayers->getTextWidth( 1.0f, displayName );
    gpMngFontPlayers->drawText( x, y, 1.0f, 0xff, 0xff, 0xff, alpha, "%s",
                                displayName );
}

void MngSectorName::tickFadeIn( float deltaTime ) {
    // FUN_006c49d0
    fadeTime -= deltaTime;
    if ( gpMngFontPlayers != nullptr && pCurrentSector != nullptr ) {
        int32_t alpha =
            0xff - static_cast<int32_t>( fadeTime * kFadeRate * kMaxAlpha );
        if ( alpha > 0xff ) {
            alpha = 0xff;
        }
        if ( alpha > 0 ) {
            drawDisplayName( static_cast<uint8_t>( alpha ) );
        }
    }

    if ( fadeTime <= 0.0f ) {
        fadeTime = kHoldDuration;
        state = SNS_WaitFadeOut;
    }
}

void MngSectorName::tickFadeOut( float deltaTime ) {
    // FUN_006c4820
    fadeTime -= deltaTime;
    if ( fadeTime <= 0.0f ) {
        fadeTime = 0.0f;
        state = SNS_Inactive;
    }

    if ( gpMngFontPlayers != nullptr && pCurrentSector != nullptr ) {
        int32_t alpha =
            static_cast<int32_t>( fadeTime * kFadeRate * kMaxAlpha );
        if ( alpha > 0xff ) {
            alpha = 0xff;
        }
        if ( alpha > 0 ) {
            drawDisplayName( static_cast<uint8_t>( alpha ) );
        }
    }
}

void MngSectorName::tickWaitFadeOut( float deltaTime ) {
    // FUN_006c4910
    fadeTime -= deltaTime;
    if ( fadeTime <= 0.0f ) {
        fadeTime = kFadeOutDuration;
        state = SNS_FadeOut;
    }

    if ( gpMngFontPlayers != nullptr && pCurrentSector != nullptr ) {
        drawDisplayName( 0xff );
    }
}
