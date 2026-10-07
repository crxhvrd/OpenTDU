#include "shared.h"
#include "mng_mods_ui.h"

#include "core/arg_parser.h"
#include "database/gs_database.h"
#include "database/port_strings.h"
#include "filesystem/gs_file.h"
#include "filesystem/mod_installer.h"
#include "filesystem/mod_manager.h"
#include "flash/bank/flash_bank_resources.h"
#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "game/gm_base.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_display_list.h"
#include "flash/implementation/flash_resource.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/implementation/flash_tags.h"
#include "flash/modal_message_box.h"
#include "player_data/gs_playerdata.h"

static MngModsUI gModsUI = {};
MngModsUI *gpModsUI = &gModsUI;

static constexpr int32_t kAutoOpenTicks = 3;

static constexpr const char *kMovieName = "VIEWER";
static constexpr const char *kPackListPath = "/array_bnk";
static constexpr const char *kDetailListPath = "/array_cine";
static constexpr const char *kHighlightClipName = "barre";
static constexpr const char *kPackHighlightPath = "/array_bnk/barre";

static constexpr const char *kPackPaneFrame = "bnk_clic";
static constexpr const char *kDetailPaneFrame = "cine_clic";

static int32_t gPackRestFrame = -1;
static int32_t gPaneRestFrame = -1;
static float gPackPaneRestX = 0.0f;
static float gDetailPaneRestX = 0.0f;
static float gPackHighlightX = 0.0f;
static float gPackHighlightScaleX = 0.0f;

static constexpr FlashMessage kPackListWidget = FlashMessage( "bnkl" );
static constexpr FlashMessage kDetailListWidget = FlashMessage( "cinl" );

static constexpr FlashMessage kWidget = FlashMessage( "widg" );

static constexpr const char *kMoviesToMute[] = { "LOG_IN", "CONFIGPC",
                                                 "GENERAL" };

static constexpr int8_t kModsUIFlagBit = 2;

static constexpr const char *kClipsToHide[] = { "/remote", "/time" };

static constexpr const char *kOptionsMovieName = "CONFIGPC";
static constexpr const char *kFrontEndScreenClips[] = { "/menus", "/profile",
                                                        "/help" };

static constexpr const char *kTabBarClip = "/barre";
static constexpr const char *kTabBarCursorClip = "/barre/cur";

static constexpr const char *kTabBarKeyLabel = "X";
static constexpr const char *kTabBarNameLabel = "Mod Manager";

static constexpr ColorRGBA kTabBarKeyColor( 0xf0, 0xf0, 0xf0 );
static constexpr FlashMessage kPackChosen = FlashMessage( "bnch" );
static constexpr FlashMessage kDetailChosen = FlashMessage( "cich" );

static uint16_t GetGlyphWidth( const FlashTagDefineFontData *pFont,
                               const uint8_t glyphIndex ) {
    static constexpr uint32_t kWidthOffsetInRecord = 4u;

    if ( pFont == nullptr ||
         glyphIndex >= (uint8_t)( sizeof( pFont->GlyphesASCII ) /
                                  sizeof( pFont->GlyphesASCII[0] ) ) ) {
        return 0u;
    }

    const uint32_t recordOffset = pFont->GlyphesASCII[glyphIndex].Unknown;
    if ( recordOffset == 0u ||
         recordOffset + kWidthOffsetInRecord + sizeof( uint16_t ) >
             pFont->TagSize ) {
        return 0u;
    }

    return *(const uint16_t *)( (const uint8_t *)pFont + recordOffset +
                                kWidthOffsetInRecord );
}

static const FlashTagDefineFontData *
FindFontTag( FlashResourceHeader *pHeader ) {
    static constexpr uint32_t kDefineFontDataTagId = 10u;

    for ( const FlashTag *pTag =
              (const FlashTag *)( (uint8_t *)pHeader + 0x48 );
          pTag != nullptr; pTag = pTag->getNextTag() ) {
        if ( pTag->TagId == kDefineFontDataTagId &&
             pTag->TagSize >= sizeof( FlashTagDefineFontData ) ) {
            return (const FlashTagDefineFontData *)pTag;
        }
    }

    return nullptr;
}

static const char *GetPlaceObjectName( const FlashTagPlaceObject2Data *pData ) {
    static constexpr uint8_t kHasCharacter = 0x02u;
    static constexpr uint8_t kHasMatrix = 0x04u;
    static constexpr uint8_t kHasColorTransform = 0x08u;
    static constexpr uint8_t kHasRatio = 0x10u;
    static constexpr uint8_t kHasName = 0x20u;
    static constexpr uint8_t kHasClipDepth = 0x40u;
    static constexpr uint8_t kHasClipActions = 0x80u;

    const uint8_t lowFlags = pData->LowFlags;
    if ( ( lowFlags & kHasName ) == 0 ) {
        return nullptr;
    }

    auto alignToDword = []( uint32_t offset ) { return ( offset + 3u ) & ~3u; };

    uint32_t offset = ( lowFlags & kHasCharacter ) ? 0xeu : 0xcu;
    if ( ( lowFlags & kHasMatrix ) != 0 ) {
        offset = alignToDword( offset );
        if ( ( pData->Flags & 0x100 ) != 0 ) {
            offset += 8u;
        }
        if ( ( lowFlags & kHasClipActions ) != 0 ) {
            offset += 8u;
        }
        if ( ( pData->Flags & 0x200 ) != 0 ) {
            offset += 8u;
        }
    }
    if ( ( lowFlags & kHasColorTransform ) != 0 ) {
        offset = alignToDword( offset ) + 16u;
    }
    if ( ( lowFlags & kHasRatio ) != 0 ) {
        offset += 2u;
    }
    if ( ( lowFlags & kHasClipDepth ) != 0 ) {
        offset += 2u;
    }

    return (const char *)( (const uint8_t *)pData + offset );
}

static bool GetPlaceObjectTranslation( const FlashTagPlaceObject2Data *pData,
                                       float *pOutX,
                                       float *pOutY ) {
    static constexpr uint8_t kHasCharacter = 0x02u;
    static constexpr uint8_t kHasMatrix = 0x04u;
    static constexpr uint8_t kHasClipActions = 0x80u;

    const uint8_t lowFlags = pData->LowFlags;
    if ( ( lowFlags & kHasMatrix ) == 0 || ( pData->Flags & 0x200 ) == 0 ) {
        return false;
    }

    uint32_t offset = ( lowFlags & kHasCharacter ) ? 0xeu : 0xcu;
    offset = ( offset + 3u ) & ~3u;
    if ( ( pData->Flags & 0x100 ) != 0 ) {
        offset += 8u;
    }
    if ( ( lowFlags & kHasClipActions ) != 0 ) {
        offset += 8u;
    }

    const float *pTranslation =
        (const float *)( (const uint8_t *)pData + offset );
    *pOutX = pTranslation[0];
    *pOutY = pTranslation[1];

    return true;
}

