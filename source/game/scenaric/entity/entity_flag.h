#pragma once

#include "base_entity.h"

class Render2DM;
class Render2DB;
struct ClothInstance;

// Cloth flag (e.g. starting line flags, spot flags)
class EntityFlag : public Entity {
public:
    const char *GetName() override { return "SCFlag"; /* FUN_004242a0 */ }

public:
    enum eAttachMode : int32_t {
        AM_LeftEdge = 0,
        AM_LeftAndTopEdges = 1,
        AM_LeftAndRightEdges = 2,
    };

public:
    EntityFlag();
    ~EntityFlag();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void initializeInstance( void *param_2, void *param_3 ) override;
    void unloadResources() override;
    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

private:
    void pinAttachedEdges();

protected:
    Eigen::Vector4f size; // x = width, y = height
    Eigen::Vector4f unknownVector;
    Render2DM *pRef2DM;
    int32_t num2DBRefs;
    Render2DB *pRef2DBs;
    ClothInstance *pCloth;
    eAttachMode attachMode;
    bool bPinWholeEdges;
};
