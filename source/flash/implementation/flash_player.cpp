#include "shared.h"
#include "flash_player.h"

#include "flash/gs_flash.h"
#include "flash_resource.h"
#include "flash_tags.h"

#include "core/time.h"

FlashPlayer::FlashPlayer( uint8_t *param_1,
                          uint32_t param_2,
                          edFlashInput *param_4,
                          edFlashConfig *param_5 )
    : pFlashResource( nullptr ) {
    // FUN_00440880
    if ( param_2 < sizeof( FlashResource ) || param_1 == nullptr ) {
        OTDU_LOG_WARN( "FlashPlayer created without a .vmf bound (instance "
                       "will be in an invalid state)\n" );
        return;
    }

    memset( param_1, 0, sizeof( FlashResource ) );
    void *pMemPool = (uint8_t *)param_1 + sizeof( FlashResource );
    uint32_t size = param_2 - sizeof( FlashResource );
    pFlashResource =
        new ( param_1 ) FlashResource( pMemPool, size, this, param_4, param_5 );
}

FlashPlayer::~FlashPlayer() {}

void FlashPlayer::showFrame( const char *param_1,
                             const char *param_2,
                             bool param_3 ) {
    // FUN_00441090
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar4 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;

    FlashSprite *pFVar2 = gpActiveFlashResource->getRoot();
    FlashClip &pFVar1 = gpActiveFlashResource->getActiveClip();
    FlashSprite *pFVar3 = pFVar1.pSprite;

    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->getDisplayList()->getAction().pParent =
        pFVar2;
    pFVar1.pSprite = pFVar2;

    bool bVar5 = pFVar1.retrieveSpriteFromPath( param_2, nullptr, nullptr );
    if ( bVar5 ) {
        flash::actions::gotoAndStop( param_1 );
        if ( param_3 ) {
            flash::actions::play();
        } else {
            flash::actions::stop();
        }
    }

    gpActiveFlashResource->getActiveClip().pSprite = pFVar3;
    gpPreviousActiveFlashResource = pFVar4;
    gpActiveFlashResource = pFVar4;
}

void FlashPlayer::destroy() {
    // FUN_00443190
    FlashResource *pFVar2 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    pFlashResource->destroy();
    FlashResource *pFVar1 = pFlashResource;
    gpPreviousActiveFlashResource = pFVar2;
    gpActiveFlashResource = pFVar2;
    if ( pFVar1 != nullptr ) {
        pFVar1->destroyInternalState();
        pFVar1->destroyRenderer();
        setBoundFlashResource( nullptr );
    }
}