static FlashTag *FindTagForCharacter( FlashTag *pFirstTag,
                                      const uint32_t character ) {
    static constexpr uint32_t kEndTagId = 0u;
    static constexpr uint32_t kDefineShapeTagId = 2u;
    static constexpr uint32_t kDefineTextTagId = 0xbu;
    static constexpr uint32_t kDefineSpriteTagId = 0x27u;

    int32_t nesting = 0;
    for ( FlashTag *pTag = pFirstTag; pTag != nullptr;
          pTag = pTag->getNextTag() ) {
        if ( pTag->TagId == kEndTagId ) {
            if ( nesting == 0 ) {
                break;
            }
            nesting--;
            continue;
        }
        if ( pTag->TagId == kDefineSpriteTagId ) {
            nesting++;
        }

        if ( pTag->TagId != kDefineShapeTagId &&
             pTag->TagId != kDefineTextTagId ) {
            continue;
        }

        if ( *(const uint32_t *)( (const uint8_t *)pTag + 8 ) == character ) {
            return pTag;
        }
    }

    return nullptr;
}

static FlashTag *GetRootTimeline( FlashResourceHeader *pHeader ) {
    uint8_t *pRoot = (uint8_t *)pHeader + 0x48;
    return (FlashTag *)( pRoot + *(const int32_t *)( pRoot + 0x14 ) );
}

static int32_t FindRootCharacterByName( FlashTag *pFirstTag,
                                        const char *pName ) {
    static constexpr uint32_t kEndTagId = 0u;
    static constexpr uint32_t kPlaceObject2TagId = 0x1au;
    static constexpr uint32_t kDefineSpriteTagId = 0x27u;

    int32_t nesting = 0;
    for ( FlashTag *pTag = pFirstTag; pTag != nullptr;
          pTag = pTag->getNextTag() ) {
        if ( pTag->TagId == kDefineSpriteTagId ) {
            nesting++;
            continue;
        }
        if ( pTag->TagId == kEndTagId ) {
            if ( nesting == 0 ) {
                break;
            }
            nesting--;
            continue;
        }
        if ( nesting != 0 || pTag->TagId != kPlaceObject2TagId ) {
            continue;
        }

        FlashTagPlaceObject2Data *pPlace = (FlashTagPlaceObject2Data *)pTag;
        const char *pPlaceName = GetPlaceObjectName( pPlace );
        if ( pPlaceName != nullptr && strcmp( pPlaceName, pName ) == 0 ) {
            return (int32_t)pPlace->Unknown;
        }
    }

    return -1;
}

static FlashTag *FindSpriteForCharacter( FlashTag *pFirstTag,
                                         const int32_t character ) {
    static constexpr uint32_t kEndTagId = 0u;
    static constexpr uint32_t kDefineSpriteTagId = 0x27u;

    int32_t nesting = 0;
    for ( FlashTag *pTag = pFirstTag; pTag != nullptr;
          pTag = pTag->getNextTag() ) {
        if ( pTag->TagId == kEndTagId ) {
            if ( nesting == 0 ) {
                break;
            }
            nesting--;
            continue;
        }
        if ( pTag->TagId != kDefineSpriteTagId ) {
            continue;
        }

        nesting++;
        FlashTagDefineSpriteData *pSprite = (FlashTagDefineSpriteData *)pTag;
        if ( nesting == 1 && (int32_t)pSprite->Index == character ) {
            return pTag;
        }
    }

    return nullptr;
}

static void SetPlaceObjectTranslation( FlashTagPlaceObject2Data *pData,
                                       const float x,
                                       const float y ) {
    float currentX = 0.0f;
    float currentY = 0.0f;
    if ( !GetPlaceObjectTranslation( pData, &currentX, &currentY ) ) {
        return;
    }

    static constexpr uint8_t kHasCharacter = 0x02u;
    static constexpr uint8_t kHasClipActions = 0x80u;

    uint32_t offset = ( pData->LowFlags & kHasCharacter ) ? 0xeu : 0xcu;
    offset = ( offset + 3u ) & ~3u;
    if ( ( pData->Flags & 0x100 ) != 0 ) {
        offset += 8u;
    }
    if ( ( pData->LowFlags & kHasClipActions ) != 0 ) {
        offset += 8u;
    }

    float *pTranslation = (float *)( (uint8_t *)pData + offset );
    pTranslation[0] = x;
    pTranslation[1] = y;
}

static bool SetTextRunColor( FlashTag *pTextTag, const ColorRGBA &color ) {
    if ( pTextTag == nullptr || pTextTag->TagSize <= 0x34u ) {
        return false;
    }

    uint8_t *pStream = (uint8_t *)pTextTag + 0x34u;
    const uint8_t *pEnd = (const uint8_t *)pTextTag + pTextTag->TagSize;

    while ( pStream < pEnd ) {
        const uint8_t opcode = *pStream;

        uint32_t recordSize = 0u;
        switch ( opcode ) {
        case 0x00u:
            return false;
        case 0x01u:
        case 0x02u:
        case 0xffu:
            recordSize = 4u;
            break;
        case 0x10u:
            recordSize = 6u;
            break;
        case 0x04u:
        case 0x08u:
            recordSize = 8u;
            break;
        default:
            return false;
        }

        if ( pStream + recordSize > pEnd ) {
            return false;
        }

        if ( opcode == 0x04u ) {
            pStream[4] = color.R;
            pStream[5] = color.G;
            pStream[6] = color.B;
            pStream[7] = color.A;
            return true;
        }

        pStream += recordSize;
    }

    return false;
}

static uint32_t SetTextRun( FlashTag *pTextTag,
                            const FlashTagDefineFontData *pFont,
                            const char *pText ) {
    static constexpr uint32_t kGlyphCountOffset = 8u + 0x30u;
    static constexpr uint32_t kRecordsOffset = 8u + 0x46u;
    static constexpr uint32_t kRecordSize = 4u;

    if ( pTextTag == nullptr || pText == nullptr ) {
        return 0u;
    }

    uint8_t *pTagBytes = (uint8_t *)pTextTag;
    const uint32_t numGlyphs = (uint32_t)strlen( pText );

    if ( numGlyphs == 0u ||
         kRecordsOffset + numGlyphs * kRecordSize + 1u > pTextTag->TagSize ) {
        return 0u;
    }

    uint8_t *pRecords = pTagBytes + kRecordsOffset;

    float scale = 0.0f;
    if ( pFont != nullptr ) {
        const uint16_t oldWidth = GetGlyphWidth( pFont, pRecords[1] );
        const uint16_t oldAdvance = *(const uint16_t *)( pRecords + 2 );
        if ( oldWidth != 0u ) {
            scale = float( oldAdvance ) / float( oldWidth );
        }
    }

    uint32_t totalAdvance = 0u;
    for ( uint32_t i = 0; i < numGlyphs; i++ ) {
        const uint8_t glyphIndex = (uint8_t)( pText[i] - 0x20 );
        pRecords[i * kRecordSize + 0] = 0xffu;
        pRecords[i * kRecordSize + 1] = glyphIndex;

        if ( scale > 0.0f ) {
            const uint16_t width = GetGlyphWidth( pFont, glyphIndex );
            if ( width != 0u ) {
                *(uint16_t *)( pRecords + i * kRecordSize + 2 ) =
                    (uint16_t)( float( width ) * scale + 0.5f );
            }
        }

        totalAdvance += *(const uint16_t *)( pRecords + i * kRecordSize + 2 );
    }

    pRecords[numGlyphs * kRecordSize] = 0u;
    *(uint32_t *)( pTagBytes + kGlyphCountOffset ) = numGlyphs;

    return totalAdvance;
}

