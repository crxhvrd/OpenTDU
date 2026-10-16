#pragma once

#include "shared.h"

#include "game/gs_base.h"
#include "game/scenaric/scenaric_bank.h"
#include "render/gs_render_helper.h"
#include "render/mng_env_map.h"
#include "filesystem/bank.h"
#include "avatar_bnk.h"
#include "avatar_configuration.h"
#include "avatar_part.h"
#include "avatar_pool.h"

class EntityAvatar;
struct DBEntryPNJ;

class GSAvatar : public GameSystem, public EnvmapUnknown {
public:
    static constexpr int32_t kNumIdentityBones = 256;

public:
    const char *getName() const override {
        // FUN_00410840
        return "Service : Avatar";
    }

    inline const AvatarConfiguration &
    getDefaultConfiguration( int32_t gender ) const {
        return pDefaultConfig[gender];
    }

    inline AvatarBnkManager &getBnkManager() { return bankManager; }
    inline AvatarPartDB *getClothesDB() const { return pClothesDB; }
    inline AvatarPartDB *getSkinDB() const { return pSkinDB; }
    inline AvatarPartDB *getEyeDB() const { return pEyeDB; }
    inline int32_t getNumStyles() {
        return styleBank.isLoaded() ? styleBank.getNumElements() : 0;
    }
    inline AvatarPool &getPool() { return pool; }
    inline Matrix4x4f_t *getIdentityBonePalette( int32_t index ) {
        return pBones[index];
    }
    inline class EntityAnmSet *getFaceModifSet() const { return pFaceModifSet; }
    inline const Eigen::Vector2f &getFaceRange( int32_t gender,
                                                int32_t index ) const {
        return pFaceRange[gender][index];
    }

public:
    GSAvatar();
    ~GSAvatar();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;

    bool onEnvMapChanged( int32_t param_1 ) override;

    void setEnvMap( const bool bRealTime, Render2DB *p2DB );

    bool applyPNJConfiguration( AvatarConfiguration &outConfig,
                                const DBEntryPNJ &pnjEntry );
    void bakeAvatar( EntityAvatar *pAvatar,
                     const AvatarConfiguration *pConfig,
                     int32_t clotheSet,
                     bool bAsync,
                     bool bCritical );
    void installClothes( EntityAvatar *pAvatar,
                         const AvatarConfiguration *pConfig,
                         int32_t clotheSet,
                         bool bAsync,
                         bool bCritical );

private:
    void installStyleResource( EntityAvatar *pAvatar,
                               const AvatarConfiguration *pConfig );
    void installBodyResource( EntityAvatar *pAvatar,
                              const AvatarConfiguration *pConfig );

public:
    void installSkinResource( EntityAvatar *pAvatar,
                              const AvatarConfiguration *pConfig );
    void installEyeResource( EntityAvatar *pAvatar,
                             const AvatarConfiguration *pConfig );

private:
    void installBodySkin( EntityAvatar *pAvatar,
                          const AvatarConfiguration *pConfig );

public:
    bool installBelt( const char *pBeltType,
                      EntityAvatar *pAvatar,
                      eAvatarBeltSeat seat );
    AvatarClothePart *findClotheByID( uint16_t id );
    void savePlayerAvatarConfiguration( const EntityAvatar *pAvatar );
    int32_t getNumPNJConfigurations();
    bool getPNJConfiguration( int32_t index,
                              std::string &outName,
                              AvatarConfiguration &outConfig );
    void readAvatarConfig( const EntityAvatar *pAvatar,
                           AvatarConfiguration &outConfig );

private:
    AvatarBeltPart *findBelt( eAvatarClotheSize size,
                              int32_t gender,
                              eAvatarBeltType beltType,
                              eAvatarBeltSeat seat );

    AvatarClothePart *findDefaultPartByID( uint16_t id );
    bool loadFaceModifSet( AvatarConfiguration &outConfig,
                           const char *pFileConfigName );

    void buildClothesDB();
    void buildDefaultPartsDB();
    void buildBodyDB();
    void buildSkinDB();
    void buildBodyLODDB();
    void buildStyleDB();
    void buildBeltDB();
    void buildEyeDB();
    void loadConfigBanks();
    void buildDefaultConfig();
    void buildLowResBonePalettes();

private:
    Matrix4x4f_t pBones[2][kNumIdentityBones];

    AvatarPartDB *pClothesDB;
    AvatarPartDB *pDefaultPartsDB;
    AvatarPartDB *pBodyDB;
    AvatarPartDB *pBodyLODDB;
    AvatarPartDB *pStyleDB;
    AvatarPartDB *pSkinDB;
    AvatarPartDB *pBeltDB;
    AvatarPartDB *pEyeDB;

    AvatarPool pool;
    Bank pnjBank;
    Bank styleBank;

    AvatarConfiguration pDefaultConfig[kNumGenders];
    int32_t maxClotheID;

    class EntityAnmSet *pFaceModifSet;
    ScenaricBank faceParametersBank;
    Eigen::Vector2f pFaceRange[2][kNumFaceParams];

    AvatarBnkManager bankManager;
};

extern GSAvatar *gpAvatar;

extern uint64_t gHashcodeNoMesh;
extern uint64_t gHashcodeArms;
extern uint64_t gHashcodeEyes;
extern uint64_t gHashcodeFeet;
extern uint64_t gHashcodeHands;
extern uint64_t gHashcodeHead;
extern uint64_t gHashcodeLegs;
extern uint64_t gHashcodeSexe;
extern uint64_t gHashcodeTeeth;
extern uint64_t gHashcodeTorso;
extern uint64_t gHashcodeHideable;

enum eAvatarLightFactor : int32_t {
    ALF_LightModulator = 0,
    ALF_HeightBase = 1,
    ALF_HeightEnd = 2,
    ALF_AmbiantAttenuation = 3,
    ALF_DiffuseAttenuation = 4,
    ALF_SpecularAttenuation = 5,

    ALF_Count = 6
};

extern float gAvatarInCarLightFactor[ALF_Count];
extern float gAvatarConvertibleLightFactor[ALF_Count];
extern float gAvatarOutCarLightFactor[ALF_Count];
extern bool gShowAvatar;