void FlashPlayer::setVariableValue( const char *param_1, const char *param_2 ) {
    // FUN_00440ab0
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->setVariable( param_1, param_2 );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

void FlashPlayer::setVariableValueNonLinked( const char *param_1,
                                             const char *param_2 ) {
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = getBoundFlashResource();
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->setVariableNonLinked( param_1, param_2 );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

void FlashPlayer::setVariableValueWritableLinked( const char *param_1,
                                                  char *param_2 ) {
    // FUN_00440d00
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->setVariableWritableLinked( param_1,
                                                                 param_2 );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

void FlashPlayer::setVariableValue( const char *param_1,
                                    const int32_t param_2 ) {
    // FUN_00440ba0
    ScopedMutexLock lock = gpFlash->scopedLock();

    char pBuffer[32];
    sprintf( pBuffer, "%d", param_2 );

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->setVariableNonLinked( param_1, pBuffer );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

void FlashPlayer::setVariableValue( const char *param_1,
                                    const uint32_t param_2 ) {
    // FUN_00440c50
    ScopedMutexLock lock = gpFlash->scopedLock();

    char pBuffer[32];
    sprintf( pBuffer, "%u", param_2 );

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->setVariableNonLinked( param_1, pBuffer );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

void FlashPlayer::setVariableValue( const char *param_2, const float param_3 ) {
    // FUN_00440b50
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->setVariable( param_2, param_3 );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

void FlashPlayer::setVariableValueVariadic( const char *param_1,
                                            const char *param_2,
                                            ... ) {
    // FUN_00440d50
    ScopedMutexLock lock = gpFlash->scopedLock();

    va_list argList;
    va_start( argList, param_2 );
    char pBuffer[32];
    vsnprintf( pBuffer, sizeof( pBuffer ), param_2, argList );
    va_end( argList );

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getRoot()->setVariableNonLinked( param_1, pBuffer );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

int32_t FlashPlayer::getVariableAsInt( const char *param_1 ) {
    // FUN_00440e00
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar2 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = pFlashResource;

    FlashSprite *pActiveSprite = pFlashResource->getRoot();

    const char *local_4 = nullptr;
    FlashSprite *local_8 = nullptr;
    bool bVar3 = pActiveSprite->resolveVarPath( param_1, &local_4, &local_8 );

    char local_20[32];
    bool bVar6 = false;
    if ( bVar3 ) {
        bVar6 = pActiveSprite->getVariables()->getVariableAsString(
            local_20, local_8->getInstanceID(), local_4 );
    }
    gpPreviousActiveFlashResource = pFVar2;
    gpActiveFlashResource = pFVar2;

    if ( bVar6 ) {
        int32_t iVar5 = atoi( local_20 );
        return iVar5;
    }

    return 0;
}

bool FlashPlayer::getVariableAsFloat( const char *pVariableName,
                                      float &outValue ) {
    // FUN_00440a50
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pPreviousResource = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = pFlashResource;

    FlashSprite *pRoot = pFlashResource->getRoot();
    const char *pResolvedName = nullptr;
    FlashSprite *pOwner = nullptr;
    bool bFound = false;
    outValue = 0.0f;
    // FUN_00448fd0 (inlined)
    if ( pRoot->resolveVarPath( pVariableName, &pResolvedName, &pOwner ) ) {
        ActionScriptVariable *pVariable =
            pRoot->getVariables()->retrieveVariable( pResolvedName,
                                                     pOwner->getInstanceID() );
        if ( pVariable != nullptr ) {
            bFound = true;
            switch ( pVariable->Type ) {
            case eFlashStackVarType::FSVT_String:
                outValue = FlashResource::StringToFloat( pVariable->pString );
                break;
            case eFlashStackVarType::FSVT_LinkedString:
            case eFlashStackVarType::FSVT_WritableLinkedString:
                outValue =
                    FlashResource::StringToFloat( pVariable->pStringPointer );
                break;
            default:
                outValue = pVariable->Float;
                break;
            }
        }
    }

    gpPreviousActiveFlashResource = pPreviousResource;
    gpActiveFlashResource = pPreviousResource;
    return bFound;
}

bool FlashPlayer::getVariableAsString( const char *param_2, char *param_3 ) {
    // FUN_004409c0
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar2 = gpActiveFlashResource;
    FlashResource *pFVar1 = pFlashResource;
    gpActiveFlashResource = pFVar1;
    gpPreviousActiveFlashResource = pFVar1;

    FlashSprite *param_1 = nullptr;
    bool bVar3 =
        pFVar1->getRoot()->resolveVarPath( param_2, &param_2, &param_1 );
    bool uVar5 = false;
    if ( bVar3 ) {
        uVar5 = pFVar1->getRoot()->getVariables()->getVariableAsString(
            param_3, param_1->getInstanceID(), param_2 );
    }

    gpPreviousActiveFlashResource = pFVar2;
    gpActiveFlashResource = pFVar2;

    return uVar5;
}

bool FlashPlayer::getClipPlayer( const char *param_2, FlashClip *param_1 ) {
    // FUN_00441180
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    param_1->pSprite = gpActiveFlashResource->getRoot();
    param_1->pParent = pFlashResource;
    bool bVar2 = param_1->retrieveSpriteFromPath( param_2, nullptr, nullptr );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
    if ( !bVar2 ) {
        param_1->pSprite = nullptr;
        param_1->pParent = nullptr;
        return false;
    }

    return true;
}

void FlashPlayer::goToFrameAndPlay( int32_t param_2,
                                    const char *param_3,
                                    bool param_4 ) {
    // FUN_00440ff0
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar3 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    FlashSprite *pFVar2 = ( gpActiveFlashResource->getActiveClip() ).pSprite;
    FlashClip *pFVar1 = &gpActiveFlashResource->getActiveClip();
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    pFVar1->pSprite = gpActiveFlashResource->getRoot();

    bool bVar4 = pFVar1->retrieveSpriteFromPath( param_3, nullptr, nullptr );
    if ( bVar4 ) {
        uint16_t frameIndex = param_2;
        flash::actions::goToFrameAndStop( &frameIndex );
        if ( param_4 == false ) {
            flash::actions::stop();
        } else {
            flash::actions::play();
        }
    }

    ( gpActiveFlashResource->getActiveClip() ).pSprite = pFVar2;
    gpPreviousActiveFlashResource = pFVar3;
    gpActiveFlashResource = pFVar3;
}

void FlashPlayer::stopClip( const char *param_2 ) {
    // FUN_00440eb0
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar3 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    FlashSprite *pFVar2 = ( gpActiveFlashResource->getActiveClip() ).pSprite;
    FlashClip *pFVar1 = &gpActiveFlashResource->getActiveClip();
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    pFVar1->pSprite = gpActiveFlashResource->getRoot();

    bool bVar4 = pFVar1->retrieveSpriteFromPath( param_2, nullptr, nullptr );
    if ( bVar4 ) {
        flash::actions::stop();
    }

    ( gpActiveFlashResource->getActiveClip() ).pSprite = pFVar2;
    gpPreviousActiveFlashResource = pFVar3;
    gpActiveFlashResource = pFVar3;
}

void FlashPlayer::tick( uint32_t param_1, bool param_2, bool param_3 ) {
    // FUN_004408d0
    FlashResource *pFVar1 = pFlashResource;

    float fVar2 = GetDeltaTimeSeconds();
    float fVar3 = ( param_2 ) ? 0.0f : fVar2 - pFVar1->getTime();

    pFVar1->tick( fVar3, fVar2, param_1 );
    if ( param_3 ) {
        pFVar1->draw();
    }
}

bool FlashPlayer::spriteExists( const char *param_2 ) {
    // FUN_00441120
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    FlashClip clip;
    clip.pSprite = gpActiveFlashResource->getRoot();
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    bool bVar2 = clip.retrieveSpriteFromPath( param_2, nullptr, nullptr );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;

    return bVar2;
}

bool FlashPlayer::FUN_00441200( const char *param_2 ) {
    // FUN_00441200
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    bool bVar2 = pFlashResource->getFlashUnknown()->FUN_0044a9f0( param_2 );
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;

    return bVar2;
}

void FlashPlayer::releaseTextInputFocus() {
    // FUN_00441250
    ScopedMutexLock lock = gpFlash->scopedLock();

    FlashResource *pFVar1 = gpActiveFlashResource;
    gpActiveFlashResource = pFlashResource;
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    gpActiveFlashResource->getFlashUnknown()->FUN_0044ab50();
    gpPreviousActiveFlashResource = pFVar1;
    gpActiveFlashResource = pFVar1;
}

class ScopedFlashParentClip {
public:
    ScopedFlashParentClip( FlashResource *pResource, const char *pParentPath )
        : pPreviousResource( gpActiveFlashResource ),
          activeClip( pResource->getActiveClip() ),
          pPreviousSprite( activeClip.pSprite ),
          pParent( nullptr ) {
        gpActiveFlashResource = pResource;
        gpPreviousActiveFlashResource = pResource;

        activeClip.pSprite = pResource->getRoot();
        if ( activeClip.retrieveSpriteFromPath( pParentPath, nullptr,
                                                nullptr ) ) {
            pParent = activeClip.pSprite;
        }
    }

    ~ScopedFlashParentClip() {
        activeClip.pSprite = pPreviousSprite;
        gpPreviousActiveFlashResource = pPreviousResource;
        gpActiveFlashResource = pPreviousResource;
    }

    FlashSprite *getParent() const { return pParent; }

    FlashDisplayListEntry *findEntry( const uint16_t depth ) const {
        if ( pParent == nullptr ) {
            return nullptr;
        }

        FlashDisplayListEntry *pEntry =
            pParent->getDisplayList()->getFirstEntry();
        while ( pEntry != nullptr && pEntry->ObjectIndex != depth ) {
            pEntry = pEntry->pNext;
        }
        return pEntry;
    }

private:
    FlashResource *pPreviousResource;
    FlashClip &activeClip;
    FlashSprite *pPreviousSprite;
    FlashSprite *pParent;
};

// Same layout as a PlaceObject2 with character, full matrix, color transform,
// ratio and name.
struct FlashClonePlaceObject : public FlashTagPlaceObject2Data {
    float Scale[2];
    float Rotate[2];
    float Translate[2];
    CXForm ColorTransform;
    uint16_t Ratio;
    char pName[32];
};
OTDU_SIZE_MUST_MATCH( FlashClonePlaceObject, 0x5c );

// Same layout as a PlaceObject2 moving an existing object, with a color
// transform only.
struct FlashColorPlaceObject : public FlashTag {
    uint16_t Index;
    uint16_t Flags;
    CXForm ColorTransform;
};
OTDU_SIZE_MUST_MATCH( FlashColorPlaceObject, 0x1c );

// Same layout as a PlaceObject2 moving an existing object, with a full matrix
// only.
struct FlashMatrixPlaceObject : public FlashTag {
    uint16_t Index;
    uint16_t Flags;
    float Matrix[6];
};
OTDU_SIZE_MUST_MATCH( FlashMatrixPlaceObject, 0x24 );

static constexpr uint16_t kPlaceObject2TagId = 0x1a;

bool FlashPlayer::cloneObject( const char *pParentPath,
                               const FlashCloneDesc &desc ) {
    static constexpr uint16_t kCloneFlags =
        0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0080 | 0x0100 | 0x0200;

    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip parentClip( pFlashResource, pParentPath );
    FlashSprite *pParent = parentClip.getParent();
    if ( pParent == nullptr ) {
        return false;
    }

    FlashDisplayListEntry *pSource = pParent->getDisplayList()->getFirstEntry();
    while ( pSource != nullptr ) {
        const bool bMatch =
            ( desc.pSourceName != nullptr )
                ? strcmp( pSource->pName, desc.pSourceName ) == 0
                : pSource->ObjectIndex == desc.SourceDepth;
        if ( bMatch ) {
            break;
        }
        pSource = pSource->pNext;
    }

    if ( pSource == nullptr ) {
        return false;
    }

    const float *pMatrix = pSource->InstanceTransform.ScalarsFloat;

    FlashClonePlaceObject clone = {};
    clone.TagId = kPlaceObject2TagId;
    clone.TagSize = sizeof( FlashClonePlaceObject );
    clone.Index = desc.Depth;
    clone.Flags = kCloneFlags;
    clone.Unknown = uint16_t( pSource->Index );
    clone.Scale[0] = pMatrix[0];
    clone.Scale[1] = pMatrix[1];
    clone.Rotate[0] = pMatrix[2];
    clone.Rotate[1] = pMatrix[3];
    clone.Translate[0] = pMatrix[4];
    clone.Translate[1] = pMatrix[5];
    if ( ( pSource->InstanceTransform.Flags & 1 ) != 0 ) {
        clone.ColorTransform = pSource->InstanceTransform.Transform;
    }
    clone.Ratio = uint16_t( pSource->UnknownDword );
    strncpy( clone.pName, desc.pCloneName, sizeof( clone.pName ) - 1 );

    if ( desc.Mirror != eFlashCloneMirror::FCM_None ) {
        clone.Translate[0] = 2.0f * desc.MirrorAxisX - pMatrix[4];
    }
    if ( desc.Mirror == eFlashCloneMirror::FCM_Geometry ) {
        clone.Scale[0] = -pMatrix[0];
        clone.Rotate[1] = -pMatrix[3];
    }
    clone.Translate[0] += desc.OffsetX;
    clone.Translate[1] += desc.OffsetY;

    if ( pParent->getDisplayList()->enqueuePlaceObjectTag( &clone ) ) {
        pParent->increaseNumObjects();
    }

    return true;
}

void FlashPlayer::removeObject( const char *pParentPath, uint16_t depth ) {
    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip parentClip( pFlashResource, pParentPath );
    if ( parentClip.findEntry( depth ) == nullptr ) {
        return;
    }

    FlashSprite *pParent = parentClip.getParent();
    pParent->getDisplayList()->enqueueRemoveObjectTag( depth, pFlashResource );
    pParent->decreaseNumObjects();
}

bool FlashPlayer::getObjectColorTransform( const char *pParentPath,
                                           uint16_t depth,
                                           CXForm *pColorTransform ) {
    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip parentClip( pFlashResource, pParentPath );
    const FlashDisplayListEntry *pEntry = parentClip.findEntry( depth );
    if ( pEntry == nullptr || ( pEntry->InstanceTransform.Flags & 1 ) == 0 ) {
        return false;
    }

    *pColorTransform = pEntry->InstanceTransform.Transform;
    return true;
}

void FlashPlayer::setObjectColorTransform( const char *pParentPath,
                                           uint16_t depth,
                                           const CXForm &colorTransform ) {
    static constexpr uint16_t kMoveColorFlags = 0x0001 | 0x0008;

    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip parentClip( pFlashResource, pParentPath );
    if ( parentClip.findEntry( depth ) == nullptr ) {
        return;
    }

    FlashColorPlaceObject move = {};
    move.TagId = kPlaceObject2TagId;
    move.TagSize = sizeof( FlashColorPlaceObject );
    move.Index = depth;
    move.Flags = kMoveColorFlags;
    move.ColorTransform = colorTransform;

    parentClip.getParent()->getDisplayList()->enqueuePlaceObjectTag(
        (FlashTagPlaceObject2Data *)&move );
}

bool FlashPlayer::getObjectMatrix( const char *pParentPath,
                                   uint16_t depth,
                                   float pMatrix[6] ) {
    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip parentClip( pFlashResource, pParentPath );
    const FlashDisplayListEntry *pEntry = parentClip.findEntry( depth );
    if ( pEntry == nullptr ) {
        return false;
    }

    memcpy( pMatrix, pEntry->InstanceTransform.ScalarsFloat,
            sizeof( float ) * 6 );
    return true;
}

void FlashPlayer::setObjectMatrix( const char *pParentPath,
                                   uint16_t depth,
                                   const float pMatrix[6] ) {
    static constexpr uint16_t kMoveMatrixFlags =
        0x0001 | 0x0004 | 0x0080 | 0x0100 | 0x0200;

    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip parentClip( pFlashResource, pParentPath );
    if ( parentClip.findEntry( depth ) == nullptr ) {
        return;
    }

    FlashMatrixPlaceObject move = {};
    move.TagId = kPlaceObject2TagId;
    move.TagSize = sizeof( FlashMatrixPlaceObject );
    move.Index = depth;
    move.Flags = kMoveMatrixFlags;
    memcpy( move.Matrix, pMatrix, sizeof( move.Matrix ) );

    parentClip.getParent()->getDisplayList()->enqueuePlaceObjectTag(
        (FlashTagPlaceObject2Data *)&move );
}

void FlashPlayer::setClipProperty( const char *pClipPath,
                                   const eFlashProperty property,
                                   const float value ) {
    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip targetClip( pFlashResource, pClipPath );
    if ( targetClip.getParent() == nullptr ) {
        return;
    }

    FlashClip clip;
    clip.pSprite = targetClip.getParent();
    clip.pParent = pFlashResource;
    clip.setProperty( property, value );
}

bool FlashPlayer::isClipAtLabel( const char *pClipPath, const char *pLabel ) {
    ScopedMutexLock lock = gpFlash->scopedLock();
    ScopedFlashParentClip targetClip( pFlashResource, pClipPath );
    FlashSprite *pClip = targetClip.getParent();
    if ( pClip == nullptr ) {
        return false;
    }

    FlashDisplayList *pDisplayList = pClip->getDisplayList();
    const int32_t labelFrame = pClip->findLabel( pLabel );
    return labelFrame > 0 &&
           pDisplayList->getCurrentFrameIndex() == labelFrame &&
           !pDisplayList->hasPendingFrameChange();
}