static void PatchTabBar( FlashPlayer *pMovie ) {
    static constexpr uint16_t kDeadDepth = 0xfffeu;
    static constexpr uint32_t kEndTagId = 0u;
    static constexpr uint32_t kDefineShapeTagId = 2u;
    static constexpr uint32_t kDefineTextTagId = 0xbu;
    static constexpr uint32_t kPlaceObject2TagId = 0x1au;
    static constexpr uint32_t kDefineSpriteTagId = 0x27u;

    if ( pMovie == nullptr || pMovie->getBoundFlashResource() == nullptr ) {
        return;
    }

    FlashResourceHeader *pHeader = pMovie->getBoundFlashResource()->getHeader();
    if ( pHeader == nullptr ) {
        return;
    }

    uint8_t *pRoot = (uint8_t *)pHeader + 0x48;
    FlashTag *pFirstTag = (FlashTag *)( pRoot + *(int32_t *)( pRoot + 0x14 ) );

    int32_t tabBarCharacter = -1;
    int32_t nesting = 0;
    for ( FlashTag *pTag = pFirstTag; pTag != nullptr;
          pTag = pTag->getNextTag() ) {
        if ( pTag->TagId == kDefineSpriteTagId ) {
            nesting++;
            continue;
        }
        if ( pTag->TagId == kEndTagId ) {
            if ( nesting == 0 ) {
                break;
            }
            nesting--;
            continue;
        }
        if ( nesting != 0 || pTag->TagId != kPlaceObject2TagId ) {
            continue;
        }

        FlashTagPlaceObject2Data *pPlace = (FlashTagPlaceObject2Data *)pTag;
        const char *pName = GetPlaceObjectName( pPlace );
        if ( pName != nullptr && strcmp( pName, kTabBarClip + 1 ) == 0 ) {
            tabBarCharacter = (int32_t)pPlace->Unknown;
            break;
        }
    }

    if ( tabBarCharacter < 0 ) {
        return;
    }

    const FlashTagDefineFontData *pFont = FindFontTag( pHeader );

    FlashTagPlaceObject2Data *pLeftLabel = nullptr;
    FlashTagPlaceObject2Data *pRightLabel = nullptr;
    FlashTag *pLeftText = nullptr;
    FlashTag *pRightText = nullptr;
    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
    int32_t numShapesRemoved = 0;

    nesting = 0;
    for ( FlashTag *pTag = pFirstTag; pTag != nullptr;
          pTag = pTag->getNextTag() ) {
        if ( pTag->TagId == kEndTagId ) {
            if ( nesting == 0 ) {
                break;
            }
            nesting--;
            continue;
        }
        if ( pTag->TagId != kDefineSpriteTagId ) {
            continue;
        }

        nesting++;
        FlashTagDefineSpriteData *pSprite = (FlashTagDefineSpriteData *)pTag;
        if ( nesting != 1 || (int32_t)pSprite->Index != tabBarCharacter ) {
            continue;
        }

        for ( FlashTag *pChild = pTag->getNextTag();
              pChild != nullptr && pChild->TagId != kEndTagId;
              pChild = pChild->getNextTag() ) {
            if ( pChild->TagId != kPlaceObject2TagId ||
                 ( ( (FlashTagPlaceObject2Data *)pChild )->LowFlags & 0x02u ) ==
                     0 ) {
                continue;
            }

            FlashTagPlaceObject2Data *pPlace =
                (FlashTagPlaceObject2Data *)pChild;
            FlashTag *pCharacter =
                FindTagForCharacter( pFirstTag, pPlace->Unknown );
            if ( pCharacter == nullptr ) {
                continue;
            }

            if ( pCharacter->TagId == kDefineShapeTagId ) {
                *(uint16_t *)( (uint8_t *)pChild + 8 ) = kDeadDepth;
                numShapesRemoved++;
                continue;
            }

            if ( pCharacter->TagId != kDefineTextTagId ) {
                continue;
            }

            float placeX = 0.0f;
            float placeY = 0.0f;
            if ( !GetPlaceObjectTranslation( pPlace, &placeX, &placeY ) ) {
                continue;
            }

            if ( pLeftLabel == nullptr || placeX < leftX ) {
                pLeftLabel = pPlace;
                pLeftText = pCharacter;
                leftX = placeX;
                leftY = placeY;
            }
            if ( pRightLabel == nullptr || placeX > rightX ) {
                pRightLabel = pPlace;
                pRightText = pCharacter;
                rightX = placeX;
                rightY = placeY;
            }
        }
        break;
    }

    if ( pLeftText == nullptr || pRightText == nullptr ||
         pLeftLabel == pRightLabel ) {
        return;
    }

    const uint32_t keyWidth = SetTextRun( pLeftText, pFont, kTabBarKeyLabel );
    const uint32_t nameWidth =
        SetTextRun( pRightText, pFont, kTabBarNameLabel );

    if ( keyWidth == 0u || nameWidth == 0u ) {
        return;
    }

    SetTextRunColor( pLeftText, kTabBarKeyColor );

    const uint32_t spaceWidth = keyWidth / (uint32_t)strlen( kTabBarKeyLabel );
    const float joinedX = rightX - float( keyWidth + spaceWidth );
    if ( joinedX > 0.0f && joinedX < rightX ) {
        SetPlaceObjectTranslation( pLeftLabel, joinedX, rightY );
    }
}

