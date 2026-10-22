#include "shared.h"
#include "gs_config.h"

#include "world/weather/gs_weather.h"
#include "world/otdu_lights.h"

static WeatherConfig *gpParsingWeatherConfig = nullptr; // DAT_010e7294
static WeatherDesc *gpParsingWeatherDesc = nullptr;     // DAT_010e7290

void GSConfig::registerWeatherCommands() {
    registerCommand( "WEATHERENABLED", [&]( const char *pArg ) {
        // FUN_00984140
        gpWeather->setEnabled( atoi( pArg ) != 0 );
    } );

    registerCommand( "SETDEFAULTWEATHERDESC", [&]( const char *pArg ) {
        // FUN_00984120
        std::string argStr( pArg );
        size_t it = 0;
        std::string descName = ParseString( argStr, it );

        gpWeather->setDefaultWeatherDesc( descName.c_str() );
    } );

    registerCommand( "SETWEATHERDESC", [&]( const char *pArg ) {
        // FUN_009840d0
        std::string argStr( pArg );
        size_t it = 0;

        int32_t descIndex = ParseInteger( argStr, it );
        std::string descName = ParseString( argStr, it );

        if ( -1 < descIndex && descIndex < 31 && !descName.empty() ) {
            gpWeather->setWeatherDesc( descIndex, descName.c_str() );
        }
    } );

    registerCommand( "TIMESCALE", [&]( const char *pArg ) {
        // FUN_009840b0
        gpWeather->setTimeScale( static_cast<float>( atof( pArg ) ) );
    } );

    registerCommand( "NAME", [&]( const char *pArg ) {
        // FUN_00987400
        std::string argStr( pArg );
        size_t it = 0;

        std::string descName = ParseString( argStr, it );

        gpParsingWeatherDesc =
            gpWeather->getWeatherDescByName( descName.c_str() );
        if ( gpParsingWeatherDesc == nullptr ) {
            gpParsingWeatherDesc =
                new ( TestDrive::Alloc( sizeof( WeatherDesc ) ) ) WeatherDesc();
            gpWeather->registerDesc( gpParsingWeatherDesc );
            gpParsingWeatherDesc->setName( descName );
            return;
        }

        gpParsingWeatherDesc->clearConfigs();
    } );

    registerCommand( "STATICCFG", [&]( const char *pArg ) {
        // FUN_00988880

        // StaticCfg   =    "DEFAULT"
        // "Interior"
        //                   ^                                   ^
        //                   Cfg                                'HDR' Cfg
        //                   (optional)
        if ( gpParsingWeatherDesc != nullptr ) {
            std::string argStr( pArg );
            size_t it = 0;

            std::string name = ParseString( argStr, it );
            std::string hdrName = ParseString( argStr, it );

            gpParsingWeatherDesc->registerStaticConfigs( name.c_str(),
                                                         hdrName.c_str() );
        }
    } );

    registerCommand( "DYNAMICCFG", [&]( const char *pArg ) {
        // FUN_009840a0
        // NOP
    } );

    registerCommand( "WEATHERCONFIGNAME", [&]( const char *pArg ) {
        // FUN_00989310
        std::string argStr( pArg );
        size_t it = 0ull;

        std::string parsedName = ParseString( argStr, it );
        if ( parsedName.empty() ) {
            gpParsingWeatherConfig = nullptr;
            return;
        }

        std::string weatherCfgName = ( gpConfig->bHDRWeather ) ? "HDR_" : "";
        weatherCfgName += parsedName;

        int32_t weatherIndex =
            gpWeather->getWeatherConfigIndex( weatherCfgName.c_str() );
        gpParsingWeatherConfig =
            gpWeather->getWeatherConfigByIndex( weatherIndex );
        if ( gpParsingWeatherConfig == nullptr ) {
            gpParsingWeatherConfig =
                new ( TestDrive::Alloc( sizeof( WeatherConfig ) ) )
                    WeatherConfig( weatherCfgName.c_str(), bHDRWeather );
            gpWeather->registerConfig( gpParsingWeatherConfig );
        }
    } );

    registerCommand( "GLOBALAMBIENTCOLOR", [&]( const char *pArg ) {
        // FUN_00988820
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setGlobalAmbientColor( color );
    } );

    registerCommand( "SUNAMBIENTCOLOR", [&]( const char *pArg ) {
        // FUN_009887c0
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setSunAmbientColor( color );
    } );

    registerCommand( "SUNDIFFUSECOLOR", [&]( const char *pArg ) {
        // FUN_00988760
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setSunDiffuseColor( color );
    } );

    registerCommand( "SUNSPECULARCOLOR", [&]( const char *pArg ) {
        // FUN_00988700
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setSunSpecularColor( color );
    } );

    registerCommand( "SUNDIR", [&]( const char *pArg ) {
        // FUN_00988650
        if ( gpParsingWeatherConfig != nullptr ) {
            std::string argStr( pArg );
            size_t it = 0ull;

            Eigen::Vector4f sunDir = ParseVector4( argStr, it );
            sunDir.normalize();
            gpParsingWeatherConfig->setSunDirection( sunDir );
        }
    } );

    registerCommand( "SUNDIRT2ANGLE", [&]( const char *pArg ) {
        // FUN_00989110
        OTDU_UNIMPLEMENTED // TODO: Unused so not bothering decompiling this one
                           // (for now)
    } );

    registerCommand( "OCEANAMPLITUDE", [&]( const char *pArg ) {
        // FUN_00984080
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setOceanAmplitude(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "WINDSPEED", [&]( const char *pArg ) {
        // FUN_00984050
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            if ( 0.0f <= value ) {
                gpParsingWeatherConfig->setWindSpeed( value );
            }
        }
    } );

    registerCommand( "WINDDIR", [&]( const char *pArg ) {
        // FUN_009890c0
        if ( gpParsingWeatherConfig != nullptr ) {
            std::string argStr( pArg );
            size_t it = 0ull;

            Eigen::Vector4f winDir = Eigen::Vector4f::Zero();
            winDir.x() = ParseFloat( argStr, it );
            winDir.z() = -ParseFloat( argStr, it );

            gpParsingWeatherConfig->setWindDirection( winDir );
        }
    } );

    registerCommand( "CLOUDSPERLINCOVER", [&]( const char *pArg ) {
        // FUN_009873c0
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            value = Clamp( value, 0.0f, 1.0f );

            gpParsingWeatherConfig->setCloudsCoverage( value );
        }
    } );

    registerCommand( "CLOUDSPERLINSHARPNESS", [&]( const char *pArg ) {
        // FUN_00987380
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            value = Clamp( value, 1e-06f, 1.0f );

            gpParsingWeatherConfig->setCloudsSharpness( value );
        }
    } );

    registerCommand( "CLOUDSPERLINOPACIFIER", [&]( const char *pArg ) {
        // FUN_00987340
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            value = Clamp( value, 1e-06f, 10000.0f );

            gpParsingWeatherConfig->setCloudsNoPacifier( value );
        }
    } );

    registerCommand( "CLOUDSPERLINCOLOR", [&]( const char *pArg ) {
        // FUN_009885f0
        if ( gpParsingWeatherConfig != nullptr ) {
            std::string argStr( pArg );
            size_t it = 0ull;

            Eigen::Vector4f color = ParseVector4( argStr, it );
            gpParsingWeatherConfig->setCloudsColor( color );
        }
    } );

    registerCommand( "CLOUDSPERLINSCALEU", [&]( const char *pArg ) {
        // FUN_00984020
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            value = Max( value, 1e-05f );

            gpParsingWeatherConfig->setCloudsScaleU( value );
        }
    } );

    registerCommand( "CLOUDSPERLINSCALEV", [&]( const char *pArg ) {
        // FUN_00983ff0
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            value = Max( value, 1e-05f );

            gpParsingWeatherConfig->setCloudsScaleV( value );
        }
    } );

    registerCommand( "CLOUDSPERLINALPHASTART", [&]( const char *pArg ) {
        // FUN_00983fd0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCloudsAlphaStart(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CLOUDSPERLINALPHALENGTH", [&]( const char *pArg ) {
        // FUN_00983fa0
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            value = Max( value, 0.001f );

            gpParsingWeatherConfig->setCloudsAlphaLength( value );
        }
    } );

    registerCommand( "FOGSTART", [&]( const char *pArg ) {
        // FUN_00983f80
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setFogStart(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "FOGEND", [&]( const char *pArg ) {
        // FUN_00983f60
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setFogEnd(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "FOGMAX", [&]( const char *pArg ) {
        // FUN_00983f40
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setFogMax(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "FOGCOLOR_ADD", [&]( const char *pArg ) {
        // NOTE: This is unimplemented in TDU1 (despite existing in the .ini
        // files). This stub exists to prevent warning spam in stdout
    } );

    registerCommand( "FOGCOLOR", [&]( const char *pArg ) {
        // FUN_00988590
        if ( gpParsingWeatherConfig != nullptr ) {
            std::string argStr( pArg );
            size_t it = 0ull;

            Eigen::Vector4f color = ParseVector4( argStr, it );
            gpParsingWeatherConfig->setFogColor( color );
        }
    } );

    registerCommand( "SUNMARKERSIZE", [&]( const char *pArg ) {
        // FUN_00983f20
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSunMarkerSize(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "SUNCORECOLOR", [&]( const char *pArg ) {
        // FUN_00988520
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setSunCoreColor( color );
    } );

    registerCommand( "SUNZSURFCOLOR",
                     [&]( const char *pArg ) { // Alias of "SUNCORECOLOR"
                         // FUN_00988520
                         if ( gpParsingWeatherConfig == nullptr ) {
                             return;
                         }

                         std::string argStr( pArg );
                         size_t it = 0ull;

                         Eigen::Vector4f color = ParseVector4( argStr, it );
                         gpParsingWeatherConfig->setSunCoreColor( color );
                     } );

    registerCommand( "SUNHALOCOLOR", [&]( const char *pArg ) {
        // FUN_009884b0
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setSunHaloColor( color );
    } );

    registerCommand( "SUNCOEFF", [&]( const char *pArg ) {
        // FUN_00983da0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSunCoef(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "SUNCOEFFHALO", [&]( const char *pArg ) {
        // FUN_00983f00
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSunCoefHalo(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "SUNDAZZLETHRESHOLD", [&]( const char *pArg ) {
        // FUN_00983ee0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSunDazzleThreshold(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "SUNDAZZLEDECREASESPEED", [&]( const char *pArg ) {
        // FUN_00983ec0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSunDazzleDecreaseSpeed(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "DOME2DB", [&]( const char *pArg ) {
        // FUN_00983e90
        std::string argStr( pArg );
        size_t it = 0ull;

        std::string filename = ParseString( argStr, it );
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setDome2DBFilename( filename );
        }
    } );

    registerCommand( "DOMEEMISSIVE", [&]( const char *pArg ) {
        // FUN_00988440
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setDomeEmissive( color );
    } );

    registerCommand( "CLOUDSEMISSIVE", [&]( const char *pArg ) {
        // FUN_00983e70
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCloudsEmissive(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "SUNEMISSIVE", [&]( const char *pArg ) {
        // FUN_00983e50
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSunEmissive(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "FILTER2DB", [&]( const char *pArg ) {
        // FUN_00983e20
        std::string argStr( pArg );
        size_t it = 0ull;

        std::string filename = ParseString( argStr, it );
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setFilter2DBFilename( filename );
        }
    } );

    registerCommand( "SUN2DB", [&]( const char *pArg ) {
        // FUN_00983df0
        std::string argStr( pArg );
        size_t it = 0ull;

        std::string filename = ParseString( argStr, it );
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSun2DBFilename( filename );
        }
    } );

    registerCommand(
        "SUNCORE2DB",
        [&]( const char *pArg ) { // TODO: Alias of "SUN2DB"; might worth adding
                                  // support for that to avoid dupe
            // FUN_00983df0
            std::string argStr( pArg );
            size_t it = 0ull;

            std::string filename = ParseString( argStr, it );
            if ( gpParsingWeatherConfig != nullptr ) {
                gpParsingWeatherConfig->setSun2DBFilename( filename );
            }
        } );

    registerCommand( "SUNHALO2DB", [&]( const char *pArg ) {
        // FUN_00983dc0
        std::string argStr( pArg );
        size_t it = 0ull;

        std::string filename = ParseString( argStr, it );
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setSunHalo2DBFilename( filename );
        }
    } );

    registerCommand( "HEIGHTMAPCOLOR", [&]( const char *pArg ) {
        // FUN_009883e0
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        gpParsingWeatherConfig->setHeightmapColor( color );
    } );

    registerCommand( "HEIGHTMAPCONTRAST", [&]( const char *pArg ) {
        // FUN_00983d80
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setHeightmapContrast(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CONTRAST", [&]( const char *pArg ) {
        // FUN_00983d60
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setContrast(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "DESATURATION", [&]( const char *pArg ) {
        // FUN_00987300
        if ( gpParsingWeatherConfig != nullptr ) {
            float value = static_cast<float>( atof( pArg ) );
            value = Clamp( value, 0.0f, 1.0f );
            gpParsingWeatherConfig->setDesaturation(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "BALANCE", [&]( const char *pArg ) {
        // FUN_00987270
        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector3f vec3 = Eigen::Vector3f::Zero();
        vec3.x() = ParseFloat( argStr, it );
        vec3.x() = Clamp( vec3.x(), -1.0f, 1.0f );

        vec3.y() = ParseFloat( argStr, it );
        vec3.y() = Clamp( vec3.y(), -1.0f, 1.0f );

        vec3.z() = ParseFloat( argStr, it );
        vec3.z() = Clamp( vec3.z(), -1.0f, 1.0f );

        gpParsingWeatherConfig->setBalance( vec3 );
    } );

    registerCommand( "CLOUDNAME", [&]( const char *pArg ) {
        // FUN_00983d20
        if ( gpParsingWeatherConfig != nullptr ) {
            std::string argStr( pArg );
            size_t it = 0ull;

            std::string cloudName = ParseString( argStr, it );
            gpParsingWeatherConfig->addCloudName( cloudName );
        }
    } );

    registerCommand( "CLOUDCOLOR", [&]( const char *pArg ) {
        // FUN_00988370
        if ( gpParsingWeatherConfig == nullptr ) {
            return;
        }

        std::string argStr( pArg );
        size_t it = 0ull;

        Eigen::Vector4f color = ParseVector4( argStr, it );
        color.w() = 1.0f;
        gpParsingWeatherConfig->addCloudColor( color );
    } );

    registerCommand( "CLOUD3DNAME",
                     [&]( const char *pArg ) { // Alias "CLOUDNAME"
                         // FUN_00983d20
                         if ( gpParsingWeatherConfig != nullptr ) {
                             std::string argStr( pArg );
                             size_t it = 0ull;

                             std::string cloudName = ParseString( argStr, it );
                             gpParsingWeatherConfig->addCloudName( cloudName );
                         }
                     } );

    registerCommand( "CLOUD3DCOLOR",
                     [&]( const char *pArg ) { // Alias "CLOUDCOLOR"
                                               // FUN_00988370
                         if ( gpParsingWeatherConfig == nullptr ) {
                             return;
                         }

                         std::string argStr( pArg );
                         size_t it = 0ull;

                         Eigen::Vector4f color = ParseVector4( argStr, it );
                         color.w() = 1.0f;
                         gpParsingWeatherConfig->addCloudColor( color );
                     } );

    registerCommand( "CARGLOBALFACTOR", [&]( const char *pArg ) {
        // FUN_00983d00
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarGlobalFactor(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARGLOBALENV", [&]( const char *pArg ) {
        // FUN_00983ce0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarGlobalEnv(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARLUMMIN", [&]( const char *pArg ) {
        // FUN_00983cc0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarLumMin(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARLUMMAX", [&]( const char *pArg ) {
        // FUN_00983ca0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarLumMax(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARAMBIANTFACTOR", [&]( const char *pArg ) {
        // FUN_00983c80
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarAmbiantFactor(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARDIFFUSEFACTOR", [&]( const char *pArg ) {
        // FUN_00983c60
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarDiffuseFactor(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARSPECULARFACTOR", [&]( const char *pArg ) {
        // FUN_00983c40
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarSpecularFactor(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARREFLECTIONFACTOR", [&]( const char *pArg ) {
        // FUN_00983c20
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarReflectionFactor(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARREFLECTIONLUM", [&]( const char *pArg ) {
        // FUN_00983c00
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarReflectionLum(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "CARCLOUDSGROUNDFACTOR", [&]( const char *pArg ) {
        // FUN_00983be0
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setCarCloudsGroundFactor(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    // OTDU_ADDITION: night time parameters (see WeatherConfig::getNightParams).
    registerCommand( "OTDU_UNLITSCALE", [&]( const char *pArg ) {
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setUnlitScale(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "OTDU_STARS", [&]( const char *pArg ) {
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setStarBrightness(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "OTDU_MOON", [&]( const char *pArg ) {
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setMoonBrightness(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    registerCommand( "OTDU_LAMPS", [&]( const char *pArg ) {
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setLampIntensity(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    // OTDU_HDREXPOSURE stops: HDR exposure offset
    registerCommand( "OTDU_HDREXPOSURE", [&]( const char *pArg ) {
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setHDRExposure(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    // OTDU_EMISSIVE scale: unlit signs (emissive color or OTDU_EMISSIVETEXTURE)
    // use this scale instead of OTDU_UNLITSCALE.
    registerCommand( "OTDU_EMISSIVE", [&]( const char *pArg ) {
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setEmissiveScale(
                static_cast<float>( atof( pArg ) ) );
        }
    } );

    // OTDU_EMISSIVETEXTURE "TEXTURE": unlit materials whose base texture is
    // TEXTURE are emissive.
    registerCommand( "OTDU_EMISSIVETEXTURE", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0;
        const std::string textureName = ParseString( argStr, it );
        if ( textureName.empty() ) {
            OTDU_LOG_WARN( "Invalid OTDU_EMISSIVETEXTURE '%s'\n", pArg );
            return;
        }
        RegisterOTDUEmissiveTexture(
            GetIdentifier64bit( textureName.c_str() ) );
    } );

    // OTDU_LIGHTMODEL "MODEL" x y z r g b radius (object space offset).
    registerCommand( "OTDU_LIGHTMODEL", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0;
        std::string modelName = ParseString( argStr, it );
        float values[7] = {};
        if ( sscanf( argStr.c_str() + it, "%f %f %f %f %f %f %f", &values[0],
                     &values[1], &values[2], &values[3], &values[4], &values[5],
                     &values[6] ) != 7 ||
             modelName.empty() ) {
            OTDU_LOG_WARN( "Invalid OTDU_LIGHTMODEL '%s'\n", pArg );
            return;
        }
        OTDULightModel model;
        // A model name or its hashcode (w/ 0x prefix)
        model.Hashcode = ( modelName.rfind( "0x", 0 ) == 0 )
                             ? strtoull( modelName.c_str(), nullptr, 16 )
                             : GetIdentifier64bit( modelName.c_str() );
        model.Offset = Eigen::Vector3f( values[0], values[1], values[2] );
        model.Color = Eigen::Vector3f( values[3], values[4], values[5] );
        model.Radius = values[6];
        RegisterOTDULightModel( model );
    } );

    // OTDU_HEADLIGHTS r g b lowBeamRange highBeamRange outerAngle innerAngle
    // pitch intensity [tailLightLevel] (ranges in meters, cone half angles and
    // pitch in degrees).
    registerCommand( "OTDU_HEADLIGHTS", [&]( const char *pArg ) {
        float values[10] = {};
        values[9] = OTDUHeadlightConfig().TailLightLevel;
        if ( sscanf( pArg, "%f %f %f %f %f %f %f %f %f %f", &values[0],
                     &values[1], &values[2], &values[3], &values[4], &values[5],
                     &values[6], &values[7], &values[8], &values[9] ) < 9 ) {
            OTDU_LOG_WARN( "Invalid OTDU_HEADLIGHTS '%s'\n", pArg );
            return;
        }
        OTDUHeadlightConfig config;
        config.Color = Eigen::Vector3f( values[0], values[1], values[2] );
        config.LowBeamRange = values[3];
        config.HighBeamRange = values[4];
        config.OuterAngle = values[5];
        config.InnerAngle = values[6];
        config.Pitch = values[7];
        config.Intensity = values[8];
        config.TailLightLevel = values[9];
        SetOTDUHeadlightConfig( config );
    } );

    // OTDU_WINDOWS warm(r g b) litFraction cool(r g b) coolRatio intensity:
    // the default lit window profile.
    // OTDU_WINDOWPROFILE "PREFIX" (same values): models whose name starts
    // with PREFIX
    auto parseWindowProfile = []( const char *pValues,
                                  OTDUWindowProfile &profile ) {
        float values[9] = {};
        if ( sscanf( pValues, "%f %f %f %f %f %f %f %f %f", &values[0],
                     &values[1], &values[2], &values[3], &values[4], &values[5],
                     &values[6], &values[7], &values[8] ) != 9 ) {
            return false;
        }
        profile.WarmColor = Eigen::Vector3f( values[0], values[1], values[2] );
        profile.LitFraction = values[3];
        profile.CoolColor = Eigen::Vector3f( values[4], values[5], values[6] );
        profile.CoolRatio = values[7];
        profile.Intensity = values[8];
        return true;
    };
    registerCommand( "OTDU_WINDOWS", [=]( const char *pArg ) {
        OTDUWindowProfile profile;
        if ( !parseWindowProfile( pArg, profile ) ) {
            OTDU_LOG_WARN( "Invalid OTDU_WINDOWS '%s'\n", pArg );
            return;
        }
        RegisterOTDUWindowProfile( profile );
    } );
    registerCommand( "OTDU_WINDOWPROFILE", [=]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0;
        OTDUWindowProfile profile;
        profile.Prefix = ParseString( argStr, it );
        if ( profile.Prefix.empty() ||
             !parseWindowProfile( argStr.c_str() + it, profile ) ) {
            OTDU_LOG_WARN( "Invalid OTDU_WINDOWPROFILE '%s'\n", pArg );
            return;
        }
        RegisterOTDUWindowProfile( profile );
    } );

    // OTDU_WINDOWEXCLUDE "TEXTURE": materials whose gloss map is TEXTURE get
    // no lit windows.
    registerCommand( "OTDU_WINDOWEXCLUDE", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0;
        const std::string textureName = ParseString( argStr, it );
        if ( textureName.empty() ) {
            OTDU_LOG_WARN( "Invalid OTDU_WINDOWEXCLUDE '%s'\n", pArg );
            return;
        }
        ExcludeOTDUWindowTexture( GetIdentifier64bit( textureName.c_str() ) );
    } );

    // OTDU_WINDOWSPOTRADIUS meters: no lit windows within this distance of a
    // building spot (every world spot but the challenges).
    registerCommand( "OTDU_WINDOWSPOTRADIUS", [&]( const char *pArg ) {
        SetOTDUWindowSpotRadius( static_cast<float>( atof( pArg ) ) );
    } );

    registerCommand( "OTDU_BAKEDSHADOW", [&]( const char *pArg ) {
        if ( gpParsingWeatherConfig != nullptr ) {
            gpParsingWeatherConfig->setBakedShadowStrength(
                static_cast<float>( atof( pArg ) ) );
        }
    } );
}
