#include "shared.h"
#include "car_desc.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "database/gs_database.h"

static const CarConfig *FindCarConfig( const int32_t configID ) {
    // FUN_008916f0 (inlined)
    if ( gpDatabase == nullptr ) {
        return nullptr;
    }

    const std::vector<CarConfig> &carList = gpDatabase->getCarList();
    for ( const CarConfig &carConfig : carList ) {
        if ( carConfig.Hash == configID ) {
            return &carConfig;
        }
    }

    return nullptr;
}

CarDesc::CarDesc() : Notifiable() {
    // FUN_00894fa0
    clear();
}

CarDesc::~CarDesc() {}

void CarDesc::clear() {
    // FUN_00891900
    setCarConfigID( 0u );

    carID = 0xffffffffu;
    state = eState::CDS_Invalid;
    pConfig = nullptr;

    setUpgradeRate( 0.0f );

    playTime = 0.0;

    paintIndex = -1;
    rimID = -1;
    interiorID = -1;
    packHashcode = 0xffffffffffffffffull;

    paintType = ePaintType::PT_Default;
    price = 0;
    odometerKms = 0.0f;
}

void CarDesc::deserialize( edStore &stream ) {
    // FUN_008916f0
    carID = stream.readULong();

    pCarConfigID[0] = stream.readULong();
    pCarConfigID[1] = stream.readULong();

    state = eState( stream.readByte() );
    packHashcode = stream.readULongLong();

    const uint32_t configID = getCarConfigID();
    pConfig = FindCarConfig( int32_t( configID ) );

    if ( pConfig == nullptr ) {
        const int32_t fallbackConfigID =
            ( configID >= uint32_t( kFirstBikeConfigID ) &&
              configID < uint32_t( kFirstAfterBikeConfigID ) )
                ? kDefaultBikeConfigID
                : kDefaultCarConfigID;
        pConfig = FindCarConfig( fallbackConfigID );
    }

    pUpgradeRate[0] = stream.readULong();
    pUpgradeRate[1] = stream.readULong();

    paintType = ePaintType( stream.readInteger() );

    paintIndex = int32_t( stream.readByte() );
    if ( uint32_t( paintIndex ) == kUnsetByteID ) {
        paintIndex = -1;
    }

    rimID = int32_t( stream.readByte() );
    if ( uint32_t( rimID ) == kUnsetByteID ) {
        rimID = -1;
    }

    interiorID = int32_t( stream.readByte() );
    if ( uint32_t( interiorID ) == kUnsetByteID ) {
        interiorID = -1;
    }

    ColorRGBA *pDeluxeColors[4] = { &deluxeMainColor, &deluxeSecondColor,
                                    &deluxeLogoColor, &deluxeSecondColor2 };
    for ( ColorRGBA *pColor : pDeluxeColors ) {
        for ( int32_t i = 0; i < 4; i++ ) {
            ( *pColor )[i] = stream.readByte();
        }
    }

    price = stream.readInteger();
    odometerKms = stream.readFloat();
    playTime = stream.readDouble();

    notify( 0u );
}

void CarDesc::serialize( edStore &stream ) const {
    // FUN_00891570
    stream.writeInteger( int32_t( carID ) );
    stream.writeInteger( int32_t( pCarConfigID[0] ) );
    stream.writeInteger( int32_t( pCarConfigID[1] ) );

    stream.writeByte( uint8_t( state ) );
    stream.writeULongLong( packHashcode );

    stream.writeInteger( int32_t( pUpgradeRate[0] ) );
    stream.writeInteger( int32_t( pUpgradeRate[1] ) );

    stream.writeInteger( int32_t( paintType ) );
    stream.writeByte( uint8_t( paintIndex ) );
    stream.writeByte( uint8_t( rimID ) );
    stream.writeByte( uint8_t( interiorID ) );

    const ColorRGBA *pDeluxeColors[4] = { &deluxeMainColor, &deluxeSecondColor,
                                          &deluxeLogoColor,
                                          &deluxeSecondColor2 };
    for ( const ColorRGBA *pColor : pDeluxeColors ) {
        stream.writeByte( pColor->R );
        stream.writeByte( pColor->G );
        stream.writeByte( pColor->B );
        stream.writeByte( pColor->A );
    }

    stream.writeInteger( price );
    stream.writeFloat( odometerKms );
    stream.writeDouble( playTime );
}

void CarDesc::setArchetype( const uint32_t configID ) {
    // FUN_00891320
    setCarConfigID( configID );
    state = eState::CDS_None;

    pConfig = FindCarConfig( int32_t( getCarConfigID() ) );

    setUpgradeRate( 0.0f );

    paintIndex = -1;
    rimID = -1;
    interiorID = -1;
    paintType = ePaintType::PT_Default;

    notify( 0u );
}

void CarDesc::setup( const uint32_t configID,
                     const int32_t paintIndex,
                     const int32_t rimID,
                     const int32_t interiorID,
                     const uint32_t carID ) {
    // FUN_008913f0
    if ( isValid() ) {
        // Only ever fills a free slot.
        return;
    }

    this->carID = carID;
    setCarConfigID( configID );
    state = eState::CDS_None;

    pConfig = FindCarConfig( int32_t( getCarConfigID() ) );

    setUpgradeRate( 0.0f );

    this->paintIndex = paintIndex;
    packHashcode = 0xffffffffffffffffull;
    paintType = ePaintType::PT_Default;
    this->rimID = rimID;
    this->interiorID = interiorID;
    price = 0;

    notify( 0u );
}

uint32_t CarDesc::getCarConfigID() const {
    return TestDriveTEAReadDword( pCarConfigID );
}

void CarDesc::setCarConfigID( const uint32_t configID ) {
    TestDriveTEAWriteDword( pCarConfigID, configID );
}

void CarDesc::setConfigFromID( const uint32_t configID ) {
    // FUN_008914f0
    setCarConfigID( configID );

    pConfig = FindCarConfig( int32_t( getCarConfigID() ) );
}

float CarDesc::getUpgradeRate() const {
    return TestDriveTEAReadFloat( pUpgradeRate );
}

int32_t CarDesc::getPackLevel( int32_t *pOutMaxLevel ) const {
    // FUN_008954a0
    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( int32_t( getCarConfigID() ) );

    return gpDatabase->getPackLevel( pCarEntry, packHashcode, pOutMaxLevel );
}

void CarDesc::setUpgradeRate( const float upgradeRate ) {
    TestDriveTEAWriteFloat( pUpgradeRate, upgradeRate );
}