static void PatchPanesSideBySide( FlashPlayer *pMovie ) {
    static constexpr uint16_t kDetailPaneDepth = 1u;
    static constexpr uint16_t kDeadDepth = 0xfffeu;

    static constexpr uint32_t kEndTagId = 0u;
    static constexpr uint32_t kDoActionTagId = 0xcu;
    static constexpr uint32_t kPlaceObject2TagId = 0x1au;
    static constexpr uint32_t kRemoveObject2TagId = 0x1cu;
    static constexpr uint32_t kDefineSpriteTagId = 0x27u;
    static constexpr uint32_t kFrameLabelTagId = 0x2bu;

    if ( pMovie == nullptr || pMovie->getBoundFlashResource() == nullptr ) {
        return;
    }

    FlashResourceHeader *pHeader = pMovie->getBoundFlashResource()->getHeader();
    if ( pHeader == nullptr ) {
        return;
    }

    uint8_t *pRoot = (uint8_t *)pHeader + 0x48;
    FlashTag *pTag = (FlashTag *)( pRoot + *(int32_t *)( pRoot + 0x14 ) );

    static constexpr uint32_t kShowFrameTagId = 1u;
    static constexpr uint32_t kStopScript = 0x00000007u;

    enum eRange {
        R_Untouched,
        R_HoldPackPane,
        R_DetailPane,
        R_AfterDetailPane
    };
    eRange range = R_Untouched;

    int32_t frameIndex = 1;
    FlashTag *pFrameScript = nullptr;

    int32_t numRetargeted = 0;
    int32_t numNeutered = 0;
    int32_t numStopped = 0;
    int32_t nesting = 0;

    for ( ; pTag != nullptr; pTag = pTag->getNextTag() ) {
        if ( pTag->TagId == kDefineSpriteTagId ) {
            nesting++;
            continue;
        }
        if ( pTag->TagId == kEndTagId ) {
            if ( nesting == 0 ) {
                break;
            }
            nesting--;
            continue;
        }
        if ( nesting != 0 ) {
            continue;
        }

        if ( pTag->TagId == kShowFrameTagId ) {
            pFrameScript = nullptr;
            frameIndex++;
            continue;
        }

        if ( pTag->TagId == kFrameLabelTagId ) {
            const char *pLabel = ( (FlashTagFrameLabelData *)pTag )->pLabel;
            const bool bLeavesPackPane = strcmp( pLabel, "back_bnk" ) == 0;
            const bool bLeavesDetailPane = strcmp( pLabel, "back_cine" ) == 0;

            if ( ( bLeavesPackPane || bLeavesDetailPane ) &&
                 pFrameScript != nullptr ) {
                *(uint32_t *)( (uint8_t *)pFrameScript + 8 ) = kStopScript;
                numStopped++;
            }

            if ( bLeavesPackPane ) {
                gPackRestFrame = frameIndex - 2;
                range = R_HoldPackPane;
            } else if ( strcmp( pLabel, kDetailPaneFrame ) == 0 ) {
                range = R_DetailPane;
            } else if ( bLeavesDetailPane ) {
                gPaneRestFrame = frameIndex - 2;
                range = R_AfterDetailPane;
            }
            continue;
        }

        if ( pTag->TagId == kDoActionTagId ) {
            pFrameScript = pTag;

            if ( range == R_HoldPackPane ) {
                *(uint16_t *)( (uint8_t *)pTag + 8 ) = 0u;
                numNeutered++;
            } else if ( range == R_AfterDetailPane ) {
                *(uint32_t *)( (uint8_t *)pTag + 8 ) = kStopScript;
                numStopped++;
            }
            continue;
        }

        if ( pTag->TagId == kPlaceObject2TagId &&
             *(const uint16_t *)( (const uint8_t *)pTag + 8 ) == 0u ) {
            float restX = 0.0f;
            float restY = 0.0f;
            if ( GetPlaceObjectTranslation( (FlashTagPlaceObject2Data *)pTag,
                                            &restX, &restY ) ) {
                if ( range == R_Untouched ) {
                    gPackPaneRestX = restX;
                } else if ( range == R_DetailPane ) {
                    gDetailPaneRestX = restX;
                }
            }
        }

        if ( range == R_Untouched ) {
            continue;
        }

        if ( pTag->TagId != kPlaceObject2TagId &&
             pTag->TagId != kRemoveObject2TagId ) {
            continue;
        }

        uint16_t *pDepth = (uint16_t *)( (uint8_t *)pTag + 8 );
        if ( *pDepth != 0u ) {
            continue;
        }

        *pDepth = ( range == R_DetailPane ) ? kDetailPaneDepth : kDeadDepth;
        numRetargeted++;
    }
}

static void GatherSpriteExtentX( FlashTag *pFirstTag,
                                 FlashTag *pSprite,
                                 const float offsetX,
                                 const int32_t depthBudget,
                                 float *pMinX,
                                 float *pMaxX ) {
    static constexpr uint32_t kEndTagId = 0u;
    static constexpr uint32_t kDefineShapeTagId = 2u;
    static constexpr uint32_t kPlaceObject2TagId = 0x1au;
    static constexpr uint8_t kHasCharacter = 0x02u;

    if ( pSprite == nullptr || depthBudget < 0 ) {
        return;
    }

    for ( FlashTag *pChild = pSprite->getNextTag();
          pChild != nullptr && pChild->TagId != kEndTagId;
          pChild = pChild->getNextTag() ) {
        if ( pChild->TagId != kPlaceObject2TagId ) {
            continue;
        }

        FlashTagPlaceObject2Data *pPlace = (FlashTagPlaceObject2Data *)pChild;
        if ( ( pPlace->LowFlags & kHasCharacter ) == 0 ) {
            continue;
        }

        float placeX = 0.0f;
        float placeY = 0.0f;
        GetPlaceObjectTranslation( pPlace, &placeX, &placeY );

        FlashTag *pCharacter =
            FindTagForCharacter( pFirstTag, pPlace->Unknown );
        if ( pCharacter != nullptr && pCharacter->TagId == kDefineShapeTagId ) {
            const FlashRECT &bb = ( (FlashTagDefineShapeData *)pCharacter )->BB;
            *pMinX = Min( *pMinX, offsetX + placeX + bb.X );
            *pMaxX = Max( *pMaxX, offsetX + placeX + bb.Y );
            continue;
        }

        GatherSpriteExtentX(
            pFirstTag, FindSpriteForCharacter( pFirstTag, pPlace->Unknown ),
            offsetX + placeX, depthBudget - 1, pMinX, pMaxX );
    }
}

static void NarrowShapeRight( FlashTagDefineShapeData *pShape,
                              const int16_t cutFromX,
                              const int16_t shift ) {
    static constexpr uint32_t kEndRecord = 0u;
    static constexpr uint32_t kStyleTableRecord = 3u;
    static constexpr uint32_t kTriangleListRecord = 8u;
    static constexpr uint32_t kFilledTriangleListRecord = 9u;
    static constexpr uint32_t kLineStripRecord = 0x108u;

    uint8_t *pBase = (uint8_t *)&pShape->TagSize;
    uint8_t *pEnd = (uint8_t *)pShape + pShape->TagSize;
    uint8_t *pRecord = pBase + pShape->field_0x1ci;

    while ( pRecord >= pBase && pRecord + 8 <= pEnd ) {
        const uint32_t record = *(const uint32_t *)pRecord;
        if ( record == kEndRecord ) {
            break;
        }

        if ( record == kStyleTableRecord ) {
            pRecord = pBase + *(const uint32_t *)( pRecord + 4 );
            continue;
        }

        const uint32_t numEntries = *(const uint16_t *)( pRecord + 4 );

        uint8_t *pVertices = nullptr;
        uint32_t numVertices = 0u;
        if ( record == kTriangleListRecord ) {
            pVertices = pRecord + 8;
            numVertices = numEntries * 3u;
        } else if ( record == kLineStripRecord ) {
            pVertices = pRecord + 8;
            numVertices = numEntries;
        } else if ( record == kFilledTriangleListRecord ) {
            pVertices = pRecord + 0xc;
            numVertices = numEntries;
        } else {
            break;
        }

        if ( pVertices + numVertices * 4u > pEnd ) {
            break;
        }

        for ( uint32_t i = 0; i < numVertices; i++ ) {
            int16_t *pX = (int16_t *)( pVertices + i * 4u );
            if ( *pX > cutFromX ) {
                *pX = (int16_t)( *pX - shift );
            }
        }

        pRecord = pVertices + numVertices * 4u;
    }

    if ( pShape->BB.Y > (float)cutFromX ) {
        pShape->BB.Y -= (float)shift;
    }
}

