#include "shared.h"
#include "mng_font_players.h"

#include "2db.h"
#include "2dm.h"
#include "camera/camera_game.h"
#include "camera/gs_camera.h"
#include "draw_list.h"
#include "frustum.h"
#include "gs_render.h"
#include "material.h"
#include "render_scene.h"

#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/gs_playerdata.h"
#include "world/gs_mng_resources.h"
#include "world/mng_sector_name.h"

#include "gs_timer.h"

MngFontPlayers *gpMngFontPlayers = nullptr; // DAT_010e7ff4

static constexpr float kInvReferenceWidth = 1.0f / 1280.0f; // DAT_00f418b4
static constexpr float kInvReferenceHeight = 1.0f / 720.0f; // DAT_00f418b8
static constexpr float k43AspectCorrection = 0.75f;         // DAT_00f416a4
static constexpr float kGlyphAdvanceScale = 0.05f;          // DAT_00f41778

static constexpr float kMetersToKilometers = 0.001f;    // FLOAT_00f4167c
static constexpr float kMetersToYards = 1.0936133f;     // DAT_00f418bc
static constexpr float kKilometersToMiles = 0.6213712f; // DAT_00f418c0
static constexpr float kMaxDistanceInMeters = 1000.0f;  // DAT_00f41718

void FormatDistance( char *pDest, size_t destSize, float distanceInMeters ) {
    // FUN_006ca260
    if ( gpPlayerData->isUsingImperialUnits() ) {
        const float distanceInMiles =
            distanceInMeters * kMetersToKilometers * kKilometersToMiles;
        if ( distanceInMiles < 1.0f ) {
            snprintf( pDest, destSize, "%uyrd",
                      (uint32_t)( distanceInMeters * kMetersToYards ) );
        } else {
            snprintf( pDest, destSize, "%.3fmi", (double)distanceInMiles );
        }
    } else if ( distanceInMeters < kMaxDistanceInMeters ) {
        snprintf( pDest, destSize, "%um", (uint32_t)distanceInMeters );
    } else {
        snprintf( pDest, destSize, "%.3fkm",
                  (double)( distanceInMeters * kMetersToKilometers ) );
    }

    pDest[destSize - 1] = '\0';
}

const Font *GetActiveFont() {
    FontResources &fontResources = gpMngResources->getFontResources();
    if ( !fontResources.areFontsLoaded() ) {
        return nullptr;
    }
    return fontResources.getActiveFont();
}

MngFontPlayers::MngFontPlayers()
    : Manager(),
      bInitialized( false ),
      pString{},
      pDrawList( new DrawList() ),
      numPrimitives( 0 ),
      numVertices( 0 ),
      numIndices( 0 ) {
    // FUN_006ca530
    if ( gpMngFontPlayers == nullptr ) {
        gpMngFontPlayers = this;
        bInitialized = false;
        numPrimitives = 0x11;
        numVertices = 0x680;
        numIndices = 0x1a1;
    }
}

MngFontPlayers::~MngFontPlayers() {
    if ( gpMngFontPlayers == this ) {
        gpMngFontPlayers = nullptr;
    }

    delete pDrawList;
}

bool MngFontPlayers::initialize( TestDriveGameInstance * ) {
    // FUN_006ca190
    DrawStreams streams;
    streams.Normals = 0;
    streams.Specular = 0;
    streams.Tangent = 0;
    streams.Binormal = 0;
    streams.Diffuse = 1;
    streams.bIndex16 = 1;
    streams.UVMap = 1;

    return pDrawList->initialize( numPrimitives, numVertices, numIndices, true,
                                  &streams, "FontPlayersList", false );
}

void MngFontPlayers::draw() {
    // FUN_006cbbf0
    FontResources &fontResources = gpMngResources->getFontResources();
    if ( fontResources.areFontsLoaded() ) {
        pDrawList->reset();

        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

        pDrawList->setIdentityTransform();
        pDrawList->setActiveMaterial(
            fontResources.getRender2DM()->getMaterialAtIndex( 0 ) );

        if ( gpPlayerData->isOnline() ) {
            if ( gpPlayerData->getOnlineOp( 3 ) ) {
                drawPlayerNames();
                drawBotNames();
            }
        } else if ( gpPlayerData->getLocalPlayerData().bDispOP1 ) {
            // TODO: ONLINE (also requires !gGSAI.bOnlineSession)
            drawBotNames();
        }

        if ( gpMngSectorName != nullptr ) {
            gpMngSectorName->drawNames( gpTimer->SystemDeltaTime );
        }

        // TODO: CGMCompOutOfZone::DrawMessage (gCGMCompOutOfZone)
        OTDU_UNIMPLEMENTED_ONCE;
    }

    if ( pDrawList->getNumPrimitives() != 0 ) {
        gpRender->getRenderPass<eRenderPass::RP_2D>()
            .pScene->enqueueDynamicDrawList( pDrawList );
    }
}

