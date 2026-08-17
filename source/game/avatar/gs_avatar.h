#pragma once

#include "shared.h"

#include "game/gs_base.h"
#include "game/scenaric/scenaric_bank.h"
#include "filesystem/bank.h"
#include "avatar_bnk.h"
#include "avatar_configuration.h"
#include "avatar_part.h"
#include "avatar_pool.h"

class EntityAvatar;
struct DBEntryPNJ;

class GSAvatar : public GameSystem {
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

public:
    GSAvatar();
    ~GSAvatar();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;

    bool applyPNJConfiguration( AvatarConfiguration &outConfig,
                                const DBEntryPNJ &pnjEntry );

    void bakeAvatar( EntityAvatar *pAvatar,
                     const AvatarConfiguration *pConfig,
                     int32_t bodyType,
                     bool bForceReskin,
                     bool bForceFullBake );
    void installClothes( EntityAvatar *pAvatar,
                         int32_t bodyType,
                         bool bForceReskin,
                         bool bForceFullBake );

private:
    void installStyleResource( EntityAvatar *pAvatar,
                               const AvatarConfiguration *pConfig );
    void installSkinResource( EntityAvatar *pAvatar,
                              const AvatarConfiguration *pConfig );
    void installBodyResource( EntityAvatar *pAvatar,
                              const AvatarConfiguration *pConfig );
    void installEyeResource( EntityAvatar *pAvatar,
                             const AvatarConfiguration *pConfig );
    void installBodySkin( EntityAvatar *pAvatar,
                          const AvatarConfiguration *pConfig );
    void readAvatarConfig( const EntityAvatar *pAvatar,
                           AvatarConfiguration &outConfig );
    bool loadFaceModifSet( AvatarConfiguration &outConfig,
                           const char *pFileConfigName );

    void buildClothesDB();
    void buildDefaultPartsDB();
    void buildBodyDB();
    void buildBodyMorphDB();
    void buildBodyLODDB();
    void buildStyleDB();
    void buildBeltDB();
    void buildEyeDB();
    void loadConfigBanks();
    void buildDefaultConfig();

private:
    Matrix4x4f_t pBones[2][256];

    AvatarPartDB *pClothesDB;
    AvatarPartDB *pDefaultPartsDB;
    AvatarPartDB *pBodyDB;
    AvatarPartDB *pBodyLODDB;
    AvatarPartDB *pStyleDB;
    AvatarPartDB *pBodyMorphDB;
    AvatarPartDB *pBeltDB;
    AvatarPartDB *pEyeDB;

    AvatarPool pool;
    Bank pnjBank;
    Bank styleBank;

    AvatarConfiguration pDefaultConfig[kNumGenders];
    int32_t maxClotheID;

    uint32_t faceParameterCount;
    ScenaricBank faceParametersBank;
    Eigen::Vector2f pFaceRange[2][kNumFaceParams];

    AvatarBnkManager bankManager;
};

extern GSAvatar *gpAvatar;

extern float gAvatarInCarLightFactor[6];
extern float gAvatarConvertibleLightFactor[6];
extern float gAvatarOutCarLightFactor[6];
