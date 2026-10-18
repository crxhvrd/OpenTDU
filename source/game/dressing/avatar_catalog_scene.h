#pragma once

#include "filesystem/bank.h"
#include "game/avatar/avatar_layer.h"
#include "game/avatar/avatar_configuration.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scene_interior.h"

const char *GetBrandVariableName( const char *pBrandName );

class AvatarClotheCursor;
class AvatarPartDB;
class CameraGame;
class EntityAvatar;
class EntityCamera;
class FlashPlayer;
struct AvatarClothePart;
struct FlashMessage;

// Avatar clothes catalog shown in an interior
class AvatarCatalogScene {
public:
    static constexpr int32_t kNoCategory = AC_Count;

public:
    inline SceneInterior &getScene() { return scene; }
    inline EntityAvatar *getAvatar() const { return pAvatar; }
    inline FlashPlayer *getFlashPlayer() const { return pFlashPlayer; }

public:
    AvatarCatalogScene();
    virtual ~AvatarCatalogScene();

    virtual void initialize();
    virtual void destroy();
    virtual void reset();
    virtual void pause();
    virtual void tick( float deltaTime, float totalTime );
    virtual void onInteriorReady();
    virtual bool onMessage( FlashMessage *pMessage );
    virtual AvatarClotheCursor *createCursor( AvatarPartDB *pDB,
                                              int32_t category,
                                              int32_t gender,
                                              const char *pBrand,
                                              int32_t clotheSet );
    virtual void syncCursorsToAvatar( EntityAvatar *pTargetAvatar );
    virtual void stepItem( int32_t clotheSet,
                           int32_t category,
                           const FlashMessage &message );
    virtual int32_t countItems( AvatarPartDB *pDB,
                                int32_t gender,
                                int32_t category,
                                const char *pBrand,
                                int32_t clotheSet,
                                int32_t *pOutNumPlaceholders );
    virtual int32_t indexOfItem( AvatarPartDB *pDB,
                                 const AvatarClothePart *pPart,
                                 int32_t clotheSet );
    virtual bool resetColor( const AvatarClothePart *pPart,
                             uint8_t *pColorIndex );
    virtual bool nextColor( const AvatarClothePart *pPart,
                            uint8_t *pColorIndex,
                            uint8_t numColors );
    virtual void resetToSavedOutfit();
    virtual void refreshChangeColorVariable();

protected:
    static bool IsPlaceholderMesh( const char *pMeshName );
    static void PlaceCameraOnEntity( CameraGame *pCamera,
                                     const EntityCamera *pCameraEntity );

    void setInteriorEnvMap( EntityAvatar *pTargetAvatar );
    EntityCamera *findSelectedCamera();
    void setGameCamera( const EntityCamera *pCameraEntity );
    void loadInteriorBank();

    int32_t getCurrentCategory();
    int32_t getCurrentClotheSet();
    int32_t getAvatarGender( EntityAvatar *pTargetAvatar ) const;
    AvatarClothePart *getActiveItem( int32_t category ) const;
    void applyActiveItem( int32_t clotheSet, int32_t category );
    bool applyNextColor( int32_t clotheSet, int32_t category );
    void restoreSavedItem( int32_t clotheSet, int32_t category );
    bool buyActiveItem( int32_t category );
    void updateItemVariables( int32_t category );
    void updateItemCounts();
    void updateIsDressVariable();
    void updateHelpVariable( int32_t category );
    void createCursors();
    void releaseCursors();
    RenderAnm *findHeelAnim();

protected:
    SceneInterior scene;
    Bank interiorBank;
    ScenaricBank heelLayerBank;
    AvatarHeelLayer heelLayer;
    AvatarHelmetLayer helmetLayer;

    FlashPlayer *pFlashPlayer;
    char movieName[0x20];
    char frameLabel[0x20];
    char helpParameter[0x20];

    const char *pBrandFilter;
    EntityAvatar *pAvatar;
    bool bSavedSeating;
    bool bIgnoreOwnership;
    AvatarPartDB *pClothesDB;

    AvatarClotheCursor *cursors[2][kNumClotheTypes][AC_Count];
    AvatarClotheCursor *activeCursors[AC_Count];
    uint8_t colors[2][kNumClotheTypes][AC_Count];
    uint8_t *activeColors[AC_Count];
};
