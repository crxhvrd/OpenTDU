#include "shared.h"
#include "local_player_data.h"

#include "config/gs_config.h"
#include "core/ed_store.h"
#include "render/gs_render.h"
#include "render/ed_render.h"
#include "render/vulkan/render_device.h"
#include "system/ed_sys.h"

void LocalPlayerData::clear() {
    // FUN_008980c0
    bAuto = true;
    StakeValue = 1000u;
    Linearity = 0.5f;
    QuiverSensitivity = 0.5f;
    SeatPositionHeight = 0.0f;
    SeatPositionPosition = 0.0f;
    Gamma = 0.5f;
    CameraShake = 0.0f;
    RadialBlurIntensity = 0.5f;
    DrivingHelpLevel = 1;
    bAutoClutch = true;
    PadConfig = 1u;
    bDispOP1 = true;
    bDisplayPlayerNames = true;
    bDisplayGPS = true;
    bGPSRotation = true;
    field10_0x30 = 0u;
    bStakeEnabled = false;
    bUseWheel = false;
    bInvertMouse = false;
    bHShifterEnabled = false;
    bTrackIREnabled = false;
    bFFBInvert = false;
    Language[0] = '\0';
    UnitSystem = US_Metric;
    bHelpSystem = false;
    bSubtitles = false;
    bOnlineAccount = false;

    memset( Nickname, 0, sizeof( Nickname ) );
    memset( Email, 0, sizeof( Email ) );
    memset( Password, 0, sizeof( Password ) );

    InputBindings.clear();

    bOnlineOps[0] = true; // TODO: ONLINE (gLobbyNetworkService._56248_4_ == 0)
    bOnlineOps[1] = true;
    bOnlineOps[2] = false;
    bOnlineOps[3] = true;
    bOnlineOps[4] = true;
    bOnlineOps[5] = true;
    bOnlineOps[6] = true;
    bOnlineOps[7] = true;
    bOnlineOps[8] = true;

    field89274_0x15d05 = false;
    field89275_0x15d06 = false;
    field89276_0x15d07 = false;
    field89277_0x15d08 = false;

    if ( gpRender != nullptr ) {
        RenderWidth = gpRender->getWidth();
        RenderHeight = gpRender->getHeight();
        LODQuality = gpRender->getLODQuality();
    }
    RefreshRate = uint32_t( gPickedRefreshRate );
    AntiAliasing = eAntiAliasingMethod::AAM_Disabled;
    bHDREnabled = false;

    if ( gEdRender.pRenderDevice != nullptr ) {
        GPUAdapterDesc adapterInfos = {};
        if ( gEdRender.pRenderDevice->retrieveAdapterInfos( 0u,
                                                            &adapterInfos ) ) {
            GPUDeviceId = adapterInfos.DeviceID;
            GPUSubSysId = adapterInfos.SubSysId;
            GPUDriverRevision = adapterInfos.Revision;
            GPUVendorId = adapterInfos.VendorId;
        }
    }

    VoicesVolume = 1.0f;
    VoicesThreshold = 0.16f;
    bUseLoopBack = false;
    VoicesInput = 0.5f;
    bEnablePlayerVoices = true;
}

bool LocalPlayerData::loadFromStream( edStore &stream ) {
    // FUN_00897d50
    stream.readBufferNoAlloc( Language, sizeof( Language ) );
    if ( StringCmpI( Language, "gb" ) == 0 ) {
        Language[0] = 'u';
        Language[1] = 's';
        Language[2] = '\0';
    }

    UnitSystem = eUnitSystem( stream.readInteger() );
    bDispOP1 = stream.readInteger() != 0;
    bDisplayPlayerNames = stream.readInteger() != 0;
    bDisplayGPS = stream.readInteger() != 0;
    bGPSRotation = stream.readInteger() != 0;
    bHelpSystem = stream.readInteger() != 0;

    Gamma = stream.readFloat();
    RadialBlurIntensity = stream.readFloat();
    CameraShake = stream.readFloat();

    bSubtitles = stream.readInteger() != 0;
    bAuto = stream.readInteger() != 0;

    PadSensitivity = stream.readFloat();
    BarSensitivity = stream.readFloat();
    MouseSensitivity = stream.readFloat();
    QuiverSensitivity = stream.readFloat();

    DrivingHelpLevel = stream.readInteger();
    field10_0x30 = uint32_t( stream.readInteger() );
    bStakeEnabled = stream.readInteger() != 0;
    StakeValue = uint32_t( stream.readInteger() );
    bUseWheel = stream.readInteger() != 0;

    Linearity = stream.readFloat();

    bInvertMouse = stream.readInteger() != 0;
    bAutoClutch = stream.readInteger() != 0;
    bHShifterEnabled = stream.readInteger() != 0;
    bTrackIREnabled = stream.readInteger() != 0;
    bFFBInvert = stream.readInteger() != 0;

    SeatPositionHeight = stream.readFloat();
    SeatPositionPosition = stream.readFloat();

    field36_0x64 = stream.readInteger();
    PadConfig = uint32_t( stream.readInteger() );
    bOnlineAccount = stream.readInteger() != 0;
    bOffline = stream.readInteger() != 0;

    stream.readBufferNoAlloc( Nickname, sizeof( Nickname ) );
    stream.readBufferNoAlloc( Email, sizeof( Email ) );
    stream.readBufferNoAlloc( Password, sizeof( Password ) );

    InputBindings.deserialize( stream );
    OTDU_LOG_DEBUG( "Restored %d input binding(s) from the savegame\n",
                    InputBindings.NumEntries );

    for ( int32_t i = 0; i < 9; i++ ) {
        bOnlineOps[i] = stream.readByte() != 0;
    }

    RenderWidth = stream.readUInteger();
    RenderHeight = stream.readUInteger();
    RefreshRate = stream.readUInteger();
    AntiAliasing = eAntiAliasingMethod( stream.readUInteger() );
    bHDREnabled = stream.readUInteger() != 0;
    LODQuality = stream.readUInteger();

    GPUDeviceId = stream.readULong();

    // The original writes those two out byte/short at a time.
    GPUSubSysId = uint32_t( stream.readUShort() );
    GPUSubSysId |= uint32_t( stream.readUShort() ) << 16u;

    GPUDriverRevision = 0u;
    for ( int32_t i = 0; i < 4; i++ ) {
        GPUDriverRevision |= uint32_t( stream.readByte() ) << ( i * 8 );
    }

    GPUVendorId = 0u;
    for ( int32_t i = 0; i < 4; i++ ) {
        GPUVendorId |= uint32_t( stream.readByte() ) << ( i * 8 );
    }

    VoicesVolume = stream.readFloat();
    VoicesThreshold = stream.readFloat();
    VoicesInput = stream.readFloat();

    bUseLoopBack = stream.readByte() != 0;
    bEnablePlayerVoices = stream.readByte() != 0;

    return true;
}

