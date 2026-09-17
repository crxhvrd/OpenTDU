#pragma once

#include "shared.h"

class DrawList;

namespace prtf {
    constexpr uint32_t kKeyBase = 0x44, kKeyCount = 0x48;
    constexpr uint32_t kEmitterBase = 0x58, kEmitterCount = 0x60;
    constexpr uint32_t kDefABase = 0x64, kDefATotal = 0x6c;
    constexpr uint32_t kDefBBase = 0x70, kDefBTotal = 0x78;
    constexpr uint32_t kSmallBase = 0x7c, kSmallTotal = 0x84;
    constexpr uint32_t kMatBase = 0x88, kMatTotal = 0x90;

    constexpr uint32_t kKeyStride = 0x40;
    constexpr uint32_t kEmitterStride = 0x90;
    constexpr uint32_t kDefStride = 0x230;
    constexpr uint32_t kSmallStride = 0x20;
    constexpr uint32_t kMatStride = 0x110;
} // namespace prtf

namespace prto {
    constexpr uint32_t kState = 0x00;
    constexpr uint32_t kColorId = 0x04;
    constexpr uint32_t kGenDensity = 0x08;
    constexpr uint32_t kGenSpeed = 0x0c;
    constexpr uint32_t kGenSize = 0x10;

    constexpr uint32_t kAttrBase = 0x24;
    constexpr uint32_t kAttrActiveCount = 0x28;
    constexpr uint32_t kAttrTotalCount = 0x2c;
    constexpr uint32_t kVelBase = 0x30;
    constexpr uint32_t kPosBase = 0x34;

    constexpr uint32_t kEmitterBase = 0x38;
    constexpr uint32_t kEmitterActiveCount = 0x3c;
    constexpr uint32_t kEmitterTotalCount = 0x40;

    constexpr uint32_t kDefABase = 0x44;
    constexpr uint32_t kDefAActiveCount = 0x48;
    constexpr uint32_t kDefATotalCount = 0x4c;

    constexpr uint32_t kDefBBase = 0x50;
    constexpr uint32_t kDefBActiveCount = 0x54;
    constexpr uint32_t kDefBTotalCount = 0x58;

    constexpr uint32_t kSmallBase = 0x5c;
    constexpr uint32_t kSmallActiveCount = 0x60;
    constexpr uint32_t kSmallTotalCount = 0x64;

    constexpr uint32_t kMatBase = 0x68;
    constexpr uint32_t kMatActiveCount = 0x6c;
    constexpr uint32_t kMatTotalCount = 0x70;

    constexpr uint32_t kAttrStride = 0x40;
    constexpr uint32_t kVelStride = 0x20;
    constexpr uint32_t kPosStride = 0x10;
    constexpr uint32_t kEmitterStride = 0x90;
    constexpr uint32_t kDefStride = 0x230;
    constexpr uint32_t kSmallStride = 0x20;
    constexpr uint32_t kMatStride = 0x110;
} // namespace prto

// .2PT particle file
class ParticleFile {
public:
    bool isParsed() const { return pObject != nullptr; }
    uint8_t *getBlob() const { return pBlob; }
    uint8_t *getObject() const { return pObject; }

public:
    ParticleFile();

    bool parse( uint8_t *pData, uint32_t size );

    static bool IsParticleFile( const uint8_t *pData, uint32_t size );

    uint8_t *arrayBase( uint32_t baseField ) const;
    uint32_t arrayCount( uint32_t countField ) const;
    void updateInstance( const Eigen::Vector4f &wind,
                         uint8_t *pPrimaryDef,
                         const Eigen::Vector4f &color,
                         float alpha,
                         float genSize,
                         float genDensity,
                         float genSpeed,
                         const Matrix4x4f_t &instanceMatrix,
                         const Eigen::Vector4f &shiftPosition );

    void simulate( float deltaTime );
    void applyDefinitionForces( uint8_t *pEmitter, int32_t scratchCount );
    bool hasLiveParticles() const;
    void buildGeometry( const Matrix4x4f_t &worldToCam,
                        const Matrix4x4f_t &projection,
                        DrawList &out );
    void reseed( uint32_t seed );
    uint8_t *findPrimaryDefinition();
    void applyMaterialDescriptor( bool bHasDescriptor,
                                  uint8_t alphaReject,
                                  uint8_t coverageBudget );

private:
    void processEmitter( uint8_t *pEmitter );
    uint8_t *poolBase( uint32_t objectField ) const;
    uint32_t poolCount( uint32_t objectField ) const;
    void applyStartColor( uint32_t packedColor, float alpha );
    void spawnParticles( uint8_t *pEmitter,
                         uint8_t *pDef,
                         int32_t maxParticles,
                         float genDensity,
                         float genSpeed,
                         float genSize );
    void spawnShape( uint8_t *pDef, float *pOutVelocity, float *pOutPosition );
    float
    sampleRandom( uint8_t *pDef, uint8_t mode, float base, float variance );
    void integrateEmitter( uint8_t *pEmitter, float deltaTime );
    void ageLinkedDefinitions( uint8_t *pEmitter, uint8_t *pLink );
    void chainParticles( uint8_t *pLink, int32_t start, int32_t end );
    void orientBoundDefinition( uint8_t *pDef );
    void rebuildDefinitionMatrix( uint8_t *pDef );
    void attachBoundDefinition( uint8_t *pDef );
    void rebuildBoundDefinitionMatrices( uint8_t *pDef );
    void insertParticleInGridCell( uint8_t *pCell, uint32_t attrOffset );
    void linkLatticeParticle( uint8_t *pSource, uint8_t *pAttr );
    void emitDef( uint8_t *pEmitter,
                  uint8_t *pDef,
                  uint32_t maxParticles,
                  uint32_t colorId,
                  const Matrix4x4f_t &worldToCam,
                  const Matrix4x4f_t &projection,
                  DrawList &out );

private:
    uint8_t *pBlob;
    uint8_t *pObject;
    uint32_t blobSize;
};
