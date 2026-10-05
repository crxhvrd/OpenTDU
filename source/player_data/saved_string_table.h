#pragma once

class edStore;

class SavedStringTable {
public:
    static constexpr int32_t kNumEntries = 0x80;
    static constexpr uint32_t kEntryLength = 8u;

    struct Entry {
        char Key[kEntryLength + 1u] = {};
        char Value[kEntryLength + 1u] = {};
    };

public:
    SavedStringTable();
    ~SavedStringTable();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    const char *findValue( const char *pKey ) const;
    bool setValue( const char *pKey, const char *pValue );

private:
    Entry *findEntry( const char *pKey );

public:
    Entry Entries[kNumEntries];
};