void MngFontPlayers::terminate() {
    // FUN_006c9d10
    pDrawList->destroy();
}

void MngFontPlayers::drawText( float x,
                               float y,
                               float scale,
                               uint8_t r,
                               uint8_t g,
                               uint8_t b,
                               uint8_t a,
                               const char *pFormat,
                               ... ) {
    // FUN_006c9f70
    va_list args;
    va_start( args, pFormat );
    vsnprintf( pString, sizeof( pString ), pFormat, args );
    va_end( args );

    const Font *pFont = GetActiveFont();
    if ( pFont == nullptr ) {
        return;
    }

    Eigen::Vector4f pen( x / (float)gpRender->getWidth(),
                         y / (float)gpRender->getHeight(), 0.0f, 1.0f );

    pDrawList->setActiveMaterial( pFont->pMaterial );

    const int32_t numChar = (int32_t)strlen( pString );
    if ( !pDrawList->beginPrimitive( PT_TriangleStrip, numChar * 4 ) ) {
        return;
    }

    const uint32_t packedColor = ( (uint32_t)a << 24 ) | ( (uint32_t)r << 16 ) |
                                 ( (uint32_t)g << 8 ) | (uint32_t)b;
    pDrawList->setDiffuse( 0, ColorRGBA( packedColor ) );

    for ( int32_t i = 0; i < numChar; i++ ) {
        const float advance = pushGlyph( pString[i], pen, scale );
        pen.x() += advance;
    }

    pDrawList->commitPrimitive();
}

void MngFontPlayers::drawText3D( float x,
                                 float y,
                                 float z,
                                 float scale,
                                 uint8_t r,
                                 uint8_t g,
                                 uint8_t b,
                                 uint8_t a,
                                 const char *pFormat,
                                 ... ) {
    // FUN_006ca340
    va_list args;
    va_start( args, pFormat );
    vsnprintf( pString, sizeof( pString ), pFormat, args );
    va_end( args );

    const CameraGame *pCamera = gpCamera->getActiveCamera();
    if ( pCamera == nullptr ) {
        return;
    }

    Frustum &nearFrustum = gpRender->getRenderPass<eRenderPass::RP_Near>()
                               .pScene->getFrustumWrite();

    const Eigen::Vector4f viewSpacePos = TDUMat4x4MulVec(
        pCamera->getWorldToCam(), Eigen::Vector4f( x, y, z, 1.0f ) );

    if ( viewSpacePos.z() > 0.0f ) {
        return;
    }

    const Eigen::Vector4f clipSpacePos =
        TDUMat4x4MulVec( nearFrustum.getProjectionMatrix(), viewSpacePos );

    Eigen::Vector4f pen = clipSpacePos / clipSpacePos.w();
    pen.x() = ( pen.x() + 1.0f ) * 0.5f;
    pen.y() = ( 1.0f - pen.y() ) * 0.5f;

    const Font *pFont = GetActiveFont();
    if ( pFont == nullptr ) {
        return;
    }
    pDrawList->setActiveMaterial( pFont->pMaterial );

    const int32_t numChar = (int32_t)strlen( pString );
    if ( !pDrawList->beginPrimitive( PT_TriangleStrip, numChar * 4 ) ) {
        return;
    }

    const uint32_t packedColor = ( (uint32_t)a << 24 ) | ( (uint32_t)r << 16 ) |
                                 ( (uint32_t)g << 8 ) | (uint32_t)b;
    pDrawList->setDiffuse( 0, ColorRGBA( packedColor ) );

    for ( int32_t i = 0; i < numChar; i++ ) {
        const float advance = pushGlyph( pString[i], pen, scale );
        pen.x() += advance;
    }

    pDrawList->commitPrimitive();
}

