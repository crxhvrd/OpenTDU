#include "shared.h"
#include "game_parameter.h"

#include "core/binary_helpers.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "tdu_instance.h"

bool gbGameParameterOwnedCarsOnly = false;
int32_t gIGEFileVersion = 0;

const uint64_t kGameParameterEmptyOptions[1] = { 0x8a9087 };
const uint64_t kGameParameterLevelOptions[7] = {
    0x36b9b33, 0x37add73, 0x38a1fb3, 0x3a8a433,
    0x39961f3, 0x3b7e673, 0x6339522 };
const uint64_t kGameParameterRestrictionOptions[5] = {
    0x38ff432, kGameParameterChoiceBrand, kGameParameterChoiceModel,
    kGameParameterChoiceCategory, kGameParameterChoiceRange };
const uint64_t kGameParameterVehicleTypeOptions[3] = { 0x537c0f, 0x5502af,
                                                       0x56894f };
const uint64_t kGameParameterCarCategoryOptions[7] = {
    0x467a0e8, 0x476e328, 0x4862568, 0x49567a8,
    0x4a4a9e8, 0x4b3ec28, 0x4c32e68 };
const uint64_t kGameParameterBikeCategoryOptions[2] = { 0x493e108, 0x4a32348 };
const uint64_t kGameParameterDrivingAidOptions[4] = { 0x3326ae2, 0x341ad22,
                                                      0x350ef62, 0x3b7ee92 };

static uint64_t gOwnedCategoryOptions[7] = { 0x467a0e8, 0x476e328, 0x4862568,
                                             0x49567a8, 0x4a4a9e8, 0x4b3ec28,
                                             0x4c32e68 }; // DAT_00f94220

// DAT_00f94300
static constexpr GameParameterTypeDesc kTypeDescs[] = {
    { "text" },
    { "time" },
    { "level_0" },
    { "level_1" },
    { "level_2" },
    { "level_3" },
    { "level_4" },
    { "level_5" },
    { "weather_random" },
    { "weather_sunshine" },
    { "weather_cloudy" },
    { "weather_rainy" },
    { "weather_storm" },
    { "chkbox_off" },
    { "chkbox_on" },
    { "text" },
    { "level_free" },
    { "text" },
    { "text" },
    { "input_txt" },
    { "input_txt_disable" } };

static constexpr uint64_t kDefaultStringHashcode = 0x8a9087;
static constexpr uint64_t kInfiniteLapsHashcode = 0x5fda45;

static inline uint32_t ReadBigEndian32( const void *pData ) {
    return ReverseEndianness32( ReadUint32( (const uint8_t *)pData ) );
}

static inline void WriteBigEndian32( void *pData, const uint32_t value ) {
    Write<uint32_t>( (uint8_t *)pData, 0, ReverseEndianness32( value ) );
}

static inline uint64_t ReadBigEndian64( const void *pData ) {
    return ReverseEndianness64( Read<uint64_t>( (const uint8_t *)pData ) );
}

static inline void WriteBigEndian64( void *pData, const uint64_t value ) {
    Write<uint64_t>( (uint8_t *)pData, 0, ReverseEndianness64( value ) );
}

static inline const char *GetDefaultString() {
    return gpDatabase->getStringByHashcode( 0, kDefaultStringHashcode );
}

static inline const DBEntryCarPhysicsData &GetCarEntry( const int32_t index ) {
    return gpDatabase->getGameDB().DBCarData[index];
}

static inline const DBEntryBrand &GetBrandEntry( const int32_t index ) {
    return gpDatabase->getGameDB().DBBrand[index];
}

static void FormatModelName( const int32_t carIndex, char *pOutName ) {
    // FUN_00786510
    const DBEntryCarPhysicsData &car = GetCarEntry( carIndex );
    if ( car.CarFriendlyName.pAsString[0] != '?' ) {
        strncpy( pOutName, car.CarFriendlyName.pAsString, 0x80 );
        return;
    }
    if ( car.VersionName.pAsString[0] != '?' ) {
        snprintf( pOutName, 0x80, "%s %s", car.ModelName.pAsString,
                  car.VersionName.pAsString );
        return;
    }
    strncpy( pOutName, car.ModelName.pAsString, 0x80 );
}

