#pragma once

#include "shared.h"

#include "core/maths_helper.h"
#include "filesystem/bank.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

// Base class for details
class TDUDetail {
public:
    inline bool isInitialized() const { return bInitialized; }
    inline bool isEnabled() const { return bEnabled; }
    inline const char *getName() const { return pName; }
    inline uint32_t getHash() const { return hash; }

public:
    TDUDetail();
    virtual ~TDUDetail();

    virtual bool initialize() = 0;
    virtual void tick( float deltaTime ) {}
    virtual void draw() = 0;
    virtual void terminate() = 0;
    virtual void setEnabled( bool bEnable ) { bEnabled = bEnable; }

protected:
    void loadBank( const char *pBankName );
    bool load2DResources( Render2DB *p2DBArray,
                          Render2DM *p2DMArray,
                          int32_t num2DMToLoad,
                          int32_t num2DBToLoad );
    void unload2DResources();

protected:
    Bank bank;
    FileCollection2D collection2D;
    Render2DB *pLoaded2DB;
    int32_t num2DB;
    Render2DM *pLoaded2DM;
    int32_t num2DM;
    bool bInitialized;
    bool bEnabled;
    const char *pName;
    uint32_t hash;
};

static constexpr float kDetailRandNorm = 1.0f / 32767.0f; // DAT_00f41714

inline float DetailRandom() {
    return static_cast<float>( rand() & 0x7fff ) * kDetailRandNorm;
}

void ComputeDetailSpawnPosition( float minDistance,
                                 float maxDistance,
                                 float mediumDistance,
                                 Eigen::Vector4f &outPosition,
                                 Eigen::Vector4f &outDirection );
bool SampleDetailGroundHeight( const Eigen::Vector4f &worldPosition,
                               float &outHeight );