float MngFontPlayers::getTextWidth( float scale, const char *pText ) const {
    // FUN_006ca090
    const Font *pFont = GetActiveFont();
    if ( pFont == nullptr ) {
        return 0.0f;
    }

    float totalAdvance = 0.0f;

    const int32_t numChar = (int32_t)strlen( pText );
    for ( int32_t i = 0; i < numChar; i++ ) {
        for ( int32_t glyphIndex = 0; glyphIndex < pFont->NumGlyphes;
              glyphIndex++ ) {
            const Font::Glpyh &glyph = pFont->pGlyphes[glyphIndex];
            if ( (uint32_t)(uint8_t)pText[i] == (uint32_t)glyph.ID ) {
                totalAdvance +=
                    glyph.GlyphBB[2] * kGlyphAdvanceScale + glyph.Scale;
                break;
            }
        }
    }

    return (float)gpRender->getWidth() * totalAdvance * scale *
           kInvReferenceWidth;
}

float MngFontPlayers::getTextWidthFormatted( float scale,
                                             const char *pFormat,
                                             ... ) {
    // FUN_006ca130
    va_list args;
    va_start( args, pFormat );
    vsnprintf( pString, sizeof( pString ), pFormat, args );
    va_end( args );

    return getTextWidth( scale, pString );
}

float MngFontPlayers::pushGlyph( char character,
                                 Eigen::Vector4f &pen,
                                 float scale ) {
    // FUN_006c9d40
    const Font *pFont = GetActiveFont();
    if ( pFont == nullptr ) {
        return 0.0f;
    }

    int32_t glyphIndex = 0;
    for ( ; glyphIndex < pFont->NumGlyphes; glyphIndex++ ) {
        if ( (uint32_t)(uint8_t)character ==
             (uint32_t)pFont->pGlyphes[glyphIndex].ID ) {
            break;
        }
    }

    if ( glyphIndex == pFont->NumGlyphes ) {
        return 0.0f;
    }

    const Texture *pAtlas = pFont->pMaterial->getTexture( 0, 0 );
    OTDU_ASSERT( pAtlas );
    if ( pAtlas == nullptr ) {
        return 0.0f;
    }

    const Font::Glpyh &glyph = pFont->pGlyphes[glyphIndex];

    const float invAtlasWidth = 1.0f / (float)pAtlas->Width;
    const float invAtlasHeight = 1.0f / (float)pAtlas->Height;

    const float u0 = glyph.GlyphBB[0] * invAtlasWidth;
    const float u1 = glyph.GlyphBB[2] * invAtlasWidth + u0;
    const float v0 = glyph.GlyphBB[1] * invAtlasHeight;
    const float v1 = ( glyph.GlyphBB[3] + glyph.GlyphBB[1] ) * invAtlasHeight;

    float glyphHeight = glyph.GlyphBB[3] * kInvReferenceHeight;
    if ( !gpConfig->b169Ratio ) {
        glyphHeight *= k43AspectCorrection;
    }

    const float left = pen.x() - glyph.GlyphPointXY[0] * kInvReferenceWidth;
    const float right = ( glyph.GlyphBB[2] * scale - glyph.GlyphPointXY[0] ) *
                            kInvReferenceWidth +
                        pen.x();
    const float bottom = glyphHeight * scale + pen.y();

    pDrawList->setTexCoords( 0, Eigen::Vector2f( u0, v0 ) );
    pDrawList->pushVertex( left, pen.y(), 0.0f );

    pDrawList->setTexCoords( 0, Eigen::Vector2f( u0, v1 ) );
    pDrawList->pushVertex( left, bottom, 0.0f );

    pDrawList->setTexCoords( 0, Eigen::Vector2f( u1, v0 ) );
    pDrawList->pushVertex( right, pen.y(), 0.0f );

    pDrawList->setTexCoords( 0, Eigen::Vector2f( u1, v1 ) );
    pDrawList->pushVertex( right, bottom, 0.0f );

    // Degenerate pair so the next glyph does not connect to this quad.
    pDrawList->breakPrimitive();

    return ( glyph.GlyphBB[2] * scale * kGlyphAdvanceScale +
             glyph.Scale * scale ) *
           kInvReferenceWidth;
}