static void BuildOwnedCategoryOptions( const int32_t vehicleType,
                                       GameParameterChoice *pChoice ) {
    // FUN_00786e20
    int32_t numOwnedCategories = 0;
    int32_t numCategories = 7;
    bool bCars = false;
    bool bBikes = false;
    switch ( vehicleType ) {
    case 0:
        bCars = true;
        bBikes = true;
        break;
    case 1:
        bCars = true;
        break;
    case 2:
        bBikes = true;
        numCategories = 2;
        break;
    default:
        numCategories = 2;
        break;
    }

    const DBEntryList &carDatabase = gpDatabase->getGameDB().Databases[1];
    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
    for ( int32_t category = 0; category < numCategories; category++ ) {
        for ( int32_t slot = 0; slot < Garage::kNumCarSlots; slot++ ) {
            GarageSlot &car = pGarage->pCars[slot];
            if ( !car.isValid() ||
                 car.state == CarDesc::eState::CDS_PutOnEbay ) {
                continue;
            }

            const DBEntryCarPhysicsData *pEntry =
                gpDatabase->getCarDBEntry( int32_t( car.getCarConfigID() ) );
            if ( pEntry == nullptr ) {
                continue;
            }

            const uint64_t groupHashcode =
                carDatabase.getHashcodeForString( pEntry->CarGroup.pAsString );
            const bool bCarMatch =
                bCars &&
                groupHashcode == kGameParameterCarCategoryOptions[category];
            const bool bBikeMatch =
                bBikes && category < 2 &&
                groupHashcode == kGameParameterBikeCategoryOptions[category];
            if ( bCarMatch || bBikeMatch ) {
                gOwnedCategoryOptions[numOwnedCategories++] =
                    bCars ? kGameParameterCarCategoryOptions[category]
                          : kGameParameterBikeCategoryOptions[category];
                break;
            }
        }
    }

    pChoice->setupOptions( 0, numOwnedCategories - 1, 0,
                           gOwnedCategoryOptions );
}

GameParameter::GameParameter()
    : value( 0 ),
      min( 0 ),
      max( 0 ),
      bClamp( true ),
      step( 1 ),
      typeIndex( GPT_Text ),
      bEnabled( false ),
      bTracksParent( false ),
      fileFlags( 0 ),
      pParent( nullptr ),
      pDependents{},
      numDependents( 0 ),
      dependencyFlags( 0u ),
      category( 0 ),
      id( 0u ),
      descriptionHashcode( 0u ),
      field5c( 0 ),
      gameTypeMask( 0u ),
      format( 0 ),
      field68( 0u ),
      pNext( nullptr ) {
    // FUN_00785670
}

const char *GameParameter::getDisplayText() {
    // FUN_0047a8a0
    return nullptr;
}

