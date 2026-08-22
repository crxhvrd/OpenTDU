#pragma once

#include "core/notifier.h"

class edStore;

class SavedItemTable : public Notifiable {
public:
    static constexpr int32_t kNumEntries = 100;
    static constexpr uint16_t kDefaultKind = 1u;

public:
    SavedItemTable();
    ~SavedItemTable();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

public:
    uint16_t Kinds[kNumEntries]; 
    uint32_t Values[kNumEntries]; 
    uint32_t Count = 0u;          
};