void MngFontPlayers::drawPlayerNames() {
    // FUN_006caee0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

// CP2PGameService FreeRideMgr+0x4be0
struct BotNameLabel {
    float X;
    float Y;
    float Width;
    float Height;
    float Distance;
    int32_t Index;
};

static std::array<BotNameLabel, AiVehicleList::kCapacity> gBotNameLabels = {};

static constexpr float kBotNameHeightOffset = 2.0f; // DAT_00f416b0
static constexpr float kBotNameLowResScale = 1.3f;  // DAT_00f41dfc
static constexpr uint32_t kBotNameLowResHeight = 720;
static constexpr float kBotNameLineHeight = 16.0f;        // DAT_00f418d8
static constexpr float kBotNameHalfLineHeight = 8.0f;     // DAT_00f41ac4
static constexpr float kBotNameScreenOffset = 0.0694444f; // DAT_00f41ea8
static constexpr float kBotNameFadeDistance = 10.0f;      // FLOAT_00f4166c
static constexpr float kBotNameFadeSlope = 20.4f;         // DAT_00f41ea4
static constexpr float kBotNameFadeMin = 51.0f;           // DAT_00f41ea0
static constexpr int32_t kBotNameMaxLengthWithDistance = 20;
static constexpr uint8_t kBotNameOverlapAlpha = 0x33;

static inline AiVehicle *GetListVehicle( AiVehicleList &list,
                                         const int32_t index ) {
    return ( index < list.Count ) ? list.Items[index] : list.Items[0];
}

static inline bool IsBotNameVisible( const CarState *pCarState,
                                     const CarState *pLocalCarState ) {
    if ( pCarState == pLocalCarState || pCarState == nullptr ) {
        return false;
    }
    const PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
    return pPhysics != nullptr && pPhysics->isActive();
}

void MngFontPlayers::drawBotNames() {
    // FUN_006ca580
    FontResources &fontResources = gpMngResources->getFontResources();
    CarState *pLocalCarState = gpScenaric->getLocalPlayer().getCarState();
    if ( !fontResources.areFontsLoaded() || pLocalCarState == nullptr ||
         pLocalCarState->getPhysicsVehicle() == nullptr ||
         !pLocalCarState->getPhysicsVehicle()->isActive() ) {
        return;
    }
    fontResources.setActiveFont( fontResources.getDefaultFont() );

    Eigen::Vector4f referencePosition =
        pLocalCarState->getWorldMatrix().row( 3 ).transpose();
    referencePosition.w() = 1.0f;

    AiPool *pPool = gpAI->getAIPool();
    const int32_t botList = AiPool::AVL_FirstCategory +
                            static_cast<int32_t>( eAiVehicleCategory::AVC_Bot );
    const int32_t opponentList =
        AiPool::AVL_FirstCategory +
        static_cast<int32_t>( eAiVehicleCategory::AVC_Opponent );
    AiVehicleList *pList = nullptr;
    if ( pPool->getList( botList ).Count != 0 ) {
        pList = &pPool->getList( botList );
    } else if ( pPool->getList( opponentList ).Count != 0 ) {
        pList = &pPool->getList( opponentList );
    } else {
        return;
    }

    for ( BotNameLabel &label : gBotNameLabels ) {
        label.Index = -1;
    }

    const Matrix4x4f_t &worldToCam =
        gpCamera->getActiveVehicleCamera()->getWorldToCam();
    const Matrix4x4f_t &projection =
        gpRender->getRenderPass<eRenderPass::RP_Near>()
            .pScene->getFrustumWrite()
            .getProjectionMatrix();

    for ( int32_t i = 0; i < pList->Count; i++ ) {
        CarState *pCarState = GetListVehicle( *pList, i )->getCarState();
        if ( !IsBotNameVisible( pCarState, pLocalCarState ) ) {
            continue;
        }

        const Eigen::Vector4f position = pCarState->getPhysicsVehicle()
                                             ->getChassisWorldMatrix()
                                             .row( 3 )
                                             .transpose();
        const Eigen::Vector4f viewPosition = TDUMat4x4MulVec(
            worldToCam,
            Eigen::Vector4f( position.x(), position.y() + kBotNameHeightOffset,
                             position.z(), 1.0f ) );
        if ( viewPosition.z() < 0.0f ) {
            const Eigen::Vector4f delta =
                Eigen::Vector4f( position.x(), position.y(), position.z(),
                                 1.0f ) -
                referencePosition;
            gBotNameLabels[i].Distance = delta.norm();
            gBotNameLabels[i].Index = i;
        }
    }

    for ( int32_t i = 0; i < AiVehicleList::kCapacity - 1; i++ ) {
        for ( int32_t j = i + 1; j < AiVehicleList::kCapacity; j++ ) {
            const int32_t index = gBotNameLabels[i].Index;
            if ( index != -1 && gBotNameLabels[j].Index != -1 &&
                 gBotNameLabels[i].Distance > gBotNameLabels[j].Distance ) {
                gBotNameLabels[i].Index = gBotNameLabels[j].Index;
                gBotNameLabels[j].Index = index;
            }
        }
    }

    const float screenWidth = (float)gpRender->getWidth();
    const float screenHeight = (float)gpRender->getHeight();
    char pText[256];

    for ( int32_t i = 0; i < pList->Count; i++ ) {
        const int32_t index = gBotNameLabels[i].Index;
        if ( index == -1 ) {
            continue;
        }

        AiVehicle *pVehicle = GetListVehicle( *pList, index );
        CarState *pCarState = pVehicle->getCarState();
        if ( !IsBotNameVisible( pCarState, pLocalCarState ) ) {
            continue;
        }
        const char *pName = pVehicle->getDPC().pName;

        const Eigen::Vector4f position = pCarState->getPhysicsVehicle()
                                             ->getChassisWorldMatrix()
                                             .row( 3 )
                                             .transpose();
        const Eigen::Vector4f viewPosition = TDUMat4x4MulVec(
            worldToCam,
            Eigen::Vector4f( position.x(), position.y() + kBotNameHeightOffset,
                             position.z(), 1.0f ) );
        const Eigen::Vector4f clipPosition =
            TDUMat4x4MulVec( projection, viewPosition );
        const Eigen::Vector4f screenPosition = clipPosition / clipPosition.w();
        if ( !( viewPosition.z() < 0.0f ) ) {
            continue;
        }

        const int32_t nameLength = (int32_t)strlen( pName );
        const float screenX =
            ( screenPosition.x() + 1.0f ) * 0.5f * screenWidth;
        const float screenY =
            ( 1.0f - screenPosition.y() ) * 0.5f * screenHeight;
        const Eigen::Vector4f delta =
            Eigen::Vector4f( position.x(), position.y(), position.z(), 1.0f ) -
            referencePosition;

        const float scale = ( gpRender->getHeight() < kBotNameLowResHeight )
                                ? kBotNameLowResScale
                                : 1.0f;

        char pDistance[12];
        FormatDistance( pDistance, sizeof( pDistance ), delta.norm() );

        float width =
            getTextWidthFormatted( scale, "(%s)%s", pDistance, pName );
        float x = screenX - width * 0.5f;
        float y = screenY - kBotNameLineHeight;
        const bool bShowDistance = nameLength < kBotNameMaxLengthWithDistance;
        if ( x < 0.0f ) {
            x = 0.0f;
            if ( bShowDistance ) {
                snprintf( pText, sizeof( pText ), "\xbc%s(%s)", pName,
                          pDistance );
            } else {
                snprintf( pText, sizeof( pText ), "\xbc%s", pName );
            }
        } else if ( x + width > screenWidth ) {
            if ( bShowDistance ) {
                snprintf( pText, sizeof( pText ), "(%s)%s\xbe", pDistance,
                          pName );
            } else {
                snprintf( pText, sizeof( pText ), "%s\xbe", pName );
            }
            width = getTextWidth( scale, pText );
            x = screenWidth - width;
        } else if ( bShowDistance ) {
            snprintf( pText, sizeof( pText ), "(%s)%s", pDistance, pName );
        } else {
            snprintf( pText, sizeof( pText ), "%s", pName );
        }

        float screenOffset = screenHeight * kBotNameScreenOffset;
        if ( !gpConfig->b169Ratio ) {
            screenOffset *= k43AspectCorrection;
        }
        y -= screenOffset;

        uint8_t alpha = 0xff;
        float numOverlaps = 0.0f;
        for ( int32_t j = 0; j < i; j++ ) {
            const int32_t otherIndex = gBotNameLabels[j].Index;
            if ( otherIndex == -1 ) {
                continue;
            }
            const BotNameLabel &other = gBotNameLabels[otherIndex];
            const float gapX =
                std::abs( other.X - x ) - width * 0.5f - other.Width * 0.5f;
            const float gapY = std::abs( other.Y - y ) -
                               kBotNameHalfLineHeight - other.Height * 0.5f;
            const float gap = std::max( gapX, gapY );
            if ( gap <= 0.0f ) {
                y -= numOverlaps * kBotNameLineHeight;
                numOverlaps += 1.0f;
                alpha = kBotNameOverlapAlpha;
            } else if ( gap <= kBotNameFadeDistance ) {
                const float fade = gap * kBotNameFadeSlope + kBotNameFadeMin;
                alpha = (uint8_t)std::min( fade, (float)alpha );
            }
        }

        BotNameLabel &label = gBotNameLabels[index];
        label.X = x;
        label.Y = y;
        label.Width = width;
        label.Height = kBotNameLineHeight;

        const uint8_t vehicleAlpha =
            (uint8_t)( (float)alpha *
                       pCarState->getVehicleInstance()->getAlpha() );
        drawText( x, y, scale, 0xff, 0xff, 0xff, vehicleAlpha, "%s", pText );
    }
}
