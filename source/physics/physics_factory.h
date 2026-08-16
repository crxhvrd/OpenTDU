#pragma once

enum class ePhysEntityType {
    PET_VehicleDesc = 0,
    PET_Vehicle = 1,
    PET_Constraint = 2,
    PET_RigidBody = 3,
    PET_Unknown = 4,
    PET_PrimitiveShape = 5
};

struct SerializedPhysics;
struct HavokFactoryData;

class PhysFactory {
public:
    struct Entry {
        uint64_t Hashcode;
        uint32_t FactoryHashcode;
        ePhysEntityType Type;
        void *pStream;
        uint32_t Flags;
        uint64_t RefHashcode;
    };

public:
    inline bool isInitialized() const { return bInitialized; }
    inline HavokFactoryData *getHavokData() const { return pHavokData; }
    inline uint64_t getHashcode() const { return hashcode; }
    inline const void *getSourceBlob() const { return pFileHK; }

    // Take ownership of the reconstructed collision bodies and mark the slot in
    // use. Defined in physics_factory.cpp (needs the full HavokFactoryData).
    void initialize( HavokFactoryData *pData,
                     uint64_t factoryHashcode = 0,
                     const void *pBlob = nullptr );

    // Release the reconstructed bodies and free the slot. Any live Jolt bodies
    // must be removed from the world first
    // (GSPhysics::removeAndDestroyFactory), since the factory has no world
    // access.
    void reset();

private:
    uint8_t bInitialized : 1;
    uint8_t bInstanciated : 1;
    uint8_t bSelfAllocated : 1;
    uint8_t bLocalShapesInstanciation : 1;
    uint8_t bAddedToPhysWorld : 1;
    uint8_t bUnknown : 1;

    uint32_t collisionGroup;
    uint32_t groupIndex;
    uint64_t hashcode;

    ePhysEntityType type;
    float offsetX;
    float offsetY;

    uint8_t coords[4];

    SerializedPhysics *pSerializedData;
    const void *pFileHK = nullptr;
    HavokFactoryData *pHavokData = nullptr;
};

struct SerializedPhysics {
    uint8_t Version[2];
    uint16_t Size;
    uint32_t VehicleBufferSize;
    uint32_t VehicleDescBufferSize;
    PhysFactory::Entry *pEntries;
    uint8_t *pVehicleBuffer;
    uint8_t *pVehicleDescBuffer;
    uint32_t HavokSceneSize;
    uint8_t *pHavokScene;
};
