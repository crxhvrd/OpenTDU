#include "shared.h"
#include "driver.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "game/scenaric/gs_scenaric.h"

Driver::Driver() : Notifiable() {
    // FUN_00893cb0
    memset( pNumCoupons, 0, sizeof( pNumCoupons ) );
    memset( pMoneyOP, 0, sizeof( pMoneyOP ) );
    memset( pCurrentHouse, 0, sizeof( pCurrentHouse ) );
    memset( pLocale, 0, sizeof( pLocale ) );
    memset( pLevel, 0, sizeof( pLevel ) );
    memset( ClubName, 0, sizeof( ClubName ) );
    memset( pOdometerKms, 0, sizeof( pOdometerKms ) );

    clear();
}

Driver::~Driver() {}

void Driver::clear() {
    // FUN_00891f30
    ID = 0xffffffffu;

    setMoney( 0u );
    setNumCoupons( 0u );
    setPendingMoneyOperation( 0 );

    ApplyMoneyOPPendingTimeLeft = -1.0f;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (StopGroup 'money')

    CurrentCar = 0u;
    Nickname[0] = '\0';

    setCurrentHouse( 0ull );

    TutorialState = 0;
    TutorialUserParam = 0;

    if ( gpScenaric != nullptr ) {
        gpScenaric->getIntroTutorial().reset();
    }

    field28_0x78 = 0u;

    setLocale( 0u );
    setLevel( 0.0f );
    setOdometerKms( 0.0f );

    ClubName[0] = '\0';

    TeamID = 0u;
    TeamSpotID = 0u;
    field4_0x24 = 0u;

    bCheatDetected = false;
}

void Driver::deserialize( edStore &stream ) {
    // FUN_00891ae0
    ID = stream.readUInteger();
    stream.readBufferNoAlloc( Nickname, kNicknameLength );

    pMoney[0] = stream.readULong();
    pMoney[1] = stream.readULong();
    pNumCoupons[0] = stream.readULong();
    pNumCoupons[1] = stream.readULong();
    pOdometerKms[0] = stream.readULong();
    pOdometerKms[1] = stream.readULong();

    CurrentCar = stream.readUShort();

    stream.readULong();

    pCurrentHouse[0] = stream.readULong();
    pCurrentHouse[1] = stream.readULong();
    field28_0x78 = stream.readULong();

    TutorialState = int32_t( stream.readUInteger() );
    TutorialUserParam = int32_t( stream.readUInteger() );

    bCheatDetected = stream.readInteger() != 0;

    stream.readStringNoAlloc( ClubName, kClubNameLength );

    notify( 0u );
}

void Driver::initialize( const char *pNickname,
                         const uint32_t id,
                         const uint32_t money,
                         const uint32_t numCoupons,
                         const uint16_t currentCar,
                         const uint32_t teamID,
                         const uint32_t teamSpotID,
                         const uint32_t param_9 ) {
    // FUN_00891df0
    if ( isValid() ) {
        return;
    }

    ID = id;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (money/coupon history)

    setMoney( money );
    setNumCoupons( numCoupons );

    CurrentCar = currentCar;
    strncpy( Nickname, pNickname, kNicknameLength );
    Nickname[kNicknameLength - 1] = '\0';

    field28_0x78 = 0u;
    TeamID = teamID;
    TeamSpotID = teamSpotID;
    field4_0x24 = param_9;

    setLocale( 0u ); // TODO: PLAYERSAVE (FUN_00879ec0)
    setLevel( 0.0f );

    notify( 0u );
}

uint32_t Driver::getMoney() const {
    // FUN_0040be30
    return TestDriveTEAReadDword( pMoney );
}

void Driver::setMoney( const uint32_t value ) {
    TestDriveTEAWriteDword( pMoney, value );
}

uint32_t Driver::getNumCoupons() const {
    return TestDriveTEAReadDword( pNumCoupons );
}

void Driver::setNumCoupons( const uint32_t value ) {
    TestDriveTEAWriteDword( pNumCoupons, value );
}

int32_t Driver::getPendingMoneyOperation() const {
    return int32_t( TestDriveTEAReadDword( pMoneyOP ) );
}

void Driver::setPendingMoneyOperation( const int32_t value ) {
    TestDriveTEAWriteDword( pMoneyOP, uint32_t( value ) );
}

uint64_t Driver::getCurrentHouse() const {
    return TestDriveTEAReadQword( pCurrentHouse );
}

void Driver::setCurrentHouse( const uint64_t hashcode ) {
    TestDriveTEAWriteQword( pCurrentHouse, hashcode );
}

uint32_t Driver::getLocale() const { return TestDriveTEAReadDword( pLocale ); }

void Driver::setLocale( const uint32_t value ) {
    TestDriveTEAWriteDword( pLocale, value );
}

float Driver::getLevel() const { return TestDriveTEAReadFloat( pLevel ); }

void Driver::setLevel( const float value ) {
    TestDriveTEAWriteFloat( pLevel, value );
}

float Driver::getOdometerKms() const {
    return TestDriveTEAReadFloat( pOdometerKms );
}

void Driver::setOdometerKms( const float value ) {
    TestDriveTEAWriteFloat( pOdometerKms, value );
}
