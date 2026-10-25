#pragma once

#include "game/mng_base.h"
#include "render/2dm.h"
#include "render/draw_list.h"

struct Material;

// Training level guide: green ribbon along the navigation path of the AI
// driving the player car
class MngNavPathDisplay : public Manager {
public:
    const char *getName() const override {
        // FUN_004326d0
        return "NavPathDisplay";
    }

public:
    MngNavPathDisplay();
    ~MngNavPathDisplay();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

private:
    static constexpr int32_t kMaxNumVertex = 0x80;
    static constexpr int32_t kHistorySize = 6;

    struct HistoryPoint {
        Eigen::Vector4f Position;
        Eigen::Vector4f Direction;
    };

    struct Writer {
        Eigen::Vector3f *pPosition = nullptr;
        uint32_t *pDiffuse = nullptr;
        uint16_t *pIndex = nullptr;
        uint16_t NextIndex = 0;

        void emit( const Eigen::Vector4f &position, const uint32_t color );
    };

private:
    bool beginStrip( const int32_t numVertex );
    void updateHistory( const Eigen::Vector4f &position,
                        const Eigen::Vector4f &direction );

private:
    DrawList drawList;
    Writer writer;
    void *p2DMBuffer;
    Render2DM path2DM;
    Material *pMaterial;
    bool bEnabled;
    float lastTargetSpeed;
    float alphaFromA;
    float alphaFromB;
    float alphaToA;
    float alphaToB;
    float alphaA;
    float alphaB;
    float blendTime;
    HistoryPoint history[kHistorySize];
    int32_t numHistory;
    int32_t historyStart;
};

extern MngNavPathDisplay *gpMngNavPathDisplay;