static void PatchPackPaneWidth( FlashPlayer *pMovie ) {
    static constexpr int16_t kCutFromX = 8000;
    static constexpr float kPaneGap = 300.0f;

    static constexpr uint32_t kEndTagId = 0u;
    static constexpr uint32_t kDefineShapeTagId = 2u;
    static constexpr uint32_t kPlaceObject2TagId = 0x1au;
    static constexpr uint8_t kHasCharacter = 0x02u;

    if ( pMovie == nullptr || pMovie->getBoundFlashResource() == nullptr ) {
        return;
    }

    FlashResourceHeader *pHeader = pMovie->getBoundFlashResource()->getHeader();
    if ( pHeader == nullptr ) {
        return;
    }

    FlashTag *pFirstTag = GetRootTimeline( pHeader );

    FlashTag *pPackPane = FindSpriteForCharacter(
        pFirstTag, FindRootCharacterByName( pFirstTag, kPackListPath + 1 ) );
    FlashTag *pDetailPane = FindSpriteForCharacter(
        pFirstTag, FindRootCharacterByName( pFirstTag, kDetailListPath + 1 ) );
    if ( pPackPane == nullptr || pDetailPane == nullptr ) {
        return;
    }

    auto gatherShapes = []( FlashTag *pDictionary, FlashTag *pPane,
                            FlashTagDefineShapeData **pOutShapes,
                            const int32_t maxShapes ) {
        int32_t numShapes = 0;
        for ( FlashTag *pChild = pPane->getNextTag();
              pChild != nullptr && pChild->TagId != kEndTagId;
              pChild = pChild->getNextTag() ) {
            if ( pChild->TagId != kPlaceObject2TagId ) {
                continue;
            }

            FlashTagPlaceObject2Data *pPlace =
                (FlashTagPlaceObject2Data *)pChild;
            if ( ( pPlace->LowFlags & kHasCharacter ) == 0 ) {
                continue;
            }

            FlashTag *pCharacter =
                FindTagForCharacter( pDictionary, pPlace->Unknown );
            if ( pCharacter == nullptr ||
                 pCharacter->TagId != kDefineShapeTagId ) {
                continue;
            }

            FlashTagDefineShapeData *pShape =
                (FlashTagDefineShapeData *)pCharacter;

            bool bAlreadyGathered = false;
            for ( int32_t i = 0; i < numShapes; i++ ) {
                bAlreadyGathered |= pOutShapes[i] == pShape;
            }

            if ( !bAlreadyGathered && numShapes < maxShapes ) {
                pOutShapes[numShapes++] = pShape;
            }
        }

        return numShapes;
    };

    static constexpr int32_t kMaxPaneShapes = 8;
    FlashTagDefineShapeData *pPackShapes[kMaxPaneShapes] = {};
    FlashTagDefineShapeData *pDetailShapes[kMaxPaneShapes] = {};

    const int32_t numPackShapes =
        gatherShapes( pFirstTag, pPackPane, pPackShapes, kMaxPaneShapes );
    const int32_t numDetailShapes =
        gatherShapes( pFirstTag, pDetailPane, pDetailShapes, kMaxPaneShapes );
    if ( numPackShapes == 0 || numDetailShapes == 0 ) {
        return;
    }

    float packRightEdge = pPackShapes[0]->BB.Y;
    for ( int32_t i = 1; i < numPackShapes; i++ ) {
        packRightEdge = Max( packRightEdge, pPackShapes[i]->BB.Y );
    }

    float detailLeftEdge = pDetailShapes[0]->BB.X;
    for ( int32_t i = 1; i < numDetailShapes; i++ ) {
        detailLeftEdge = Min( detailLeftEdge, pDetailShapes[i]->BB.X );
    }

    const float targetRightEdge =
        gDetailPaneRestX + detailLeftEdge - gPackPaneRestX - kPaneGap;

    float keptContentRightEdge = 0.0f;
    for ( FlashTag *pChild = pPackPane->getNextTag();
          pChild != nullptr && pChild->TagId != kEndTagId;
          pChild = pChild->getNextTag() ) {
        if ( pChild->TagId != kPlaceObject2TagId ) {
            continue;
        }

        float placeX = 0.0f;
        float placeY = 0.0f;
        if ( GetPlaceObjectTranslation( (FlashTagPlaceObject2Data *)pChild,
                                        &placeX, &placeY ) &&
             placeX <= (float)kCutFromX ) {
            keptContentRightEdge = Max( keptContentRightEdge, placeX );
        }
    }

    if ( targetRightEdge <= keptContentRightEdge ) {
        gPackHighlightScaleX = 0.0f;
        return;
    }

    if ( packRightEdge <= targetRightEdge ) {
        return;
    }

    const int16_t shift = (int16_t)( packRightEdge - targetRightEdge );

    for ( int32_t i = 0; i < numPackShapes; i++ ) {
        NarrowShapeRight( pPackShapes[i], kCutFromX, shift );
    }

    for ( FlashTag *pChild = pPackPane->getNextTag();
          pChild != nullptr && pChild->TagId != kEndTagId;
          pChild = pChild->getNextTag() ) {
        if ( pChild->TagId != kPlaceObject2TagId ) {
            continue;
        }

        FlashTagPlaceObject2Data *pPlace = (FlashTagPlaceObject2Data *)pChild;
        float placeX = 0.0f;
        float placeY = 0.0f;
        if ( !GetPlaceObjectTranslation( pPlace, &placeX, &placeY ) ||
             placeX <= (float)kCutFromX ) {
            continue;
        }

        SetPlaceObjectTranslation( pPlace, placeX - (float)shift, placeY );
    }

    FlashTag *pHighlight = nullptr;
    float highlightPlaceX = 0.0f;
    for ( FlashTag *pChild = pPackPane->getNextTag();
          pChild != nullptr && pChild->TagId != kEndTagId;
          pChild = pChild->getNextTag() ) {
        if ( pChild->TagId != kPlaceObject2TagId ) {
            continue;
        }

        FlashTagPlaceObject2Data *pPlace = (FlashTagPlaceObject2Data *)pChild;
        const char *pName = GetPlaceObjectName( pPlace );
        if ( pName == nullptr || strcmp( pName, kHighlightClipName ) != 0 ) {
            continue;
        }

        float placeY = 0.0f;
        GetPlaceObjectTranslation( pPlace, &highlightPlaceX, &placeY );
        pHighlight = FindSpriteForCharacter( pFirstTag, pPlace->Unknown );
        break;
    }

    if ( pHighlight == nullptr ) {
        return;
    }

    float highlightLeft = FLT_MAX;
    float highlightRight = -FLT_MAX;
    GatherSpriteExtentX( pFirstTag, pHighlight, highlightPlaceX, 2,
                         &highlightLeft, &highlightRight );

    if ( highlightRight <= highlightLeft ) {
        return;
    }

    const float highlightInset = packRightEdge - highlightRight;
    const float scale = ( targetRightEdge - highlightInset - highlightLeft ) /
                        ( highlightRight - highlightLeft );
    if ( scale <= 0.0f || scale >= 1.0f ) {
        return;
    }

    static constexpr float kTwipsPerPixel = 20.0f;
    gPackHighlightScaleX = scale * 100.0f;
    gPackHighlightX =
        ( highlightLeft - ( highlightLeft - highlightPlaceX ) * scale ) /
        kTwipsPerPixel;
}

