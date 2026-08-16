#include "shared.h"
#include "physics_factory.h"

#include "havok/havok_to_jolt.h"

void PhysFactory::initialize( HavokFactoryData *pData,
                              uint64_t factoryHashcode,
                              const void *pBlob ) {
    pHavokData = pData;
    hashcode = factoryHashcode;
    pFileHK = pBlob;
    bInitialized = true;
}

void PhysFactory::reset() {
    // FUN_008427b0 (factory teardown). Live Jolt bodies are removed from the
    // world by GSPhysics::removeAndDestroyFactory before this is called; here we
    // only free the reconstructed template and mark the slot free.
    delete pHavokData;
    pHavokData = nullptr;
    pFileHK = nullptr;
    hashcode = 0;
    bInitialized = false;
}
