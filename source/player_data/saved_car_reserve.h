#pragma once

class edStore;

class SavedCarReserve {
public:
    static constexpr uint32_t kDefaultSeed = 0x3039u;
    static constexpr int32_t kStockSize = 0x10;

public:
    SavedCarReserve();
    ~SavedCarReserve();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
    bool restock( const bool bFromSave );

public:
    uint32_t NumRestocks = 0u;      
    uint32_t Stock[kStockSize] = {}; 
    uint32_t LastRestockDate = 0u;   
    uint32_t RestockCounter = 0u;   
    uint32_t LastRestockTicks = 0u;  
    bool bStockLocked = false;      
    uint32_t RestockTimer = 0u;     
    uint32_t Seed = kDefaultSeed;   
};