const char *GameParameter::increment( int32_t /*param_2*/ ) {
    // FUN_00785610
    value += step;
    if ( value > max ) {
        value = max;
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
    return getDisplayText();
}

const char *GameParameter::decrement( int32_t /*param_2*/ ) {
    // FUN_007855b0
    value -= step;
    if ( value < min ) {
        value = min;
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
    return getDisplayText();
}

const GameParameterTypeDesc *GameParameter::getTypeDesc() {
    // FUN_0042c7b0
    return &kTypeDescs[typeIndex];
}

const char *GameParameter::onClick( int32_t *pOutCheckBoxState ) {
    // FUN_00786750
    const char *pLabel = getTypeDesc()->pLabel;
    *pOutCheckBoxState = 0;
    if ( strcmp( pLabel, "input_txt" ) == 0 ) {
        increment( 0 );
        *pOutCheckBoxState = 1;
    } else if ( strcmp( pLabel, "input_txt_disable" ) == 0 ) {
        decrement( 0 );
        *pOutCheckBoxState = 1;
    } else if ( strcmp( pLabel, "chkbox_off" ) == 0 ) {
        increment( 0 );
        *pOutCheckBoxState = 1;
    } else if ( strcmp( pLabel, "chkbox_on" ) == 0 ) {
        decrement( 0 );
        *pOutCheckBoxState = 1;
    } else if ( fileFlags == 1 ) {
        fileFlags = 2;
        *pOutCheckBoxState = 2;
    } else if ( fileFlags == 2 ) {
        fileFlags = 1;
        *pOutCheckBoxState = 1;
    }
    return getTypeDesc()->pLabel;
}

uint32_t GameParameter::getSize() {
    // FUN_00423f90
    return sizeof( uint32_t );
}

bool GameParameter::serialize( void *pOutData ) {
    // FUN_0042c770
    WriteBigEndian32( pOutData, uint32_t( value ) );
    return true;
}

bool GameParameter::deserialize( const void *pData ) {
    // FUN_00786680
    value = int32_t( ReadBigEndian32( pData ) );
    if ( ( max != 0 || min != 0 ) && bClamp ) {
        if ( value < min ) {
            value = min;
        } else if ( value > max ) {
            value = max;
        }
    }
    refreshDependents();
    return true;
}

void GameParameter::setValue( const int32_t newValue, const bool bNotify ) {
    // FUN_0042c810
    value = newValue;
    if ( !bNotify ) {
        return;
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
}

void GameParameter::setRange( const int32_t newMin,
                              const int32_t newMax,
                              const int32_t newValue ) {
    // FUN_0042c750
    min = newMin;
    max = newMax;
    value = newValue;
}

void GameParameter::setMinMax( const int32_t newMin, const int32_t newMax ) {
    // FUN_0042c7c0
    min = newMin;
    max = newMax;
}

void GameParameter::setup( const int32_t newMin,
                           const int32_t newMax,
                           const int32_t newValue,
                           const int32_t newStep,
                           const bool bNewClamp ) {
    // FUN_007854c0
    min = newMin;
    max = newMax;
    value = newValue;
    bClamp = bNewClamp;
    step = newStep;
}

intptr_t GameParameter::getValue() {
    // FUN_00423f30
    return value;
}

bool GameParameter::refresh() {
    // FUN_00785540
    if ( pParent == nullptr || pParent->typeIndex < GPT_Bool ||
         pParent->typeIndex > GPT_BoolOn ) {
        return false;
    }

    const bool bInverted = ( ( dependencyFlags >> 1 ) & 1 ) != 0;
    bEnabled = ( pParent->getValue() == 0 ) ? bInverted : !bInverted;
    refreshDependents();
    return true;
}

bool GameParameter::onChildChanged( GameParameter * /*pChild*/ ) {
    // FUN_00607000
    return false;
}

bool GameParameter::registerTo( GameParameterList * /*pList*/ ) {
    // FUN_0042c6b0
    return true;
}

GameParameterList::GameParameterList()
    : pHead( nullptr ), pTail( nullptr ), count( 0 ) {}

int32_t GameParameterList::serialize( uint8_t **ppCursor ) {
    // FUN_00423cd0
    uint8_t *&pCursor = *ppCursor;
    WriteBigEndian32( pCursor, uint32_t( count ) );
    pCursor += 4;

    int32_t numWritten = 0;
    for ( GameParameter *pParameter = pHead; pParameter != nullptr;
          pParameter = pParameter->pNext ) {
        numWritten++;
        WriteBigEndian32( pCursor, pParameter->id );
        pCursor += 4;

        const uint32_t size = pParameter->getSize();
        Write<uint16_t>( pCursor, 0,
                         ReverseEndianness16( uint16_t( pParameter->format ) ) );
        pCursor += 2;
        Write<uint16_t>( pCursor, 0, ReverseEndianness16( uint16_t( size ) ) );
        pCursor += 2;
        WriteBigEndian32( pCursor, uint32_t( uint8_t( pParameter->fileFlags ) ) );
        pCursor += 4;

        pParameter->serialize( pCursor );
        pCursor += size;
    }
    return numWritten;
}

void GameParameterList::deserialize( const uint8_t **ppCursor,
                                     uint32_t *pVersion ) {
    // FUN_00423990
    const uint8_t *&pCursor = *ppCursor;
    const int32_t numRecords = int32_t( ReadBigEndian32( pCursor ) );
    pCursor += 4;

    for ( int32_t i = 0; i < numRecords; i++ ) {
        const uint8_t *pRecord = pCursor;
        const uint32_t recordID = ReadBigEndian32( pRecord );
        pCursor = pRecord + 4;

        GameParameter *pParameter = pHead;
        while ( pParameter != nullptr && pParameter->id != recordID ) {
            pParameter = pParameter->pNext;
        }

        if ( pParameter == nullptr ) {
            if ( *pVersion < 100000 ) {
                const uint16_t size =
                    ReverseEndianness16( ReadU16( pRecord + 6 ) );
                pCursor = pRecord + ( ( *pVersion > 0x203 ) ? 12 : 8 ) + size;
            } else {
                pCursor = pRecord + 8;
            }
            continue;
        }

        if ( *pVersion > 0x203 ) {
            const uint16_t size = ReverseEndianness16( ReadU16( pRecord + 6 ) );
            pCursor = pRecord + 8;
            if ( size != uint16_t( pParameter->getSize() ) ) {
                return;
            }
            pParameter->fileFlags = pCursor[3];
            pCursor += 4;
            pParameter->deserialize( pCursor );
            pCursor += size;
            continue;
        }

        const int32_t format = pParameter->format;
        uint32_t size = 0;
        if ( format == 0 || *pVersion == 0 || *pVersion > 99999 ) {
            size = pParameter->getSize();
        } else {
            size = ReverseEndianness16( ReadU16( pRecord + 6 ) );
            pCursor = pRecord + 8;
        }
        pParameter->deserialize( pCursor );
        pCursor += size & 0xffff;
        if ( format == 0 ) {
            *pVersion = uint32_t( pParameter->getValue() );
        }
    }
}

bool GameParameterVersion::serialize( void *pOutData ) {
    // FUN_0042c730
    WriteBigEndian32( pOutData, 0x217u );
    return true;
}

bool GameParameterVersion::deserialize( const void *pData ) {
    // FUN_00786e00
    GameParameter::deserialize( pData );
    gIGEFileVersion = value;
    return true;
}

GameParameterString::GameParameterString() : GameParameter(), pText{} {}

uint32_t GameParameterString::getSize() {
    // FUN_00441650
    return kMaxLength;
}

bool GameParameterString::serialize( void *pOutData ) {
    // FUN_0042c710
    memcpy( pOutData, pText, kMaxLength );
    return true;
}

bool GameParameterString::deserialize( const void *pData ) {
    // FUN_0042c6d0
    memcpy( pText, pData, kMaxLength );
    refreshDependents();
    return true;
}

intptr_t GameParameterString::getValue() {
    // FUN_00483430
    return reinterpret_cast<intptr_t>( pText );
}

GameParameterNumber::GameParameterNumber()
    : GameParameter(), pText{}, bIsMoney( false ) {
    typeIndex = GPT_Text;
    setRange( 0, 1, 0 );
}

const char *GameParameterNumber::getDisplayText() {
    // FUN_00787be0
    if ( bIsMoney ) {
        FormatMoneyString( pText, value );
        return pText;
    }
    snprintf( pText, sizeof( pText ), "%d", value );
    return pText;
}

const char *GameParameterTeamCount::increment( int32_t /*param_2*/ ) {
    // FUN_00787b50
    const char *pText = GameParameter::increment( 0 );
    if ( gpTestDriveInstance->getActiveGameMode() == GM_GameModeEdit ) {
        OTDU_UNIMPLEMENTED; // TODO: GAME EDITOR (FUN_0078bd80, FUN_0078cbc0,
                            // FUN_0078cf20: team setup refresh)
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
    return pText;
}

const char *GameParameterTeamCount::decrement( int32_t /*param_2*/ ) {
    // FUN_00787ac0
    const char *pText = GameParameter::decrement( 0 );
    if ( gpTestDriveInstance->getActiveGameMode() == GM_GameModeEdit ) {
        OTDU_UNIMPLEMENTED; // TODO: GAME EDITOR (FUN_0078bd80, FUN_0078cbc0,
                            // FUN_0078cf20: team setup refresh)
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
    return pText;
}

GameParameterBool::GameParameterBool() : GameParameter() {
    typeIndex = GPT_Bool;
    setRange( 0, 1, 0 );
}

const GameParameterTypeDesc *GameParameterBool::getTypeDesc() {
    // FUN_00785310
    return &kTypeDescs[typeIndex + value];
}

GameParameterLevel::GameParameterLevel() : GameParameter() {
    typeIndex = GPT_Level;
    setRange( -1, 5, 1 );
}

const GameParameterTypeDesc *GameParameterLevel::getTypeDesc() {
    // FUN_00785320
    if ( value == -1 ) {
        return &kTypeDescs[GPT_LevelFree];
    }
    return &kTypeDescs[GPT_Level + value];
}

GameParameterVector::GameParameterVector()
    : GameParameter(), vector( -989898.0f, -989898.0f, -989898.0f, -1.0f ) {}

uint32_t GameParameterVector::getSize() {
    // FUN_007851a0
    return sizeof( float ) * 4;
}

bool GameParameterVector::serialize( void *pOutData ) {
    // FUN_00785280
    float *pOutVector = static_cast<float *>( pOutData );
    pOutVector[0] = 0.0f;
    pOutVector[1] = 0.0f;
    pOutVector[2] = 0.0f;
    pOutVector[3] = 10.0f;
    return true;
}

bool GameParameterVector::deserialize( const void *pData ) {
    // FUN_007851b0
    const uint8_t *pBytes = static_cast<const uint8_t *>( pData );
    for ( int32_t i = 0; i < 4; i++ ) {
        vector[i] = BitsToFloat( ReadBigEndian32( pBytes + i * 4 ) );
    }
    return true;
}

GameParameterTime::GameParameterTime() : GameParameter(), pText{} {
    typeIndex = GPT_Time;
    setRange( 0, 86400000, 0 );
}

const char *GameParameterTime::getDisplayText() {
    // FUN_007852b0
    const int32_t seconds = value / 1000;
    snprintf( pText, sizeof( pText ), "%d:%02d", seconds / 60, seconds % 60 );
    return pText;
}

bool GameParameterTime::refresh() {
    // FUN_00785920
    if ( pParent == nullptr ) {
        return false;
    }
    if ( pParent->getTypeIndex() != GPT_Time ) {
        return GameParameter::refresh();
    }

    const int32_t parentValue = int32_t( pParent->getValue() );
    if ( parentValue >= getValueInt() ) {
        const int32_t newValue = std::min( step + parentValue, max );
        setValue( newValue, false );
        refreshDependents();
    }
    return true;
}

bool GameParameterTime::onChildChanged( GameParameter *pChild ) {
    // FUN_007858c0
    if ( pChild == nullptr ) {
        return false;
    }

    const int32_t childValue = int32_t( pChild->getValue() );
    if ( childValue <= getValueInt() ) {
        setValue( std::max( childValue - step, min ), false );
        if ( pParent != nullptr ) {
            pParent->onChildChanged( this );
        }
    }
    return true;
}

GameParameterSpeed::GameParameterSpeed() : GameParameter(), pText{} {
    // FUN_00785bb0
    typeIndex = GPT_Speed;
    min = 0;
    max = 40000;
    if ( gpPlayerData != nullptr && gpPlayerData->isUsingImperialUnits() ) {
        value = 6435;
        step = 804;
    } else {
        value = 5000;
        step = 500;
    }
}

const char *GameParameterSpeed::getDisplayText() {
    // FUN_00785b40
    const CommonDB &commonDatabase = gpDatabase->getCommonDB();
    float speed = float( value ) * 0.01f;
    const char *pUnit = commonDatabase.CommonStrings[1];
    if ( gpPlayerData->isUsingImperialUnits() ) {
        speed *= 0.621504f;
        pUnit = commonDatabase.CommonStrings[0];
    }
    snprintf( pText, sizeof( pText ), "%.2f %s", speed, pUnit );
    return pText;
}

bool GameParameterSpeed::refresh() {
    // FUN_00785ad0
    if ( pParent == nullptr || pParent->getTypeIndex() != GPT_Speed ) {
        return GameParameter::refresh();
    }

    const int32_t parentValue = int32_t( pParent->getValue() );
    if ( parentValue < getValueInt() ) {
        setValue( std::max( parentValue, min ), false );
        refreshDependents();
    }
    return false;
}

bool GameParameterSpeed::onChildChanged( GameParameter *pChild ) {
    // FUN_00785a80
    if ( pChild == nullptr ) {
        return false;
    }

    const int32_t childValue = int32_t( pChild->getValue() );
    if ( childValue > getValueInt() ) {
        setValue( std::min( childValue, max ), false );
        if ( pParent != nullptr ) {
            pParent->onChildChanged( this );
        }
    }
    return true;
}

GameParameterLaps::GameParameterLaps() : GameParameter(), pText{} {
    typeIndex = GPT_Text;
    setRange( 0, 99, 2 );
}

const char *GameParameterLaps::getDisplayText() {
    // FUN_00785cc0
    if ( value == 0 ) {
        return gpDatabase->getStringByHashcode( 0, kInfiniteLapsHashcode );
    }
    snprintf( pText, sizeof( pText ), "%d", value );
    return pText;
}

const char *GameParameterLaps::decrementLaps() {
    // FUN_00785d00
    if ( gpTestDriveInstance->getActiveGameMode() == GM_GameModeEdit ) {
        OTDU_UNIMPLEMENTED; // TODO: GAME EDITOR (min = 1 unless the edited
                            // mission is a race)
    }
    value -= step;
    if ( value < min ) {
        value = min;
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
    return getDisplayText();
}

GameParameterChoice::GameParameterChoice()
    : GameParameter(),
      choiceHashcode( 0ull ),
      pOptions( kGameParameterEmptyOptions ),
      databaseIndex( 0 ) {
    typeIndex = GPT_Choice;
    GameParameter::setRange( 0, 0, 0 );
}

int32_t GameParameterChoice::getParentVehicleType() const {
    // FUN_00787210 (inlined)
    const GameParameterChoice *pVehicleType =
        static_cast<const GameParameterChoice *>( pParent->getParent() );
    if ( pVehicleType == nullptr ) {
        return 0;
    }

    const uint64_t vehicleType = pVehicleType->getSelectedOption();
    if ( vehicleType == kGameParameterVehicleTypeOptions[0] ) {
        return 0;
    }
    if ( vehicleType == kGameParameterVehicleTypeOptions[1] ) {
        return 1;
    }
    if ( vehicleType == kGameParameterVehicleTypeOptions[2] ) {
        return 2;
    }
    return 0;
}

const char *GameParameterChoice::getDisplayText() {
    // FUN_00785e30
    uint64_t hashcode = kDefaultStringHashcode;
    if ( pOptions != nullptr ) {
        hashcode =
            ( value >= min && value <= max ) ? pOptions[value] : pOptions[0];
    }
    return gpDatabase->getStringByHashcode( uint32_t( databaseIndex ),
                                            hashcode );
}

bool GameParameterChoice::refresh() {
    // FUN_00787210
    if ( pParent == nullptr || pParent->getTypeIndex() != GPT_Choice ) {
        return false;
    }
    GameParameterChoice *pParentChoice =
        static_cast<GameParameterChoice *>( pParent );

    if ( choiceHashcode == 0ull ) {
        if ( bTracksParent ) {
            bEnabled = pParentChoice->isEnabled();
            if ( pParentChoice->getChoiceHashcode() ==
                 kGameParameterChoiceRange ) {
                const int32_t parentValue =
                    int32_t( pParentChoice->getValue() );
                if ( parentValue < getValueInt() ) {
                    setValue( std::max( parentValue, min ), false );
                }
            } else {
                setupOptions( 0, 0, 0, kGameParameterEmptyOptions );
            }
        }
        refreshDependents();
        return true;
    }

    if ( pParentChoice->getSelectedOption() != choiceHashcode ) {
        bEnabled = false;
        refreshDependents();
        return true;
    }

    bEnabled = true;
    if ( choiceHashcode == kGameParameterChoiceCategory ) {
        const int32_t vehicleType = getParentVehicleType();
        if ( vehicleType != 2 ) {
            if ( pOptions == kGameParameterCarCategoryOptions ) {
                if ( gbGameParameterOwnedCarsOnly ) {
                    BuildOwnedCategoryOptions( vehicleType, this );
                }
            } else if ( gbGameParameterOwnedCarsOnly ) {
                BuildOwnedCategoryOptions( vehicleType, this );
            } else {
                setupOptions( 0, 6, 0, kGameParameterCarCategoryOptions );
            }
        } else if ( pOptions == kGameParameterBikeCategoryOptions ) {
            if ( gbGameParameterOwnedCarsOnly ) {
                BuildOwnedCategoryOptions( 2, this );
            }
        } else if ( gbGameParameterOwnedCarsOnly ) {
            BuildOwnedCategoryOptions( 2, this );
        } else {
            setupOptions( 0, 1, 0, kGameParameterBikeCategoryOptions );
        }
    } else if ( choiceHashcode == kGameParameterChoiceRange ) {
        const int32_t vehicleType = getParentVehicleType();
        if ( vehicleType == 2 ) {
            bEnabled = false;
            refreshDependents();
            pParent->setValue( int32_t( pParent->getValue() ) - 1, true );
            return true;
        }

        if ( gbGameParameterOwnedCarsOnly ) {
            BuildOwnedCategoryOptions( vehicleType, this );
            if ( pDependents[0] != nullptr ) {
                BuildOwnedCategoryOptions(
                    vehicleType,
                    static_cast<GameParameterChoice *>( pDependents[0] ) );
            }
        }
    }

    refreshDependents();
    return true;
}

bool GameParameterChoice::onChildChanged( GameParameter *pChild ) {
    // FUN_00785e80
    if ( pChild == nullptr || pChild->getTypeIndex() != GPT_Choice ) {
        return false;
    }

    if ( choiceHashcode == kGameParameterChoiceRange ) {
        const int32_t childValue = int32_t( pChild->getValue() );
        if ( childValue > getValueInt() ) {
            setValue( std::min( childValue, max ), false );
        }
    }
    return true;
}

bool GameParameterChoice::setupOptions( const int32_t newMin,
                                        const int32_t newMax,
                                        const int32_t newValue,
                                        const uint64_t *pNewOptions ) {
    // FUN_0042c7e0
    setRange( newMin, newMax, newValue );
    pOptions = pNewOptions;
    return pNewOptions != nullptr;
}

GameParameterDBString::GameParameterDBString()
    : GameParameter(), hashcode( 0ull ), databaseIndex( 0 ) {
    typeIndex = GPT_Choice;
    GameParameter::setRange( 0, 0, 0 );
    value = -1;
}

const char *GameParameterDBString::getDisplayText() {
    // FUN_00786270
    const DBEntryList &database =
        gpDatabase->getGameDB().Databases[databaseIndex];
    if ( value < 0 || uint32_t( value ) >= database.NumItems ) {
        return GetDefaultString();
    }
    return database.pData + database.List[value].Offset;
}

uint32_t GameParameterDBString::getSize() {
    // FUN_0050d4d0
    return sizeof( uint64_t );
}

bool GameParameterDBString::serialize( void *pOutData ) {
    // FUN_00786080
    if ( value >= 0 ) {
        hashcode = gpDatabase->getGameDB()
                       .Databases[databaseIndex]
                       .List[value]
                       .Hashcode;
    } else {
        hashcode = 0ull;
    }
    WriteBigEndian64( pOutData, hashcode );
    return true;
}

bool GameParameterDBString::deserialize( const void *pData ) {
    // FUN_00785f30
    hashcode = ReadBigEndian64( pData );
    setValue( -1, false );
    if ( hashcode == 0ull ) {
        return true;
    }

    const DBEntryList &database =
        gpDatabase->getGameDB().Databases[databaseIndex];
    for ( uint32_t i = 0; i < database.NumItems; i++ ) {
        if ( database.List[i].Hashcode == hashcode ) {
            setValue( int32_t( i ), false );
            return true;
        }
    }
    return false;
}

const char *GameParameterDBString::incrementBy( const int32_t amount ) {
    // FUN_00786200
    if ( value < 0 ) {
        value = min;
    } else {
        value = std::min( value + amount, max );
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
    return getDisplayText();
}

const char *GameParameterDBString::decrementBy( const int32_t amount ) {
    // FUN_00786190
    if ( value < 0 ) {
        value = min;
    } else {
        value = std::max( value - amount, min );
    }
    if ( pParent != nullptr ) {
        pParent->onChildChanged( this );
    }
    refreshDependents();
    return getDisplayText();
}

void GameParameterDBString::setupDatabase( const int32_t newMin,
                                           const int32_t newMax,
                                           const int32_t newValue,
                                           const int32_t newDatabaseIndex ) {
    // FUN_0042c860
    setRange( newMin, newMax, newValue );
    databaseIndex = newDatabaseIndex;
}

GameParameterBrand::GameParameterBrand()
    : GameParameterChoice(), pBrandName( nullptr ), brandHashcode( 0ull ) {
    // FUN_007864b0
    typeIndex = GPT_Brand;
}

const char *GameParameterBrand::getDisplayText() {
    // FUN_00786310
    if ( pBrandName == nullptr ) {
        return GetDefaultString();
    }
    return pBrandName;
}

const char *GameParameterBrand::increment( int32_t /*param_2*/ ) {
    // FUN_00787820
    int32_t vehicleType = 0;
    if ( choiceHashcode == kGameParameterChoiceBrand ||
         choiceHashcode == kGameParameterChoiceModel ) {
        vehicleType = getParentVehicleType();
    }

    const int32_t brandIndex = gpDatabase->findNextSelectableBrand(
        getValueInt(), gbGameParameterOwnedCarsOnly, vehicleType );
    if ( brandIndex >= 0 ) {
        const DBEntryBrand &brand = GetBrandEntry( brandIndex );
        pBrandName = brand.BrandNameID.pAsString;
        brandHashcode = brand.Hashcode;
        setValue( brandIndex, true );
    }
    return getDisplayText();
}

const char *GameParameterBrand::decrement( int32_t /*param_2*/ ) {
    // FUN_007876f0
    int32_t vehicleType = 0;
    if ( choiceHashcode == kGameParameterChoiceBrand ||
         choiceHashcode == kGameParameterChoiceModel ) {
        vehicleType = getParentVehicleType();
    }

    const int32_t brandIndex = gpDatabase->findPreviousSelectableBrand(
        getValueInt(), gbGameParameterOwnedCarsOnly, vehicleType );
    if ( brandIndex >= 0 ) {
        const DBEntryBrand &brand = GetBrandEntry( brandIndex );
        pBrandName = brand.BrandNameID.pAsString;
        brandHashcode = brand.Hashcode;
        setValue( brandIndex, true );
    }
    return getDisplayText();
}

uint32_t GameParameterBrand::getSize() {
    // FUN_0050d4d0
    return sizeof( uint64_t );
}

bool GameParameterBrand::serialize( void *pOutData ) {
    // FUN_00785370
    WriteBigEndian64( pOutData, brandHashcode );
    return true;
}

bool GameParameterBrand::deserialize( const void *pData ) {
    // FUN_00786340
    brandHashcode = ReadBigEndian64( pData );
    setValue( -1, false );
    if ( brandHashcode == 0ull ) {
        return true;
    }

    const GameDatabase &database = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < database.pNumItems[6]; i++ ) {
        if ( getValue() != -1 ) {
            return false;
        }
        if ( database.DBBrand[i].Hashcode == brandHashcode ) {
            pBrandName = database.DBBrand[i].BrandNameID.pAsString;
            setValue( int32_t( i ), true );
            return true;
        }
    }
    return false;
}

bool GameParameterBrand::refresh() {
    // FUN_00787530
    if ( pParent == nullptr || pParent->getTypeIndex() != GPT_Choice ||
         choiceHashcode == 0ull ) {
        return true;
    }

    const GameParameterChoice *pParentChoice =
        static_cast<const GameParameterChoice *>( pParent );
    if ( pParentChoice->getSelectedOption() != choiceHashcode ) {
        bEnabled = false;
        refreshDependents();
        return true;
    }

    bEnabled = true;
    if ( choiceHashcode == kGameParameterChoiceBrand ||
         choiceHashcode == kGameParameterChoiceModel ) {
        const int32_t vehicleType = getParentVehicleType();
        if ( !gpDatabase->isBrandSelectable(
                 getValueInt(), gbGameParameterOwnedCarsOnly, vehicleType ) ) {
            const int32_t brandIndex = gpDatabase->findNextSelectableBrand(
                -1, gbGameParameterOwnedCarsOnly, vehicleType );
            if ( brandIndex < 0 ) {
                pBrandName = GetDefaultString();
                brandHashcode = 0ull;
                return false;
            }
            setValue( brandIndex, false );
        }

        const DBEntryBrand &brand = GetBrandEntry( getValueInt() );
        pBrandName = brand.BrandNameID.pAsString;
        brandHashcode = brand.Hashcode;
    }

    refreshDependents();
    return true;
}

GameParameterModel::GameParameterModel()
    : GameParameterChoice(),
      pModelName{},
      brandHashcode( 0ull ),
      carIndex( -1 ),
      brandCarIndex( 0 ) {
    // FUN_00786570
    GameParameter::setValue( -1, false );
}

const char *GameParameterModel::getDisplayText() {
    // FUN_00785440
    return pModelName;
}

const char *GameParameterModel::increment( int32_t /*param_2*/ ) {
    // FUN_00787080
    const int32_t newCarIndex = gpDatabase->findBrandCar(
        brandHashcode, brandCarIndex + 1, gbGameParameterOwnedCarsOnly );
    if ( newCarIndex >= 0 ) {
        carIndex = newCarIndex;
        brandCarIndex++;
        FormatModelName( carIndex, pModelName );
        setValue( GetCarEntry( carIndex ).CarID, false );
    }
    return getDisplayText();
}

const char *GameParameterModel::decrement( int32_t /*param_2*/ ) {
    // FUN_00786ff0
    const int32_t newCarIndex = gpDatabase->findBrandCar(
        brandHashcode, brandCarIndex - 1, gbGameParameterOwnedCarsOnly );
    if ( newCarIndex >= 0 ) {
        carIndex = newCarIndex;
        brandCarIndex--;
        FormatModelName( carIndex, pModelName );
        setValue( GetCarEntry( carIndex ).CarID, false );
    }
    return getDisplayText();
}

bool GameParameterModel::deserialize( const void *pData ) {
    // FUN_00786d60
    GameParameter::deserialize( pData );
    if ( getValue() == -1 ) {
        return false;
    }

    carIndex = gpDatabase->getCarIndex( getValueInt() );
    if ( carIndex == -1 ) {
        return false;
    }

    brandHashcode = GetCarEntry( carIndex ).Brand->Hashcode;
    FormatModelName( carIndex, pModelName );
    brandCarIndex =
        gpDatabase->getBrandCarIndex( brandHashcode, getValueInt() );
    return true;
}

bool GameParameterModel::refresh() {
    // FUN_00787110
    if ( pParent == nullptr || pParent->getTypeIndex() != GPT_Brand ) {
        return false;
    }

    const GameParameterBrand *pBrand =
        static_cast<const GameParameterBrand *>( pParent );
    bEnabled = pBrand->isEnabled();
    if ( !bEnabled ) {
        return false;
    }

    if ( pBrand->getBrandHashcode() != brandHashcode ) {
        brandHashcode = pBrand->getBrandHashcode();
        brandCarIndex = 0;
        carIndex = gpDatabase->findBrandCar( brandHashcode, brandCarIndex,
                                             gbGameParameterOwnedCarsOnly );
    }

    if ( carIndex < 0 ) {
        strcpy( pModelName, GetDefaultString() );
        return true;
    }

    setValue( GetCarEntry( carIndex ).CarID, false );
    FormatModelName( carIndex, pModelName );
    return true;
}

GameParameterVehicleRestriction::GameParameterVehicleRestriction()
    : GameParameterChoice(),
      brand(),
      modelBrand(),
      model(),
      categoryChoice(),
      categoryFrom(),
      categoryTo(),
      bFixedChildIDs( false ) {
    // FUN_0042db80
}

bool GameParameterVehicleRestriction::registerTo( GameParameterList *pList ) {
    // FUN_007869b0
    pList->add( &brand, bFixedChildIDs ? MakeParameterID( "VBra" ) : ( id + 1 ),
                format, category, kGameParameterChoiceBrand, 0, 0, 0xffff );
    pList->add( &modelBrand,
                bFixedChildIDs ? MakeParameterID( "VBrM" ) : ( id + 2 ), format,
                category, kGameParameterChoiceBrand, 0, 0, 0xffff );
    pList->add( &model, bFixedChildIDs ? MakeParameterID( "VMod" ) : ( id + 3 ),
                format, category, kGameParameterChoiceModel, 0, 0, 0xffff );
    pList->add( &categoryChoice,
                bFixedChildIDs ? MakeParameterID( "VSca" ) : ( id + 4 ), format,
                category, kGameParameterChoiceCategory, 0, 0, 0xffff );
    pList->add( &categoryFrom,
                bFixedChildIDs ? MakeParameterID( "VScf" ) : ( id + 5 ), format,
                category, 0x5e7ab5, 0, 0, 0xffff );
    pList->add( &categoryTo,
                bFixedChildIDs ? MakeParameterID( "VSct" ) : ( id + 6 ), format,
                category, 0x600155, 0, 0, 0xffff );

    brand.linkParent( this );
    categoryChoice.linkParent( this );
    modelBrand.linkParent( this );
    categoryFrom.linkParent( this );
    categoryTo.linkParent( &categoryFrom );
    categoryTo.setTracksParent( true );
    model.linkParent( &modelBrand );
    model.setTracksParent( true );

    brand.setChoiceHashcode( kGameParameterChoiceBrand );
    brand.setTracksParent( true );
    modelBrand.setChoiceHashcode( kGameParameterChoiceModel );
    modelBrand.setTracksParent( true );
    categoryFrom.setChoiceHashcode( kGameParameterChoiceRange );
    categoryFrom.setTracksParent( true );
    categoryChoice.setChoiceHashcode( kGameParameterChoiceCategory );
    categoryChoice.setTracksParent( true );

    categoryChoice.setDatabaseIndex( 1 );
    categoryFrom.setDatabaseIndex( 1 );
    categoryTo.setDatabaseIndex( 1 );
    return true;
}

bool GameParameterVehicleRestriction::setupOptions(
    const int32_t newMin,
    const int32_t newMax,
    const int32_t newValue,
    const uint64_t *pNewOptions ) {
    // FUN_007859d0
    setRange( newMin, newMax, newValue );
    categoryChoice.setupOptions( 0, 6, 0, kGameParameterCarCategoryOptions );
    categoryFrom.setupOptions( 0, 6, 6, kGameParameterCarCategoryOptions );
    categoryTo.setupOptions( 0, 6, 0, kGameParameterCarCategoryOptions );
    categoryTo.setValue( 0, false );
    refreshDependents();
    pOptions = pNewOptions;
    return pNewOptions != nullptr;
}
