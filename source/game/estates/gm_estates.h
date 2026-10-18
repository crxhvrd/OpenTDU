#pragma once

#include "game/gm_world.h"

#include "game/garage/scene_garage.h"
#include "game/living/scene_living.h"
#include "render/mng_reflection.h"

#include "flash_list_houses.h"
#include "mng_houses.h"
#include "scene_estates.h"

struct FlashMessage;
class FlashPlayer;
class EntityCinematic;

// Realtor game mode (including virtual tour)
class GMEstates : public GameModeWorldBase {
public:
    enum eHouseTour : int32_t {
        HT_None = -1,
        HT_Outward = 0,
        HT_Living = 1,
        HT_Garage = 2,
        HT_Removal = 3,
    };

public:
    inline FlashPlayer *getFlashPlayer() const { return pFlashPlayer; }
    inline EstatesHouses &getHouses() { return houses; }
    inline SpotMap *getHouseSpot() const { return pHouseSpot; }
    inline void setRetakeHouse( const uint64_t hashcode ) {
        retakeHouseHashcode = hashcode;
    }

    static bool OnHousePictureLoaded( HousePictureBank *pBank,
                                      void *pUserData );

public:
    GMEstates();
    ~GMEstates();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override;
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;
    bool isDrawnInFakeDOF() override;

    void changeHouse();

private:
    static void OnTourFadeOut( void *pData, void *pSource );
    static void OnTourExitFadeOut( void *pData, void *pSource );
    static void OnBuyCinematicFadeOut( void *pData, void *pSource );

    void initializeHouseList();
    void loadRealtorScene( const bool bInstallIntro );
    void onRealtorSceneReady();
    bool isRealtorSceneLive();
    bool isHouseStreamed( const Eigen::Vector4f &position );
    void tickHouseVisit();
    bool launchTour( const eHouseTour tour );
    void startTour( const eHouseTour tour );
    void leaveRealtorScene();
    void placeRemovalCar();
    void updateLocationMarker();
    void updateMoneyState();
    void refreshHouseList();
    void relocateRetakeHouseCars( const uint64_t houseHashcode );

    void onRealtorReset();
    void onBuyHouse();
    void onLocateRealtor();
    void onTourIn();
    void onTourOut();
    void onCheckSellCapacity();
    void onSellHouse();

private:
    FlashPlayer *pFlashPlayer;
    SceneEstates sceneEstates;
    int32_t savedSpotIndex;
    SpotMap *pHouseSpot;
    bool bTourTransition;
    eHouseTour tour;
    bool bStreamToTarget;
    bool bLaunchSellCine;
    Eigen::Vector4f targetPosition;
    bool bInfosLoadedPending;
    bool bExitToFreeride;
    bool bRemovalRequested;
    bool bTourRequested;
    EntityCinematic *pBuyCinematic;
    bool bInstallIntro;
    EntityCinematic *pTourCinematic;
    SceneLiving sceneLiving;
    SceneGarage sceneGarage;
    EstatesHouses houses;
    uint64_t retakeHouseHashcode;
    MngReflection mngReflection;
    FlashListHouses houseList;
    Eigen::Vector3f savedReflectionNormal;
    float savedReflectionOffset;
    float worldSizeX;
    float worldSizeZ;
};
