#pragma once

#include "shared.h"

#include "tdu_detail.h"

#include "core/maths_helper.h"

struct Paper {
    float lifetime;
    const char *pName;
    void *pCloth;
};

// Wind-blown papers + flying-leaf world detail (EURO/BNK/Details/Papers.bnk)
class Papers : public TDUDetail {
public:
    static constexpr int32_t kNumPapers = 8;

public:
    Papers();
    ~Papers() override;

    bool initialize() override;
    void tick( float deltaTime ) override;
    void draw() override;
    void terminate() override;
    void setEnabled( bool bEnable ) override;

private:
    void updatePapers( float deltaTime );
    void updateLeaf();
    void releasePapers();
    bool isAmbianceRegionActive( int32_t regionId );
    void computeSpawnPosition( float minDistance,
                               float maxDistance,
                               float mediumDistance,
                               Eigen::Vector4f &outPos );

private:
    Render2DB render2DBs[kNumPapers];
    Render2DM render2DM;
    bool bTestAmbiance;
    Paper papers[kNumPapers];
    int32_t numPapersToSpawn;
    float paperWindFactor;
    float paperSpawnTime;
    float paperNextSpawnTime;
    float paperMinDistance;
    float paperMedDistance;
    float paperMaxDistance;
    float leftMinDistance;
    float leafMaxDistance;
    void *pLeafParticle; // TODO: Particles (strongly type this)
};
