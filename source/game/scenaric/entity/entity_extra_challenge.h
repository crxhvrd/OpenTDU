#pragma once

#include "base_entity.h"
#include "game/avatar/avatar_configuration.h"

struct BinaryReader;
class EntityVolume;

// Base class for freeride challenges.
class EntityExtraChallenge : public Entity {
public:
    const char *GetName() override {
        return "ExtraChallenge"; /* FUN_00427b10 */
    }

public:
    EntityExtraChallenge();
    ~EntityExtraChallenge();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void initializeInstance( void *param_2, void *param_3 ) override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    void deserialize( BinaryReader *param_2 ) override;

    virtual const char *getChallengeName() const { return nullptr; }

    virtual Eigen::Vector4f getSpotMeshPosition() const {
        // FUN_00427b20
        return modelMatrix.row( 3 );
    }

    bool isAvailable() const;

    inline int32_t getIndex() const { return Index; }
    inline void setIndex( int32_t index ) { Index = index; }
    inline const Matrix4x4f_t &getGoodiesMatrix() const {
        return GoodiesMatrix;
    }

protected:
    void readAvatarConfigFromPNJ( BinaryReader *pReader,
                                  AvatarConfiguration &outConfig );

protected:
    union ChallengeCondition {
        uint32_t Value;
        const char *pValue;
    };

protected:
    int32_t Index;
    float TimeLimit;
    SerializedTransform *pGoalMatrix;
    SerializedTransform *pGoodiesBinMatrix;
    Matrix4x4f_t GoodiesMatrix;
    uint64_t TriggerHashcode;
    EntityVolume *pTrigger;
    uint32_t AvatarVoiceType;
    AvatarConfiguration AvatarConfig;
    bool bInZone;
    int32_t RoadID;
    int32_t RoadEntityID;
};
