#pragma once

class UserProfileRegister {
public:
    inline uint64_t getActiveProfileHashcode() const {
        return activeProfileHashcode;
    }
    inline uint64_t getProfileListHashcode() const { // FUN_0060a150
        return listHashcode;
    }
    inline const std::string &getActiveProfileName() const { // FUN_0060a140
        return activeProfileName;
    }
    inline int32_t getNumProfiles() const { // FUN_0060a170
        return static_cast<int32_t>( profileNames.size() );
    }

    static uint64_t HashProfileName( const char *pProfileName );

public:
    UserProfileRegister();
    ~UserProfileRegister();

    bool initialize( uint64_t hashcode );

    void deserialize();
    void serialize();
    std::string getProfileName( uint64_t hashcode ) const;
    const char *getProfileNameByIndex( const int32_t profileIndex ) const;
    std::string getProfileFolderPath() const;
    int32_t getProfileIndex( uint64_t hashcode ) const;

    bool registerProfile( const char *pProfileName );
    bool updateProfileList( const uint64_t hashcode );
    bool renameProfile( const char *pProfileName, const char *pNewProfileName );
    bool deleteProfile( const char *pProfileName );

    void FUN_0060a160();
    inline void setUnknownDword( const uint32_t value ) { // FUN_004adc30
        unknownDword = value;
    }
    inline uint32_t getUnknownDword() const { // FUN_0060a0b0
        return unknownDword;
    }
    inline void setUnknownDword3( const uint32_t value ) { // FUN_0068bf20
        unknownDword3 = value;
    }
    inline bool FUN_00630320() const { return false; }
    uint32_t FUN_00419de0() const;

private:
    static constexpr size_t kMaxProfiles = 32;
    static constexpr size_t kMaxProfileNameLength = 32;

private:
    std::string saveRoot;
    std::string activeProfileName;
    int32_t activeProfileIndex;
    uint64_t activeProfileHashcode;
    std::string pathToProfile;

    uint64_t listHashcode;

    std::vector<std::string> profileNames;
    uint32_t unknownDword;
    uint32_t unknownDword2;
    uint32_t unknownDword3;
};

extern UserProfileRegister gUserProfileRegister;