static std::string GetTextRun( const FlashTag *pTextTag ) {
    static constexpr uint32_t kGlyphCountOffset = 8u + 0x30u;
    static constexpr uint32_t kRecordsOffset = 8u + 0x46u;
    static constexpr uint32_t kRecordSize = 4u;

    const uint8_t *pTagBytes = (const uint8_t *)pTextTag;
    if ( pTextTag->TagSize < kRecordsOffset ) {
        return std::string();
    }

    const uint32_t numGlyphs =
        *(const uint32_t *)( pTagBytes + kGlyphCountOffset );
    if ( numGlyphs == 0u || numGlyphs > 64u ||
         kRecordsOffset + numGlyphs * kRecordSize > pTextTag->TagSize ) {
        return std::string();
    }

    const uint8_t *pRecords = pTagBytes + kRecordsOffset;

    std::string content;
    for ( uint32_t i = 0; i < numGlyphs; i++ ) {
        content += (char)( pRecords[i * kRecordSize + 1] + 0x20 );
    }

    return content;
}

static void RetitleHeaders( FlashPlayer *pMovie ) {
    struct Retitle {
        const char *pFrom;
        const char *pTo;
    };
    static constexpr Retitle kRetitles[] = {
        {"BNKs",       "Mods"   },
        {"Cinematiks", "Details"},
        {"CINEMATIKS", ""       }
    };

    if ( pMovie == nullptr || pMovie->getBoundFlashResource() == nullptr ) {
        return;
    }

    FlashResourceHeader *pHeader = pMovie->getBoundFlashResource()->getHeader();
    if ( pHeader == nullptr ) {
        return;
    }

    const FlashTagDefineFontData *pFont = FindFontTag( pHeader );

    static constexpr uint32_t kDefineTextTagId = 11u;

    uint8_t *pBuffer = (uint8_t *)pHeader;
    for ( FlashTag *pTag = (FlashTag *)( pBuffer + 0x48 ); pTag != nullptr;
          pTag = pTag->getNextTag() ) {
        if ( pTag->TagId != kDefineTextTagId ) {
            continue;
        }

        const std::string current = GetTextRun( pTag );
        for ( const Retitle &retitle : kRetitles ) {
            if ( current != retitle.pFrom ) {
                continue;
            }

            std::string replacement = retitle.pTo;
            while ( replacement.length() < current.length() ) {
                replacement += ' ';
            }

            SetTextRun( pTag, pFont, replacement.c_str() );
            break;
        }
    }
}

MngModsUI::MngModsUI()
    : packList( &packRows ),
      detailList( &detailRows ),
      pMovie( nullptr ),
      autoOpenTicks( -1 ),
      numOpenLists( 0 ),
      paneSequenceTicks( 0 ),
      paneSequence( PS_Hidden ),
      bInputExclusive( false ),
      bListHidden( false ),
      bListKeyLatched( false ),
      bPaneKeyLatched( false ),
      bPanesPlaced( false ),
      bAnimationDone( false ),
      bSuspended( false ),
      bTickSeen( false ),
      bVisible( false ),
      bBound( false ),
      bToggleLatched( false ) {}

MngModsUI::~MngModsUI() {}

void MngModsUI::RegisterMovie() {
    if ( gpFlash == nullptr ) {
        return;
    }

    static bool bRegistered = false;
    if ( bRegistered ) {
        return;
    }
    bRegistered = true;

    FlashMovie movie;
    movie.Filename = kMovieName;
    movie.GameMode = GM_Login;
    movie.MaxNumVars = 2048;
    movie.NumDrawListStrips = 600;
    movie.NumDrawListVertices = 15000;

    gpFlash->pushMovie( movie );
}

void MngModsUI::bind() {
    pMovie = ( gpFlash != nullptr ) ? gpFlash->getFlashPlayer( kMovieName )
                                    : nullptr;
    if ( pMovie == nullptr ) {
        OTDU_LOG_WARN( "'%s' is not loaded for this game mode\n", kMovieName );
        bBound = false;
        return;
    }

    packList.setPlayer( pMovie, kPackListPath );
    detailList.setPlayer( pMovie, kDetailListPath );
    packList.setOnSelectionChanged( [this]() {
        rebuildDetailRows();
        detailList.refresh();
    } );

    gpFlash->bindPlayerEntity( this, kMovieName );

    bBound = true;
    bVisible = false;
    numOpenLists = 0;
    bListHidden = true;
    bPanesPlaced = false;
    bSuspended = false;
    paneSequence = PS_Hidden;
    setInputExclusive( false );
    setScreenActive( false );

    autoOpenTicks = kAutoOpenTicks;

    RetitleHeaders( pMovie );
    PatchPanesSideBySide( pMovie );
    PatchPackPaneWidth( pMovie );
    PatchTabBar( pMovie );
}

void MngModsUI::unbind() {
    if ( bBound ) {
        hide();
    }

    pMovie = nullptr;
    bBound = false;
}

void MngModsUI::setListHidden( const bool bHidden ) {
    if ( bListHidden == bHidden ) {
        return;
    }

    bListHidden = bHidden;

    setClipVisible( kPackListPath, !bHidden );
    setClipVisible( kDetailListPath, !bHidden );

    if ( bHidden ) {
        paneSequence = PS_Hidden;
        packList.close();
        detailList.close();
        setInputExclusive( false );
    } else if ( numOpenLists > 0 ) {
        setInputExclusive( true );
    }
}

