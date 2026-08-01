#include "shared.h"
#include "ed_audio_memory.h"

#include "core/mutex.h"

namespace edAudioHeap {
    namespace {
        MemoryBlock gBlocks[kNumBlocks] = {}; // DAT_0100fe30
        int32_t gTotalBytes = 0;              // DAT_0100fe30 + 96000
        int32_t gUsedBytes = 0;
        int32_t gFragments = 0;
        TestDriveMutex gMutex;

        MemoryBlock *findUnusedDescriptor() {
            for ( int32_t i = 0; i < kNumBlocks; i++ ) {
                if ( gBlocks[i].State == AMMDS_Unused ) {
                    return &gBlocks[i];
                }
            }

            return nullptr;
        }

        MemoryBlock *allocateDescriptor( int32_t alignedSize ) {
            // FUN_006111c0
            MemoryBlock *pDescriptor = nullptr;
            for ( int32_t i = 0; i < kNumBlocks; i++ ) {
                if ( gBlocks[i].State == AMMDS_Free &&
                     gBlocks[i].Size >= alignedSize ) {
                    pDescriptor = &gBlocks[i];
                    break;
                }
            }

            if ( pDescriptor == nullptr ) {
                return nullptr;
            }

            if ( ( pDescriptor->Size - alignedSize ) >= kAllocGranularity ) {
                MemoryBlock *pRemainder = findUnusedDescriptor();
                if ( pRemainder != nullptr ) {
                    pRemainder->pPointer = pDescriptor->pPointer + alignedSize;
                    pRemainder->Size = pDescriptor->Size - alignedSize;
                    pRemainder->State = AMMDS_Free;

                    pDescriptor->Size = alignedSize;
                }
            }

            pDescriptor->State = AMMDS_Used;
            return pDescriptor;
        }

        void defragADescriptorFreeSpace( MemoryBlock *pDescriptor ) {
            // FUN_00611250
            for ( int32_t i = 0; i < kNumBlocks; i++ ) {
                MemoryBlock &descriptor = gBlocks[i];
                if ( descriptor.State != AMMDS_Free ) {
                    continue;
                }

                if ( descriptor.pPointer ==
                     pDescriptor->pPointer + pDescriptor->Size ) {
                    descriptor.pPointer = pDescriptor->pPointer;
                    descriptor.Size += pDescriptor->Size;
                    pDescriptor->State = AMMDS_Unused;
                    defragADescriptorFreeSpace( &descriptor );
                }

                if ( descriptor.pPointer + descriptor.Size ==
                     pDescriptor->pPointer ) {
                    descriptor.Size += pDescriptor->Size;
                    pDescriptor->State = AMMDS_Unused;
                    defragADescriptorFreeSpace( &descriptor );
                }
            }
        }

        inline uintptr_t AlignDown( uintptr_t value ) {
            return value & ~static_cast<uintptr_t>( kAllocGranularity - 1 );
        }
    } // namespace

    void InitMemory( void *pMemory, int32_t size ) {
        // FUN_00611300
        ScopedMutexLock lock( &gMutex );

        for ( int32_t i = 0; i < kNumBlocks; i++ ) {
            gBlocks[i].State = AMMDS_Unused;
        }

        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>( pMemory );
        uint8_t *pBase = reinterpret_cast<uint8_t *>(
            AlignDown( baseAddress + ( kAllocGranularity - 1 ) ) );
        uint8_t *pEnd =
            reinterpret_cast<uint8_t *>( AlignDown( baseAddress + size - 1 ) );

        gTotalBytes = static_cast<int32_t>( pEnd - pBase );
        gUsedBytes = 0;
        gFragments = 0;

        MemoryBlock *pDescriptor = findUnusedDescriptor();
        pDescriptor->pPointer = pBase;
        pDescriptor->Size = gTotalBytes;
        pDescriptor->State = AMMDS_Free;
    }

    void *Allocate( int32_t size ) {
        // FUN_006113c0
        if ( size == 0 ) {
            return nullptr;
        }

        ScopedMutexLock lock( &gMutex );

        const int32_t alignedSize =
            ( size + ( kAllocGranularity - 1 ) ) & ~( kAllocGranularity - 1 );
        MemoryBlock *pDescriptor = allocateDescriptor( alignedSize );
        if ( pDescriptor == nullptr ) {
            return nullptr;
        }

        gUsedBytes += pDescriptor->Size;
        gFragments++;
        return pDescriptor->pPointer;
    }

    void Free( void *pPointer ) {
        // FUN_00611420
        if ( pPointer == nullptr ) {
            return;
        }

        ScopedMutexLock lock( &gMutex );

        for ( int32_t i = 0; i < kNumBlocks; i++ ) {
            MemoryBlock &descriptor = gBlocks[i];
            if ( descriptor.pPointer != pPointer ||
                 descriptor.State != AMMDS_Used ) {
                continue;
            }

            gUsedBytes -= descriptor.Size;
            gFragments--;
            descriptor.State = AMMDS_Free;
            defragADescriptorFreeSpace( &descriptor );
            return;
        }
    }

    int32_t GetUsedBytes() {
        // FUN_00611110
        return gUsedBytes;
    }

    int32_t GetFreeBytes() {
        // FUN_00611100
        return gTotalBytes - gUsedBytes;
    }

    int32_t GetFragmentCount() {
        // FUN_00611120
        return gFragments;
    }
} // namespace edAudioHeap
