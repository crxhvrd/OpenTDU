#pragma once

#include "shared.h"

#include "game/gs_base.h"
#include "game/scenaric/scenaric_bank.h"
#include "filesystem/bank.h"
#include "avatar_configuration.h"
#include "avatar_part.h"
#include "avatar_pool.h"

class EntityAvatar;

// TODO
struct AvatarBnkManager {};

class GSAvatar : public GameSystem {
public:
    const char *getName() const override {
        // FUN_00410840
        return "Service : Avatar";
    }

public:
    GSAvatar();
    ~GSAvatar();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;

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

    void buildClothesDB();
    void buildHairDB();
    void buildBodyDB();
    void buildBodyMorphDB();
    void buildBodyLODDB();
    void buildStyleDB();
    void buildBeltDB();
    void buildEyeDB();
    void loadConfigBanks();

private:
    Matrix4x4f_t pBones[2][256];

    AvatarPartDB *pClothesDB;
    AvatarPartDB *pHairDB;
    AvatarPartDB *pBodyDB;
    AvatarPartDB *pBodyLODDB;
    AvatarPartDB *pStyleDB;
    AvatarPartDB *pBodyMorphDB;
    AvatarPartDB *pBeltDB;
    AvatarPartDB *pEyeDB;

    AvatarPool pool;
    Bank pnjBank;
    Bank styleBank;

    AvatarConfiguration pDefaultConfig[2];

    uint32_t faceParameterCount;
    ScenaricBank faceParametersBank;
    Eigen::Vector2f pFaceRange[2][kNumFaceParams];

    AvatarBnkManager bankManager;
};

extern GSAvatar *gpAvatar;
