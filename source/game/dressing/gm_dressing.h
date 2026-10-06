#pragma once

#include "game/gm_scene.h"

#include "avatar_catalog_scene.h"
#include "flash/entities/flash_slider.h"
#include "render/mng_env_map.h"
#include "render/mng_reflection.h"

class AvatarGenderCursor;
class CameraGame;
class EntityAvatar;
class EntityCinematic;

class GMDressing : public GameModeScene, public AvatarCatalogScene {
public:
    GMDressing();
    ~GMDressing();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;
    bool onMessage( FlashMessage *pMessage ) override;

    void onInteriorReady() override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif
    AvatarClotheCursor *createCursor( AvatarPartDB *pDB,
                                      int32_t category,
                                      int32_t gender,
                                      const char *pBrand,
                                      int32_t clotheSet ) override;
    void syncCursorsToAvatar( EntityAvatar *pTargetAvatar ) override;
    int32_t countItems( AvatarPartDB *pDB,
                        int32_t gender,
                        int32_t category,
                        const char *pBrand,
                        int32_t clotheSet,
                        int32_t *pOutNumPlaceholders ) override;
    int32_t indexOfItem( AvatarPartDB *pDB,
                         const AvatarClothePart *pPart,
                         int32_t clotheSet ) override;
    bool resetColor( const AvatarClothePart *pPart,
                     uint8_t *pColorIndex ) override;
    bool nextColor( const AvatarClothePart *pPart,
                    uint8_t *pColorIndex,
                    uint8_t numColors ) override;
    void resetToSavedOutfit() override;
    void refreshChangeColorVariable() override;

private:
    AvatarClotheCursor *newListCursor( AvatarPartDB *pDB,
                                       int32_t gender,
                                       int32_t category,
                                       int32_t clotheSet ) const;
    void setHeadMode( bool bHeadMode );
    void placeAvatarsOnEntryNode();
    void restoreAvatarAfterIntro();
    bool startIntroCinematic( struct SpotMap *pSpot,
                              EntityCinematic *pCinematic );
    void placeCameraOnNode( CameraGame *pCamera, const char *pNodeName );

    void createEditorCursors();
    void releaseEditorCursors();
    void syncEditorCursors( EntityAvatar *pTargetAvatar, int32_t gender );
    void bindEditorSliders( EntityAvatar *pTargetAvatar, int32_t gender );
    bool focusFaceSlider( int32_t index );
    void unfocusFaceSlider();
    void tickEditorSliders( float deltaTime );
    void tickTintSlider( FlashSlider &slider,
                         int32_t category,
                         AvatarClotheCursor *pCursor,
                         float deltaTime );
    void stepPart( const FlashMessage &message,
                   const FlashMessage &nextMessage,
                   const FlashMessage &previousMessage,
                   int32_t category,
                   AvatarClotheCursor *pCursor,
                   FlashSlider &tintSlider,
                   const char *pGaugePath,
                   const char *pNameVariable );
    void stepEyes( const FlashMessage &message );
    void revertFaceAndHair();
    void refreshBikeGearVariable();
    void applyDebugAvatarConfiguration( const AvatarConfiguration &config );

private:
    MngReflection mngReflection;
    MngEnvMap mngEnvMap;

    float savedNearPlane;
    char cameraNodeName[0xb];

    EntityAvatar *pPreviewAvatar;
    EntityAvatar *pOriginalAvatar;
    EntityAvatar *pOriginalPreviewAvatar;
    bool bAvatarSwapPending;

    EntityCinematic *pIntroCinematic;

    AvatarPoseLayer poseLayer;
    AvatarLookAtLayer lookAtLayer;
    AvatarHeelLayer previewHeelLayer;
    AvatarHelmetLayer previewHelmetLayer;
    AvatarPoseLayer previewPoseLayer;
    AvatarLookAtLayer previewLookAtLayer;
    FlashSlider faceSliders[kNumFaceParams];
    FlashSlider *pFocusedFaceSlider;
    FlashSlider piloTintSlider;
    FlashSlider eyebrowTintSlider;
    FlashSlider hairTintSlider;
    FlashSlider skinSlider;

    AvatarClotheCursor *hairCursors[kNumGenders];
    AvatarClotheCursor *pActiveHairCursor;
    AvatarClotheCursor *piloCursors[kNumGenders];
    AvatarClotheCursor *pActivePiloCursor;
    AvatarClotheCursor *eyebrowCursors[kNumGenders];
    AvatarClotheCursor *pActiveEyebrowCursor;
    AvatarGenderCursor *eyeCursors[kNumGenders];
    AvatarGenderCursor *pActiveEyeCursor;

    std::vector<std::string> debugPNJConfigNames;
    int32_t debugPNJConfigIndex;

    bool bIntroIsInteriorCinematic;
    bool bUIPausedForIntro;
    bool bIntroSavedSeating;
    bool bIntroSavedFreeMovement;
    bool bIntroSavedUsedIK;
};
