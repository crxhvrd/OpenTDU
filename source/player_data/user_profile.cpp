#include "shared.h"
#include "user_profile.h"

#include "core/hash/crc32.h"
#include "filesystem/gs_file.h"

#include <filesystem>

UserProfileRegister gUserProfileRegister = {};

UserProfileRegister::UserProfileRegister()
    : saveRoot( "./" ),
      activeProfileName( "" ),
      activeProfileIndex( 0 ),
      activeProfileHashcode( 0 ),
      pathToProfile( "" ),
      listHashcode( 0ull ),
      unknownDword( 0 ),
      unknownDword2( 0 ) {}

UserProfileRegister::~UserProfileRegister() {}

uint64_t UserProfileRegister::HashProfileName( const char *pProfileName ) {
    // FUN_0060ab60
    const uint32_t crc = TestDriveCRC32Unfolded( (const uint8_t *)pProfileName,
                                                 strlen( pProfileName ) );

    return static_cast<uint64_t>( static_cast<uint16_t>( crc ) );
}

bool UserProfileRegister::initialize( uint64_t hashcode ) {
    activeProfileHashcode = hashcode;
    activeProfileIndex = getProfileIndex( hashcode );
    activeProfileName.clear();

    if ( activeProfileIndex < 0 ) {
        OTDU_LOG_ERROR( "No profile matches hashcode %llu\n", hashcode );
        return false;
    }

    activeProfileName = profileNames[activeProfileIndex];

    OTDU_LOG_DEBUG(
        "Active profile set to '%s' (profile index %u hashcode %llu)\n",
        activeProfileName.c_str(), activeProfileIndex, activeProfileHashcode );

    return true;
}

void UserProfileRegister::deserialize() {
    std::wstring saveLocation =
        gpFile->getSaveLocation( eContextSource::CS_Savegame );
    if ( saveLocation.empty() ) {
        OTDU_LOG_ERROR( "Failed to retrieve save location; profile "
                        "deserialization will be skipped\n" );
        return;
    }
    saveLocation += L"/ProfileList.dat";

    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

    std::string narrowFilename = converter.to_bytes( saveLocation );

    OTDU_LOG_DEBUG( "Deserializing profile list '%s'...\n",
                    narrowFilename.c_str() );

    std::ifstream profileListStream( narrowFilename.c_str(),
                                     std::ios::in | std::ios::binary );

    uint64_t activeProfileCRC = 0ull;
    if ( profileListStream.good() ) {
        profileListStream.seekg( 0, std::ios::end );
        auto fileSize = profileListStream.tellg();
        profileListStream.seekg( 0 );

        uint16_t word = 0;
        for ( auto i = 0; i < fileSize; ) {
            profileListStream.read( (char *)&word, sizeof( uint16_t ) );
            i += sizeof( uint16_t );

            if ( word == 0xffff ) {
                profileListStream.read( (char *)&activeProfileCRC,
                                        sizeof( uint64_t ) );
                break;
            } else {
                std::string profileName;
                profileName.resize( word );

                profileListStream.read( profileName.data(),
                                        sizeof( char ) * word );
                OTDU_LOG_DEBUG( "\t-Found profile '%s' (index %u)\n",
                                profileName.c_str(), profileNames.size() );

                profileNames.push_back( profileName );
            }

            i += word;
        }
    }
    profileListStream.close();

    if ( profileNames.size() == 0 ) {
        listHashcode = 0ull;
        return;
    }

    int32_t deserializedProfileIndex = getProfileIndex( activeProfileCRC );
    if ( deserializedProfileIndex < 0 ) {
        if ( profileNames.size() > 0 ) {
            listHashcode = HashProfileName( profileNames[0].c_str() );
        }
    } else {
        listHashcode = activeProfileCRC;
    }

    initialize( listHashcode );
}

std::string UserProfileRegister::getProfileName( uint64_t hashcode ) const {
    int32_t profileIndex = getProfileIndex( hashcode );
    return ( -1 < profileIndex ) ? profileNames[profileIndex] : "";
}

const char *
UserProfileRegister::getProfileNameByIndex( const int32_t profileIndex ) const {
    // FUN_0060a180
    if ( profileIndex < 0 || size_t( profileIndex ) >= profileNames.size() ) {
        return "";
    }

    return profileNames[profileIndex].c_str();
}

std::string UserProfileRegister::getProfileFolderPath() const {
    // FUN_0060a1e0
    const std::filesystem::path folderPath =
        std::filesystem::path(
            gpFile->getSaveLocation( eContextSource::CS_Savegame ) ) /
        activeProfileName;
    return folderPath.string();
}

int32_t UserProfileRegister::getProfileIndex( uint64_t hashcode ) const {
    // FUN_0060a020
    for ( size_t i = 0; i < profileNames.size(); i++ ) {
        if ( HashProfileName( profileNames[i].c_str() ) == hashcode ) {
            return static_cast<int32_t>( i );
        }
    }

    return -1;
}

bool UserProfileRegister::registerProfile( const char *pProfileName ) {
    // FUN_0060a7f0
    if ( profileNames.size() >= kMaxProfiles ) {
        return false;
    }

    for ( const std::string &profileName : profileNames ) {
        if ( StringCmpI( profileName.c_str(), pProfileName ) == 0 ) {
            return false;
        }
    }

    std::string profileName = pProfileName;
    profileName.resize(
        Min( profileName.length(), kMaxProfileNameLength - 1 ) );
    profileNames.push_back( profileName );

    serialize();

    return true;
}

bool UserProfileRegister::updateProfileList( const uint64_t hashcode ) {
    // FUN_0060a7b0
    if ( getProfileIndex( hashcode ) < 0 ) {
        return false;
    }

    listHashcode = hashcode;
    serialize();

    return true;
}

void UserProfileRegister::serialize() {
    // FUN_0060a3b0
    std::wstring saveLocation =
        gpFile->getSaveLocation( eContextSource::CS_Savegame );
    if ( saveLocation.empty() ) {
        OTDU_LOG_ERROR( "Failed to retrieve save location; profile list will "
                        "not be written\n" );
        return;
    }
    saveLocation += L"/ProfileList.dat";

    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::string narrowFilename = converter.to_bytes( saveLocation );

    std::ofstream profileListStream( narrowFilename.c_str(),
                                     std::ios::out | std::ios::binary |
                                         std::ios::trunc );
    if ( !profileListStream.good() ) {
        OTDU_LOG_ERROR( "Failed to open '%s' for writing\n",
                        narrowFilename.c_str() );
        return;
    }

    for ( const std::string &profileName : profileNames ) {
        const uint16_t nameLength = uint16_t( profileName.length() );
        profileListStream.write( (const char *)&nameLength,
                                 sizeof( uint16_t ) );
        profileListStream.write( profileName.c_str(), nameLength );
    }

    const uint16_t endMarker = 0xffff;
    profileListStream.write( (const char *)&endMarker, sizeof( uint16_t ) );
    profileListStream.write( (const char *)&listHashcode, sizeof( uint64_t ) );
    profileListStream.close();
}

void UserProfileRegister::FUN_0060a160() {
    // FUN_0060a160
    unknownDword = 5;
}

uint32_t UserProfileRegister::FUN_00419de0() const {
    // FUN_00419de0
    return unknownDword2;
}
