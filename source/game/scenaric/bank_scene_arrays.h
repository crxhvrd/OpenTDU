#pragma once

#include "filesystem/binary.h"

struct HiearchyNode;
class ScenaricBank;

class SceneryBinary {
public:
    SceneryBinary();

    void shiftObjects( const Eigen::Vector4f &streamOrigin );

    char *pName = nullptr;
    HiearchyNode *pHierarchyNode = nullptr;
    ScenaricBank *pBank = nullptr;
    uint32_t field_0xc = 0;
    Matrix4x4f_t InitialMatrix = Matrix4x4f_t::Identity();
    uint32_t field_0x50 = 0;
    uint32_t field_0x54 = 0;
};

class SceneryBinArray : public BinaryArray {
protected:
    void *deserializeElement( BinaryReader &reader, void *pContext ) override;
};

class Path {
public:
    uint32_t field_0x4 = 0;  // +0x04
    uint32_t field_0x8 = 0;  // +0x08
    uint32_t field_0xc = 0;  // +0x0c
    void *pArray0 = nullptr; // +0x10 packed sub-array: [count][count*8 dwords]
    void *pArray1 = nullptr; // +0x14 packed sub-array: [count][count*8 dwords]

    void deserialize( BinaryReader &reader );
};

class PathBinArray : public BinaryArray {
protected:
    void *deserializeElement( BinaryReader &reader, void *pContext ) override;
};

class SoundBinary {
public:
    void *pSoundInstance = nullptr;
    void *pWav = nullptr;
    float Volume = 0.0f;
    float ClipDistanceMin = 0.0f;
    float ClipDistanceMax = 0.0f;
    float Doppler = 0.0f;
    bool bLoop = false;
    float RandomPitch = 0.0f;
    float Pitch = 0.0f;
    float RandomVol = 0.0f;

    void deserialize( BinaryReader &reader );
};

class SoundBinArray : public BinaryArray {
protected:
    // FUN_007df640 (thunk 0x00429510 -> slot 0).
    void *deserializeElement( BinaryReader &reader, void *pContext ) override;
};