void MngModsUI::resizePackHighlight() {
    if ( pMovie == nullptr || gPackHighlightScaleX <= 0.0f ) {
        return;
    }

    FlashClip highlightClip;
    if ( !pMovie->getClipPlayer( kPackHighlightPath, &highlightClip ) ||
         highlightClip.pSprite == nullptr ) {
        return;
    }

    highlightClip.setProperty( eFlashProperty::FP_X, gPackHighlightX );
    highlightClip.setProperty( eFlashProperty::FP_ScaleX,
                               gPackHighlightScaleX );
}

int32_t MngModsUI::getRootFrame() {
    FlashClip rootClip;
    if ( pMovie == nullptr || !pMovie->getClipPlayer( "/", &rootClip ) ||
         rootClip.pSprite == nullptr ) {
        return -1;
    }

    return rootClip.pSprite->getDisplayList()->getCurrentFrameIndex() - 1;
}

void MngModsUI::showPanes( const bool bAnimate ) {
    if ( pMovie == nullptr ) {
        return;
    }

    setListHidden( false );
    bPanesPlaced = true;
    paneSequenceTicks = 0;

    const bool bCanAnimate =
        bAnimate && gPackRestFrame >= 0 && gPaneRestFrame > gPackRestFrame;
    if ( bCanAnimate ) {
        paneSequence = PS_PackSliding;
        bAnimationDone = false;
        pMovie->showFrame( kPackPaneFrame, "/", true );
        return;
    }

    paneSequence = PS_Visible;
    if ( gPaneRestFrame >= 0 ) {
        pMovie->goToFrameAndPlay( gPaneRestFrame, "/", false );
    } else {
        pMovie->showFrame( kPackPaneFrame, "/", true );
    }
}

void MngModsUI::updatePaneSequence() {
    static constexpr int32_t kPaneSequenceTimeoutTicks = 240;

    if ( pMovie == nullptr || gPaneRestFrame < 0 ) {
        return;
    }

    if ( paneSequence == PS_Visible ) {
        pMovie->goToFrameAndPlay( gPaneRestFrame, "/", false );
        return;
    }

    const int32_t rootFrame = getRootFrame();
    paneSequenceTicks++;

    if ( rootFrame < 0 || paneSequenceTicks > kPaneSequenceTimeoutTicks ) {
        paneSequence = PS_Visible;
        pMovie->goToFrameAndPlay( gPaneRestFrame, "/", false );
        return;
    }

    const int32_t landingFrame =
        ( paneSequence == PS_PackSliding ) ? gPackRestFrame : gPaneRestFrame;

    if ( !bAnimationDone ) {
        bAnimationDone = rootFrame < landingFrame;
        return;
    }

    if ( rootFrame < landingFrame ) {
        return;
    }

    if ( paneSequence == PS_PackSliding ) {
        paneSequence = PS_DetailSliding;
        paneSequenceTicks = 0;
        bAnimationDone = false;
        pMovie->showFrame( kDetailPaneFrame, "/", true );
        return;
    }

    paneSequence = PS_Visible;
}

bool MngModsUI::isFrontEndScreenUp() {
    if ( gpFlashMessageBox != nullptr &&
         ( gpFlashMessageBox->getNumOpenMsgBox() != 0 ||
           gpFlashMessageBox->getUnknownFlag() ) ) {
        return true;
    }

    FlashPlayer *pOptionsMovie =
        ( gpFlash != nullptr ) ? gpFlash->getFlashPlayer( kOptionsMovieName )
                               : nullptr;
    if ( pOptionsMovie == nullptr ) {
        return false;
    }

    for ( const char *pClipPath : kFrontEndScreenClips ) {
        FlashClip clip;
        if ( !pOptionsMovie->getClipPlayer( pClipPath, &clip ) ||
             clip.pSprite == nullptr ) {
            continue;
        }

        if ( clip.getProperty( eFlashProperty::FP_Visible ) ) {
            return true;
        }
    }

    return false;
}

void MngModsUI::setClipVisible( const char *pPath, const bool bShow ) {
    if ( pMovie == nullptr ) {
        return;
    }

    FlashClip clip;
    if ( pMovie->getClipPlayer( pPath, &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, bShow );
    }
}

void MngModsUI::setScreenActive( const bool bActive ) {
    if ( gpFlash == nullptr ) {
        return;
    }

    gpFlash->enablePlayer( kMovieName, ModalMessageBox::MMBDS_Closing, bActive,
                           kModsUIFlagBit );
    gpFlash->enablePlayer( kMovieName, ModalMessageBox::MMBDS_Closed, bActive,
                           kModsUIFlagBit );

    gpFlash->setMovieDrawnOnTop( kMovieName, bActive );

    if ( bActive ) {
        gpFlash->grantMovieInput( kMovieName );
    }

    if ( bActive ) {
        for ( const char *pClip : kClipsToHide ) {
            setClipVisible( pClip, false );
        }
    }
}

void MngModsUI::setInputExclusive( const bool bExclusive ) {
    if ( gpFlash == nullptr || bInputExclusive == bExclusive ) {
        return;
    }

    bInputExclusive = bExclusive;

    for ( const char *pName : kMoviesToMute ) {
        gpFlash->enablePlayer( pName, ModalMessageBox::MMBDS_Closed,
                               !bExclusive, kModsUIFlagBit );
    }
}

void MngModsUI::show() {
    if ( !bBound || bVisible ) {
        return;
    }

    bVisible = true;

    rebuildPackRows();
    rebuildDetailRows();

    setScreenActive( true );

    bListHidden = true;
    bPanesPlaced = false;
    paneSequence = PS_Hidden;
    pMovie->showFrame( "remote", "/", false );

    FlashClip rootClip;
    pMovie->getClipPlayer( "/", &rootClip );
}

int32_t MngModsUI::getPackListCapacity() const {
    return packList.getCapacity();
}

int32_t MngModsUI::getDetailListCapacity() const {
    return detailList.getCapacity();
}

void MngModsUI::hide() {
    if ( !bVisible ) {
        return;
    }

    bVisible = false;
    numOpenLists = 0;
    bListHidden = true;
    bPanesPlaced = false;
    bSuspended = false;
    paneSequence = PS_Hidden;
    setInputExclusive( false );
    setScreenActive( false );

    if ( gpModManager != nullptr && gpModManager->hasPendingChanges() ) {
        applyChanges();
    }
}

void MngModsUI::pollInput() {
    uint32_t mask = gpFlash->getInputMask();
    const bool bListKeyDown = ( mask & FK_Option1 ) == FK_Option1;
    if ( bListKeyDown && !bListKeyLatched && !bSuspended ) {
        if ( bListHidden ) {
            showPanes( true );
        } else {
            setListHidden( true );
        }
    }
    bListKeyLatched = bListKeyDown;
}

