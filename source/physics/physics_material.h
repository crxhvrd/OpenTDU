#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>

#include <array>

// Havok stores one material index per collision triangle
// (hkStorageMeshShapeSubpartStorage::m_materialIndices, +0x24, one u8 per
// triangle) indexing a per-storage table of dwords (m_materials, +0x30). Every
// value in the shipped .shk / -R.shk blobs is `0x40000000 | code`, where code
// is an EMaterial — the same numbering as eMaterialType: 0 Asphalt, 1 Dirt,
// 2 Grass, 3 Rock, 4 Water, 6 Metal, 8 Concrete, 9 Pavement, 39-41 SP1-SP3.
// FUN_0083bbd0 reads the resulting per-wheel code and only stores it
// when it is not MT_Default.
namespace TDUMaterialValue {
    // The EMaterial code occupies the low byte; bit 30 is set on every entry.
    constexpr uint32_t kCodeMask = 0xFFu;

    // eMaterialType::MT_Count / MT_Default — the original's "no material, leave
    // the previous value" sentinel is 47 (MT_Default), and 48 is the count.
    constexpr uint32_t kNumCodes = 48u;
    constexpr uint32_t kDefault = 47u;

    inline uint32_t decode( uint32_t rawMaterial ) {
        return rawMaterial & kCodeMask;
    }
} // namespace TDUMaterialValue

namespace TDUPhysicsMaterials {
    using Table = std::array<JPH::RefConst<JPH::PhysicsMaterial>,
                             TDUMaterialValue::kNumCodes>;

    inline const Table &table() {
        static const Table sTable = []() {
            Table t;
            for ( uint32_t i = 0; i < TDUMaterialValue::kNumCodes; i++ ) {
                t[i] = new JPH::PhysicsMaterial();
            }
            return t;
        }();
        return sTable;
    }

    inline const JPH::PhysicsMaterial *get( uint32_t code ) {
        if ( code >= TDUMaterialValue::kNumCodes ) {
            code = TDUMaterialValue::kDefault;
        }
        return table()[code].GetPtr();
    }

    // Recovers the eMaterialType code behind a material pointer, or
    // MT_Default when the shape carries no per-triangle material.
    inline uint32_t codeOf( const JPH::PhysicsMaterial *pMaterial ) {
        if ( pMaterial != nullptr ) {
            const Table &t = table();
            for ( uint32_t i = 0; i < TDUMaterialValue::kNumCodes; i++ ) {
                if ( t[i].GetPtr() == pMaterial ) {
                    return i;
                }
            }
        }
        return TDUMaterialValue::kDefault;
    }
} // namespace TDUPhysicsMaterials
