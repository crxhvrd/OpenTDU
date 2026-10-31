#pragma once

#include "core/locale.h"
#include "core/mutex.h"
#include "core/notifier.h"
#include "game/gs_base.h"
#include "local_player_data.h"

#include "render/gs_render_helper.h"

class PlayerSave;
class IPlayerData;
class CarDesc;
struct AvatarConfiguration;

static constexpr const char *kPlayerSaveContextName =
    "playersave"; // DAT_00f7a038
static constexpr const char *kPlayerSave2ContextName = "playersave2";
extern char gActiveSaveContextName[128];
extern bool gbSaveAllowed;

enum eSaveLoadState : int32_t {
    SLS_None = 0,   // nothing was attempted yet
    SLS_Loaded = 1, // the active profile has a save and it was read back
    SLS_NoSave = 2, // the active profile has no (readable) save
};

struct PlayerSaveDescriptor;

class GSPlayerData : public GameSystem, public Notifiable {
public:
    const char *getName() const override { return "Service : PlayerData"; }
    inline bool getOnlineOp( int32_t param_1 ) const {
        return localPlayerData.bOnlineOps[param_1];
    }
    inline bool isUsingImperialUnits() const {
        return localPlayerData.UnitSystem == US_Imperial;
    }
    inline void setSaveEnabled( const bool value ) { bSaveEnabled = value; }
    inline bool isSaveEnabled() const { return bSaveEnabled; }
    inline float getSeatPositionHeight() const {
        return localPlayerData.SeatPositionHeight;
    }
    inline float getSeatPositionPosition() const {
        return localPlayerData.SeatPositionPosition;
    }
    inline float getPadSensitivity() const {
        return localPlayerData.PadSensitivity;
    }
    inline float getBarSensitivity() const {
        return localPlayerData.BarSensitivity;
    }
    inline float getMouseSensitivity() const {
        return localPlayerData.MouseSensitivity;
    }
    inline float getPadLinearity() const { return localPlayerData.Linearity; }
    inline float getQuiverSensitivity() const {
        return localPlayerData.QuiverSensitivity;
    }
    inline float getGamma() const { return localPlayerData.Gamma; }
    inline float getCameraShake() const { return localPlayerData.CameraShake; }
    inline float getRadialBlurIntensity() const {
        return localPlayerData.RadialBlurIntensity;
    }
    inline float getVoicesVolume() const {
        return localPlayerData.VoicesVolume;
    }
    inline float getVoicesThreshold() const {
        return localPlayerData.VoicesThreshold;
    }
    inline float getVoicesInput() const { return localPlayerData.VoicesInput; }
    inline bool isAutomaticGearbox() const { return localPlayerData.bAuto; }
    inline int32_t getDrivingHelpLevel() const {
        return localPlayerData.DrivingHelpLevel;
    }
    inline void setDrivingHelpLevel( const int32_t level ) {
        localPlayerData.DrivingHelpLevel = level;
    }

    inline bool hasHardcoreMode() const { return bHasHardcoreMode; }
    inline void setHasHardcoreMode( const bool bValue ) {
        bHasHardcoreMode = bValue;
    }

    inline LocalPlayerData &getLocalPlayerData() { return localPlayerData; }
    inline const LocalPlayerData &getLocalPlayerData() const {
        return localPlayerData;
    }
    inline IPlayerData *getLoadedSave() const { return pLoadedSave2; }
    inline void setOnline( const bool bOnline ) { bIsOnline = bOnline; }
    inline float getLastArrestedTime() const { return LastArrestedTime; }
    inline void setLastArrestedTime( const float time ) {
        LastArrestedTime = time;
    }
    inline void resetArrestState( const float lastArrestedTime ) {
        LastArrestedTime = lastArrestedTime;
        OffenceRepetitionFactor = 1u;
    }

public:
    GSPlayerData();
    ~GSPlayerData();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void pause() override;

    void setDefaultLanguage( const char *pDefaultLanguage,
                             bool bForceInit = false );
    void setDefaultSeatPosition( const float height, const float depth );

    void setLODQuality( const int32_t newValue );
    void setResolution( const int32_t width, const int32_t height );
    void setRefreshRate( const int32_t refreshRate );
    void setAntiAliasing( const eAntiAliasingMethod method );
    void setHDREnabled( const bool bEnabled );

    const char *getLanguage();
    void setHelpSystem( const bool bEnabled );
    eAntiAliasingMethod getAAMethod() const;
    bool isHDREnabled() const;

    void setUnitSystem( const bool bUseImperial );
    bool isOnline() const;

    void resetPlayerData( const bool bApplyDefaults, const bool bClear );
    void reapplyInputBindings(); // OTDU_ADDITION: input devices changed
    void newGame( const bool bInstallCheats );
    void setCurrentCar( const CarDesc *pCar );
    void selectCurrentCar( const CarDesc *pCar );
    void swapCars( const int32_t indexA,
                   const uint64_t houseHashcodeA,
                   const int32_t indexB,
                   const uint64_t houseHashcodeB );
    CarDesc *getCurrentCar();
    int32_t getSaveLoadState() const;
    bool loadActiveProfileSave();
    bool loadCommonDT( const void *pSaveBuffer,
                       const int32_t bufferLength,
                       LocalPlayerData *pOutPlayerData );
    bool readProfileOptions( const uint64_t profileHashcode,
                             LocalPlayerData &outPlayerData );
    void requestSave( const int32_t mode );
    void flushSaveRequest();
    void fillSaveDescriptor( PlayerSaveDescriptor *pDescriptor );
    inline TestDriveMutex *getMutex() { return pMutex; }
    void applyDrivingOptions();
    void prepareActiveProfile();
    int32_t writeCommonDT( void *pExternalBuffer,
                           const int32_t bufferLength,
                           LocalPlayerData *pPlayerData = nullptr );
    int32_t writePlayerSave( void *pExternalBuffer,
                             const int32_t bufferLength );
    bool FUN_00897990();
    bool isRentingCar();

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

private:
    uint8_t bInitialized : 1;
    uint8_t bForceUpdate : 1;
    uint8_t bTutorialCompleted : 1;
    uint8_t bIsOnline : 1;
    uint8_t bSaveEnabled : 1;
    uint8_t bHasHardcoreMode : 1;
    uint8_t field526864_0x967e5 : 1;

    float LastArrestedTime;
    uint32_t OffenceRepetitionFactor;

    PlayerSave *pLoadedSave;
    IPlayerData *pLoadedSave2;

    LocalPlayerData localPlayerData;

    std::vector<uint8_t> saveScratchBuffer;

    bool bGarageHasMissingCars;

    int32_t saveLoadState;
    int32_t pendingSaveRequest;
    int32_t saveVersion;

    float timeout;

    TestDriveMutex *pMutex;

private:
    void updateUnitSystem();
    void upgradeSave();
};

extern float gSeatPosition[2];

extern GSPlayerData *gpPlayerData;

namespace TestDrive {
    const AvatarConfiguration *GetSavedAvatarConfiguration();
    void RebakePlayerAvatar();
} // namespace TestDrive