void LocalPlayerData::serialize( edStore &stream ) {
    // FUN_00897a20
    stream.writeBuffer( Language, sizeof( Language ) );

    stream.writeInteger( int32_t( UnitSystem ) );
    stream.writeInteger( bDispOP1 );
    stream.writeInteger( bDisplayPlayerNames );
    stream.writeInteger( bDisplayGPS );
    stream.writeInteger( bGPSRotation );
    stream.writeInteger( bHelpSystem );

    stream.writeFloat( Gamma );
    stream.writeFloat( RadialBlurIntensity );
    stream.writeFloat( CameraShake );

    stream.writeInteger( bSubtitles );
    stream.writeInteger( bAuto );

    stream.writeFloat( PadSensitivity );
    stream.writeFloat( BarSensitivity );
    stream.writeFloat( MouseSensitivity );
    stream.writeFloat( QuiverSensitivity );

    stream.writeInteger( DrivingHelpLevel );
    stream.writeInteger( int32_t( field10_0x30 ) );
    stream.writeInteger( bStakeEnabled );
    stream.writeInteger( int32_t( StakeValue ) );
    stream.writeInteger( bUseWheel );

    stream.writeFloat( Linearity );

    stream.writeInteger( bInvertMouse );
    stream.writeInteger( bAutoClutch );
    stream.writeInteger( bHShifterEnabled );
    stream.writeInteger( bTrackIREnabled );
    stream.writeInteger( bFFBInvert );

    stream.writeFloat( SeatPositionHeight );
    stream.writeFloat( SeatPositionPosition );

    stream.writeInteger( field36_0x64 );
    stream.writeInteger( int32_t( PadConfig ) );
    stream.writeInteger( bOnlineAccount );
    stream.writeInteger( bOffline );

    stream.writeBuffer( Nickname, sizeof( Nickname ) );
    stream.writeBuffer( Email, sizeof( Email ) );
    stream.writeBuffer( Password, sizeof( Password ) );

    InputBindings.serialize( stream );

    for ( int32_t i = 0; i < 9; i++ ) {
        stream.writeByte( bOnlineOps[i] ? 1u : 0u );
    }

    stream.writeInteger( int32_t( RenderWidth ) );
    stream.writeInteger( int32_t( RenderHeight ) );
    stream.writeInteger( int32_t( RefreshRate ) );
    stream.writeInteger( int32_t( AntiAliasing ) );
    stream.writeInteger( bHDREnabled );
    stream.writeInteger( int32_t( LODQuality ) );

    stream.writeInteger( int32_t( GPUDeviceId ) );

    // The original writes those two out short/byte at a time.
    stream.writeUShort( uint16_t( GPUSubSysId ) );
    stream.writeUShort( uint16_t( GPUSubSysId >> 16u ) );

    for ( int32_t i = 0; i < 4; i++ ) {
        stream.writeByte( uint8_t( GPUDriverRevision >> ( i * 8 ) ) );
    }

    for ( int32_t i = 0; i < 4; i++ ) {
        stream.writeByte( uint8_t( GPUVendorId >> ( i * 8 ) ) );
    }

    stream.writeFloat( VoicesVolume );
    stream.writeFloat( VoicesThreshold );
    stream.writeFloat( VoicesInput );

    stream.writeByte( bUseLoopBack ? 1u : 0u );
    stream.writeByte( bEnablePlayerVoices ? 1u : 0u );
}
