#include "shared.h"
#include "flash_callbacks_register.h"

FlashCallbacksRegister gFlashCallbacksRegister = {}; // DAT_00fa88f8

FlashCallbacksRegister::FlashCallbacksRegister() : numCallbacks( 0 ) {}

FlashCallbacksRegister::~FlashCallbacksRegister() {}

void FlashCallbacksRegister::add( const char *param_1,
                                  Callback_t param_2,
                                  void *param_4 ) {
    // FUN_0099ccd0
    if ( numCallbacks < kMaxNumCallbacks ) {
        strncpy( pCallbacks[numCallbacks].pClipName, param_1, 0x20 );
        pCallbacks[numCallbacks].pClipName[0x1f] = '\0';
        pCallbacks[numCallbacks].pCallback = param_2;
        pCallbacks[numCallbacks].pUser = param_4;
        numCallbacks = numCallbacks + 1;
    }
}

void FlashCallbacksRegister::remove( const char *param_1 ) {
    // FUN_0099cc40
    if ( param_1 == nullptr ) {
        return;
    }

    uint32_t writeIndex = 0;
    for ( uint32_t readIndex = 0; readIndex < numCallbacks; readIndex++ ) {
        if ( strncmp( pCallbacks[readIndex].pClipName, param_1, 0x20 ) == 0 ) {
            continue; // drop
        }
        if ( writeIndex != readIndex ) {
            pCallbacks[writeIndex] = std::move( pCallbacks[readIndex] );
        }
        writeIndex++;
    }
    // Release the shifted-out tail so stale std::functions/users don't linger.
    for ( uint32_t i = writeIndex; i < numCallbacks; i++ ) {
        pCallbacks[i] = CallbackEntry{};
    }
    numCallbacks = writeIndex;
}

void FlashCallbacksRegister::remove( Callback_t pCallback,
                                     const char *pClipName ) {
    // FUN_0099cb60
    for ( uint32_t i = 0; i < numCallbacks; i++ ) {
        if ( pCallbacks[i].pCallback != pCallback ||
             strcmp( pCallbacks[i].pClipName, pClipName ) != 0 ) {
            continue;
        }

        for ( uint32_t j = i + 1; j < numCallbacks; j++ ) {
            pCallbacks[j - 1] = pCallbacks[j];
        }
        numCallbacks--;
        pCallbacks[numCallbacks] = CallbackEntry{};
        return;
    }
}

void FlashCallbacksRegister::removeAllSynonyms( Callback_t param_1 ) {
    // FUN_0099cac0
    uint32_t writeIndex = 0;
    for ( uint32_t readIndex = 0; readIndex < numCallbacks; readIndex++ ) {
        if ( pCallbacks[readIndex].pCallback == param_1 ) {
            continue; // drop
        }
        if ( writeIndex != readIndex ) {
            pCallbacks[writeIndex] = pCallbacks[readIndex];
        }
        writeIndex++;
    }
    for ( uint32_t i = writeIndex; i < numCallbacks; i++ ) {
        pCallbacks[i] = CallbackEntry{};
    }
    numCallbacks = writeIndex;
}

void FlashCallbacksRegister::executeCallbacks( FlashRenderer::State *param_2,
                                               float param_3,
                                               DrawList *param_4,
                                               FlashResource *param_5,
                                               const char *param_6 ) {
    // FUN_0099ca30
    if ( param_6 != nullptr && *param_6 != '\0' ) {
        for ( uint32_t iVar4 = 0; iVar4 < numCallbacks; iVar4++ ) {
            size_t strLength = Min( 0x20, strlen( param_6 ) );
            if ( strncmp( pCallbacks[iVar4].pClipName, param_6, strLength ) ==
                 0 ) {
                pCallbacks[iVar4].pCallback( &param_2->Viewport, param_3,
                                             param_4, pCallbacks[iVar4].pUser,
                                             param_5, param_6 );
            }
        }
    }
}
