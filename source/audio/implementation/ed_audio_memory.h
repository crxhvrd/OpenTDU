#pragma once

// Audio heap owned by edAudio
namespace edAudioHeap {
    enum eBlockstate : int32_t {
        AMMDS_Unused = 0, // slot describes nothing
        AMMDS_Free = 1,
        AMMDS_Used = 2,
    };

    struct MemoryBlock {
        uint8_t *pPointer;
        int32_t Size;
        eBlockstate State;
    };

    static constexpr int32_t kNumBlocks = 8000;
    static constexpr int32_t kAllocGranularity = 128;

    void InitMemory( void *pMemory, int32_t size );
    void *Allocate( int32_t size );
    void Free( void *pPointer );

    int32_t GetUsedBytes();
    int32_t GetFreeBytes();
    int32_t GetFragmentCount();
} // namespace edAudioHeap