void MngModsUI::tick( float totalTime, float deltaTime ) {
    if ( !bTickSeen ) {
        bTickSeen = true;
    }

    if ( autoOpenTicks > 0 ) {
        autoOpenTicks--;
        if ( autoOpenTicks == 0 ) {
            show();
        }
    }

    if ( !bVisible ) {
        return;
    }

    const bool bBlocked = isFrontEndScreenUp();
    if ( bBlocked != (bool)bSuspended ) {
        bSuspended = bBlocked;

        if ( bBlocked ) {
            setInputExclusive( false );
            setScreenActive( false );
        } else {
            setScreenActive( true );
            if ( !bListHidden ) {
                showPanes( false );
            }
        }
    }

    if ( bSuspended ) {
        return;
    }

    for ( const char *pClip : kClipsToHide ) {
        setClipVisible( pClip, false );
    }
    setClipVisible( kTabBarClip, true );
    setClipVisible( kTabBarCursorClip, false );

    if ( !bListHidden ) {
        updatePaneSequence();
    }

    if ( bListHidden && bPanesPlaced ) {
        setClipVisible( kPackListPath, false );
        setClipVisible( kDetailListPath, false );
    }

    gpFlash->grantMovieInput( kMovieName );

    if ( bListHidden ) {
        return;
    }

    resizePackHighlight();

    if ( packList.getCapacity() != 0 ) {
        packList.tick( totalTime, deltaTime );
    }
    if ( detailList.getCapacity() != 0 ) {
        detailList.tick( totalTime, deltaTime );
    }
}

void MngModsUI::rebuildPackRows() {
    packRows.clear();

    if ( gpModManager == nullptr ) {
        return;
    }

    if ( gpModManager->getPacks().empty() ) {
        ModsListRow row;
        row.Name = GetPortString( PS_ModsFolderEmpty );
        row.State = FlashList::eLineState::FLLS_Normal;
        packRows.push_back( row );
        return;
    }

    for ( const ModPack &pack : gpModManager->getPacks() ) {
        ModsListRow row;

        row.Name = pack.Id;

        if ( pack.bConflicting ) {
            row.State = FlashList::eLineState::FLLS_Red;
        } else if ( pack.bEnabled ) {
            row.State = FlashList::eLineState::FLLS_Green;
        } else {
            row.State = FlashList::eLineState::FLLS_Disabled;
        }

        packRows.push_back( row );
    }
}

void MngModsUI::rebuildDetailRows() {
    detailRows.clear();

    if ( gpModManager == nullptr ) {
        return;
    }

    const std::vector<ModPack> &packs = gpModManager->getPacks();
    const int32_t selected = packList.getSelectedIndex();
    if ( selected < 0 || selected >= (int32_t)packs.size() ) {
        return;
    }

    const ModPack &pack = packs[selected];

    auto addRow = [this]( const std::string &text,
                          FlashList::eLineState state ) {
        ModsListRow row;
        row.Name = text;
        row.State = state;
        detailRows.push_back( row );
    };

    const ePortString stateLabel = pack.bMandatory ? PS_ModBuiltIn
                                   : pack.bEnabled ? PS_ModEnabled
                                                   : PS_ModDisabled;
    addRow( GetPortString( stateLabel ),
            pack.bEnabled ? FlashList::eLineState::FLLS_Green
                          : FlashList::eLineState::FLLS_Disabled );

    if ( pack.NumEntries != 0u ) {
        addRow( std::string( GetPortString( PS_ModFiles ) ) + ": " +
                    std::to_string( pack.NumEntries ),
                FlashList::eLineState::FLLS_Normal );
    }

    if ( pack.bMounted ) {
        addRow( std::string( GetPortString( PS_ModMountedOnMap ) ) + " " +
                    std::to_string( pack.FilemapIndex ),
                FlashList::eLineState::FLLS_Normal );
    }

    if ( !pack.Status.empty() ) {
        addRow( pack.Status, pack.bConflicting
                                 ? FlashList::eLineState::FLLS_Red
                                 : FlashList::eLineState::FLLS_Disabled );
    }

    if ( gpModInstaller != nullptr ) {
        for ( const ModDBInjection &injection :
              gpModInstaller->getDatabaseInjections() ) {
            if ( injection.PackName != pack.DisplayName ) {
                continue;
            }

            if ( !injection.Rows.empty() ) {
                addRow( injection.Table + ": +" +
                            std::to_string( injection.Rows.size() ) + " " +
                            GetPortString( PS_ModRows ),
                        FlashList::eLineState::FLLS_Normal );
            }
            if ( !injection.Strings.empty() ) {
                addRow( injection.Table + ": +" +
                            std::to_string( injection.Strings.size() ) + " " +
                            GetPortString( PS_ModStrings ),
                        FlashList::eLineState::FLLS_Normal );
            }
        }
    }
}

void MngModsUI::toggleSelectedPack() {
    if ( gpModManager == nullptr ) {
        return;
    }

    const std::vector<ModPack> &packs = gpModManager->getPacks();
    const int32_t selected = packList.getSelectedIndex();
    if ( selected < 0 || selected >= (int32_t)packs.size() ) {
        return;
    }

    const ModPack &pack = packs[selected];
    gpModManager->setEnabled( pack.Id.c_str(), !pack.bEnabled );

    rebuildPackRows();
    rebuildDetailRows();

    packList.refresh();
    detailList.refresh();
}

void MngModsUI::applyChanges() {
    if ( gpModManager == nullptr ) {
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

bool MngModsUI::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    if ( !bVisible || pMessage == nullptr ) {
        return false;
    }

    if ( *pMessage == kWidget ) {
        FlashMessage *pWidgetName = pMessage + 1;
        OTDU_LOG_DEBUG( "Widget message for '%.4s'\n", pWidgetName->pAsChar );

        const bool bPackList = ( *pWidgetName == kPackListWidget );
        const bool bDetailList = ( *pWidgetName == kDetailListWidget );
        if ( !bPackList && !bDetailList ) {
            return false;
        }

        FlashMessage *pCommand = pWidgetName + 1;
        if ( *pCommand == FlashList::kOpen ) {
            numOpenLists++;
            bPanesPlaced = true;
            setInputExclusive( true );

            FlashClip listClip;
            const char *pPath = bPackList ? kPackListPath : kDetailListPath;
            pMovie->getClipPlayer( pPath, &listClip );
        } else if ( *pCommand == FlashList::kClose ) {
            numOpenLists = Max( 0, numOpenLists - 1 );
            if ( numOpenLists == 0 ) {
                setInputExclusive( false );
            }
        }

        const bool bHandled = bPackList
                                  ? packList.onMessage( pCommand, pPlayer )
                                  : detailList.onMessage( pCommand, pPlayer );
        return bHandled;
    }

    if ( *pMessage == kPackChosen ) {
        toggleSelectedPack();
        return true;
    }

    if ( *pMessage == kDetailChosen ) {
        return true;
    }

    return false;
}
