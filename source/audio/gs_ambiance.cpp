#include "shared.h"
#include "gs_ambiance.h"

GSAmbiance *gpAmbiance = nullptr;

GSAmbiance::GSAmbiance()
    : GameSystem(),
      Mutex(),
      field_0x78( 0.1f ),
      field_0x7c( 0.7f ),
      ReverbModule( 0 ),
      pCommonConfig( nullptr ),
      bOwnsCommonConfig( false ),
      pEnvironmentClient( nullptr ),
      pEnvironmentPool( nullptr ),
      pEmitterInstancePool( nullptr ),
      pReverbPresetPool( nullptr ),
      pActiveEnvironment( nullptr ),
      field_0xa0( 0 ),
      field_0xa4( 1.0f ) {
    // FUN_009de4b0
    gpAmbiance = this;
}

GSAmbiance::~GSAmbiance() {
    // FUN_0040e540
}

bool GSAmbiance::initialize( TestDriveGameInstance *pInstance ) {
    // FUN_00901f90
    pGameInstance = pInstance;
    bPaused = false;
    Mutex.initialize( "GSAmbiance" );

    OTDU_UNIMPLEMENTED; // TODO: AUDIO/XMB

    return true;
}

void GSAmbiance::tick( float totalTime, float deltaTime ) {
    // FUN_009019f0
    ScopedMutexLock lock( &Mutex );

    OTDU_UNIMPLEMENTED; // TODO: AUDIO/XMB
}

void GSAmbiance::terminate() {
    // FUN_00901d10
    pActiveEnvironment = nullptr;
    OTDU_UNIMPLEMENTED; // TODO: AUDIO
    pEmitterInstancePool = nullptr;
    pEnvironmentPool = nullptr;
    pReverbPresetPool = nullptr;
    pEnvironmentClient = nullptr;

    if ( pCommonConfig != nullptr && bOwnsCommonConfig ) {
        OTDU_UNIMPLEMENTED; // TODO: XMB
    }
    pCommonConfig = nullptr;

    field_0x78 = 0.1f; // FLOAT_00f41668
    field_0x7c = 0.7f; // DAT_00f41828

    Mutex.terminate();
}
