#include "shared.h"
#include "entity_sound_density.h"

#include "audio/implementation/audio_dispatcher_playback.h"
#include "audio/implementation/ed_audio_sample.h"
#include "config/gs_config.h"
#include "filesystem/binary.h"
#include "filesystem/gs_file.h"
#include "game/entity_mechanism.h"
#include "system/system_shared.h"

EntitySoundDensity::DensitySlot
    EntitySoundDensity::Slots[EntitySoundDensity::kMaxDensitySlots] = {};

EntitySoundDensity::EntitySoundDensity()
    : EntitySoundEmitter(),
      pBankName( nullptr ),
      pBankData( nullptr ),
      bankDataSize( 0 ),
      SlotIndex( -1 ),
      State( SDS_Idle ),
      pInstallGuard( std::make_shared<bool>( true ) ) {
    // FUN_007dfec0
}

EntitySoundDensity::~EntitySoundDensity() {
    // FUN_004268c0
}

void EntitySoundDensity::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dff40
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }

    resolveSoundReference();

    pBankData = nullptr;
    pSample = nullptr;
}

void EntitySoundDensity::unloadResources() {
    // FUN_007df100
    uninstallSoundBank();
}

void EntitySoundDensity::deserialize( BinaryReader *param_2 ) {
    // FUN_007df9e0
    Entity::deserialize( param_2 );

    pBankName = reinterpret_cast<const char *>( param_2->pCursor );
    while ( *param_2->pCursor != '\0' ) {
        param_2->pCursor++;
    }
    param_2->pCursor = reinterpret_cast<uint8_t *>(
        ( reinterpret_cast<uintptr_t>( param_2->pCursor ) + 4 ) &
        ~static_cast<uintptr_t>( 3 ) );

    deserializeSoundParameters( param_2 );
}

void EntitySoundDensity::installSoundBank() {
    // FUN_007e0020
    if ( pBankName == nullptr || pSample != nullptr || pBankData != nullptr ||
         State != SDS_Idle ) {
        return;
    }

    SlotIndex = -1;

    int32_t freeSlot = 0;
    while ( freeSlot < kMaxDensitySlots && Slots[freeSlot].pOwner != nullptr ) {
        freeSlot++;
    }

    if ( freeSlot >= kMaxDensitySlots ) {
        return;
    }

    Slots[freeSlot].pOwner = this;
    Slots[freeSlot].State = SDS_Idle;
    SlotIndex = freeSlot;

    char path[512];
    snprintf( path, sizeof( path ), "%sSound/Cinematics/%s",
              gpConfig->getResRootPath(), pBankName );

    bool bInstalling = false;
    if ( gpFile != nullptr ) {
        bankDataSize = 0;

        std::weak_ptr<bool> guard = pInstallGuard;
        bInstalling =
            gpFile->loadBank( path, &pBankData, &bankDataSize, this,
                              [this, guard]( int32_t type, int32_t bSuccess ) {
                                  if ( guard.expired() ) {
                                      return;
                                  }
                                  if ( type == 4 && bSuccess ) {
                                      onSoundBankInstalled();
                                  }
                              } );
    }

    if ( bInstalling ) {
        Slots[SlotIndex].State = SDS_Installing;
        Slots[SlotIndex].pBankData = pBankData;
        State = SDS_Installing;
        return;
    }

    OTDU_LOG_WARN( "EntitySoundDensity: can't install '%s'\n", path );

    Slots[SlotIndex].State = SDS_Finished;
    Slots[SlotIndex].pBankData = nullptr;
    State = SDS_Finished;
}

void EntitySoundDensity::uninstallSoundBank() {
    // FUN_007df110
    if ( State == SDS_Idle ) {
        return;
    }

    if ( pSoundInstance != nullptr ) {
        pSoundInstance->setSample( nullptr );
        pSoundInstance->release();
        pSoundInstance = nullptr;
    }

    if ( State != SDS_Installing ) {
        sampleStorage.reset();
        if ( pBankData != nullptr ) {
            // OTDU_ADDITION: GSFile::loadBank allocates the bank with
            // TestDrive::Alloc; _aligned_free on that block reads a bogus
            // header and corrupts the heap (0xC0000374 when a cinematic with
            // a sound density entity is unloaded, e.g. entering a house).
            TestDrive::Free( pBankData );
        }
        pBankData = nullptr;
        bankDataSize = 0;
        pSample = nullptr;

        if ( SlotIndex >= 0 ) {
            Slots[SlotIndex].pOwner = nullptr;
            Slots[SlotIndex].pBankData = nullptr;
        }
    }

    State = SDS_Idle;
    if ( SlotIndex >= 0 ) {
        Slots[SlotIndex].State = SDS_Idle;
    }
}

void EntitySoundDensity::onSoundBankInstalled() {
    // FUN_007df1d0
    State = SDS_Finished;
    if ( SlotIndex >= 0 ) {
        Slots[SlotIndex].State = SDS_Finished;
        Slots[SlotIndex].pBankData = pBankData;
    }

    sampleStorage.reset();
    pSample = nullptr;

    if ( pBankData == nullptr || bankDataSize == 0 ) {
        OTDU_LOG_WARN( "EntitySoundDensity: '%s' installed empty\n",
                       pBankName != nullptr ? pBankName : "<none>" );
        return;
    }

    auto sample = std::make_unique<edAudioSample>();
    if ( !sample->loadFromMemory( pBankData,
                                  static_cast<int32_t>( bankDataSize ) ) ) {
        OTDU_LOG_WARN( "EntitySoundDensity: '%s' is not a decodable sound\n",
                       pBankName );
        return;
    }

    sampleStorage = std::move( sample );
    pSample = sampleStorage.get();
    OTDU_LOG_DEBUG( "Cinematic sound '%s' installed (%d frames at %dHz)\n",
                    pBankName, pSample->getFrameCount(),
                    pSample->getSampleRate() );
}
