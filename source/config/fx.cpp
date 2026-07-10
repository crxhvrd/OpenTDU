#include "shared.h"
#include "gs_config.h"

#include "render/mng_reflection.h"
#include "render/postfx/effects/postfx_hdri.h"

#include "game/gm_base.h"
#include "game/gm_world.h"

#include "world/mng_ocean.h"
#include "world/mng_tall_grass.h"

#include "world/weather/gs_weather.h"

#include "game/avatar/gs_avatar.h"

static HDRIConfig *gpParsingHDRConfig = nullptr; // DAT_010e7298

void GSConfig::registerFXCommands() {
    registerCommand( "FXO_REFLECTIONBLURINESS", [&]( const char *pArg ) {
        // FUN_00983a10
        gDefaultOceanSetup.ReflectionBluriness =
            static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXO_SPECULARPOWER", [&]( const char *pArg ) {
        // FUN_009839f0
        gDefaultOceanSetup.SpecularPower = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXO_BLURINESS_ATTENUATION", [&]( const char *pArg ) {
        // FUN_009839d0
        gDefaultOceanSetup.AttenuationPerturbation =
            static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXO_FRESNEL_ATTENUATION", [&]( const char *pArg ) {
        // FUN_009839b0
        gDefaultOceanSetup.AttenuationFresnel =
            static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXO_NMAP_LR", [&]( const char *pArg ) {
        // FUN_00983990
        gDefaultOceanSetup.NMapCoeffLR = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXO_NMAP_HR", [&]( const char *pArg ) {
        // FUN_00983970
        gDefaultOceanSetup.NMapCoeffHR = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXO_NMAP_SCALER", [&]( const char *pArg ) {
        // FUN_00983950
        gDefaultOceanSetup.NMapHeightScaler =
            static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXO_FRESNEL", [&]( const char *pArg ) {
        // FUN_00983920
        size_t it = 0;
        std::string argStr( pArg );

        gDefaultOceanSetup.FresnelBias = ParseFloat( argStr, it );
        gDefaultOceanSetup.FresnelPower = ParseFloat( argStr, it );
    } );

    registerCommand( "FXO_USE_REFLECTION_MAP", [&]( const char *pArg ) {
        // FUN_00983910
        // NOP
    } );

    registerCommand( "FXO_REFLECTION_COLOR", [&]( const char *pArg ) {
        // FUN_009838d0
        size_t it = 0;
        std::string argStr( pArg );

        gDefaultOceanSetup.ReflectionColor.x() = ParseFloat( argStr, it );
        gDefaultOceanSetup.ReflectionColor.y() = ParseFloat( argStr, it );
        gDefaultOceanSetup.ReflectionColor.z() = ParseFloat( argStr, it );
    } );

    registerCommand( "FXB_LUMTHRESHOLD", [&]( const char *pArg ) {
        // FUN_009838c0
        // NOP
    } );

    registerCommand( "FXB_BLURNBPASS", [&]( const char *pArg ) {
        // FUN_009838b0
        // NOP
    } );

    registerCommand( "FXB_BLURSIZE", [&]( const char *pArg ) {
        // FUN_009838a0
        // NOP
    } );

    registerCommand( "FXB_MODCOEFF0", [&]( const char *pArg ) {
        // FUN_00983890
        // NOP
    } );

    registerCommand( "FXB_MODCOEFF1", [&]( const char *pArg ) {
        // FUN_00983880
        // NOP
    } );

    registerCommand( "FXP_FADEIN_DELAY", [&]( const char *pArg ) {
        // FUN_00983860
        gFXPFadeInDelay = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXP_FADEOUT_DELAY", [&]( const char *pArg ) {
        // FUN_00983840
        gFXPFadeOutDelay = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXP_LUMTHRESHOLD", [&]( const char *pArg ) {
        // FUN_00983820
        gFXPLumThreshold = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXP_BLURSIZE", [&]( const char *pArg ) {
        // FUN_00983800
        gFXPBlurSize = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXP_MODCOEFF0", [&]( const char *pArg ) {
        // FUN_009837e0
        gFXPModCoeff0 = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXP_MODCOEFF1", [&]( const char *pArg ) {
        // FUN_009837c0
        gFXPModCoeff1 = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXHDRI_GAMEMODE", [&]( const char *pArg ) {
        // FUN_00987230
        const int32_t gameMode = atoi( pArg );
        if ( gameMode >= 0 && gameMode < GM_Count ) {
            gHDRIGameMode[gameMode] = true;
        }
    } );

    registerCommand( "FXHDRI_MIN_AVG_LUM", [&]( const char *pArg ) {
        // FUN_009837a0
        gFXHDRIMinAvgLum = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXHDRI_MAX_AVG_LUM", [&]( const char *pArg ) {
        // FUN_00983780
        gFXHDRIMaxAvgLum = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXHDRI_DO_ADAPTATION", [&]( const char *pArg ) {
        // FUN_009836e0
        gFXHDRIDoAdaptation = atoi( pArg ) != 0;
    } );

    registerCommand( "FXHDRI_ADAPTATION_TIME", [&]( const char *pArg ) {
        // FUN_00983760
        gFXHDRIAdaptationTime = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXHDRI_CONTRAST", [&]( const char *pArg ) {
        // FUN_00983740
        gFXHDRIContrast = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXHDRI_MIDDLE_GRAY", [&]( const char *pArg ) {
        // FUN_00983720
        gFXHDRIMiddleGray = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXHDRI_SATURATION", [&]( const char *pArg ) {
        // FUN_00983700
        gFXHDRISaturation = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "FXHDRI_GLOW_BLUR_NBPASS", [&]( const char *pArg ) {
        // FUN_009836c0
        gFXHDRIGlowBlurNBPass = static_cast<uint32_t>( atoi( pArg ) );
    } );

    registerCommand( "FXHDRI_GLOW_BLUR_KERNEL_SZ", [&]( const char *pArg ) {
        // FUN_009836a0
        gFXHDRIGlowBlurKernelSZ = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "HDRCONF_NAME", [&]( const char *pArg ) {
        // FUN_009871f0
        gpParsingHDRConfig = gpWeather->registerHDRConfig();
        if ( gpParsingHDRConfig != nullptr ) {
            std::string argStr( pArg );
            size_t it = 0;

            std::string name = ParseString( argStr, it );
            if ( name.size() > 15 ) {
                name.resize( 15 );
            }
            gpParsingHDRConfig->Name = name;
        }
    } );

    registerCommand( "HDRCONF_GLOW_MOD", [&]( const char *pArg ) {
        // FUN_00983680
        if ( gpParsingHDRConfig != nullptr ) {
            gpParsingHDRConfig->GlowMod = static_cast<float>( atof( pArg ) );
        }
    } );

    registerCommand( "HDRCONF_STAR_MOD", [&]( const char *pArg ) {
        // FUN_00983660
        if ( gpParsingHDRConfig != nullptr ) {
            gpParsingHDRConfig->StarMod = static_cast<float>( atof( pArg ) );
        }
    } );

    registerCommand( "HDRCONF_BRIGHT_THRESHOLD_GLOW", [&]( const char *pArg ) {
        // FUN_00983640
        if ( gpParsingHDRConfig != nullptr ) {
            gpParsingHDRConfig->BrightThresholdGlow =
                static_cast<float>( atof( pArg ) );
        }
    } );

    registerCommand( "HDRCONF_BRIGHT_THRESHOLD_STAR", [&]( const char *pArg ) {
        // FUN_00983620
        if ( gpParsingHDRConfig != nullptr ) {
            gpParsingHDRConfig->BrightThresholdStar =
                static_cast<float>( atof( pArg ) );
        }
    } );

    registerCommand( "FXREFL_BG_COLOR", [&]( const char *pArg ) {
        // FUN_00986e80
        size_t it = 0;
        std::string argStr( pArg );

        gReflectionBackgroundColor.R =
            static_cast<uint8_t>( ParseFloat( argStr, it ) * 0xff );
        gReflectionBackgroundColor.G =
            static_cast<uint8_t>( ParseFloat( argStr, it ) * 0xff );
        gReflectionBackgroundColor.B =
            static_cast<uint8_t>( ParseFloat( argStr, it ) * 0xff );
        gReflectionBackgroundColor.A = 0xff;
    } );

    registerCommand( "AVATAROUTCARLIGHTFACTOR", [&]( const char *pArg ) {
        // FUN_00983b70
        size_t it = 0;
        std::string argStr( pArg );

        for ( int32_t i = 0; i < 6; i++ ) {
            gAvatarOutCarLightFactor[i] = ParseFloat( argStr, it );
        }
    } );

    registerCommand( "AVATARCONVERTIBLELIGHTFACTOR", [&]( const char *pArg ) {
        // FUN_00983b00
        size_t it = 0;
        std::string argStr( pArg );

        for ( int32_t i = 0; i < 6; i++ ) {
            gAvatarConvertibleLightFactor[i] = ParseFloat( argStr, it );
        }
    } );

    registerCommand( "AVATARINCARLIGHTFACTOR", [&]( const char *pArg ) {
        // FUN_00983a90
        size_t it = 0;
        std::string argStr( pArg );

        for ( int32_t i = 0; i < 6; i++ ) {
            gAvatarInCarLightFactor[i] = ParseFloat( argStr, it );
        }
    } );

    registerCommand( "NBELTULTRANEAR", [&]( const char *pArg ) {
        // FUN_00987790
        gGrassNumElementUltraNear = atoi( pArg );
    } );

    registerCommand( "NBELTNEAR", [&]( const char *pArg ) {
        // FUN_00987770
        gGrassNumElementNear = atoi( pArg );
    } );

    registerCommand( "NBELTFAR", [&]( const char *pArg ) {
        // FUN_00987750
        gGrassNumElementFar = atoi( pArg );
    } );

    registerCommand( "ALPHASTARTULTRANEAR", [&]( const char *pArg ) {
        // FUN_00987610
        gGrassAlphaStartUltraNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "ALPHALENGTHULTRANEAR", [&]( const char *pArg ) {
        // FUN_009875f0
        gGrassAlphaLengthUltraNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALESTARTULTRANEAR", [&]( const char *pArg ) {
        // FUN_009876d0
        gGrassAlphaScaleUltraNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALELENGTHULTRANEAR", [&]( const char *pArg ) {
        // FUN_009876b0
        gGrassAlphaScaleLengthUltraNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALEFACTORULTRANEAR", [&]( const char *pArg ) {
        // FUN_00987730
        gGrassAlphaScaleFactorUltraNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "ALPHASTARTNEAR", [&]( const char *pArg ) {
        // FUN_009875d0
        gGrassAlphaStartNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "ALPHALENGTHNEAR", [&]( const char *pArg ) {
        // FUN_009875b0
        gGrassAlphaLengthNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALESTARTNEAR", [&]( const char *pArg ) {
        // FUN_00987690
        gGrassAlphaScaleNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALELENGTHNEAR", [&]( const char *pArg ) {
        // FUN_00987670
        gGrassAlphaScaleLengthNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALEFACTORNEAR", [&]( const char *pArg ) {
        // FUN_00987710
        gGrassAlphaScaleFactorNear = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "ALPHASTARTFAR", [&]( const char *pArg ) {
        // FUN_00987590
        gGrassAlphaStartFar = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "ALPHALENGTHFAR", [&]( const char *pArg ) {
        // FUN_00987570
        gGrassAlphaLengthFar = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALESTARTFAR", [&]( const char *pArg ) {
        // FUN_00987650
        gGrassAlphaScaleFar = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALELENGTHFAR", [&]( const char *pArg ) {
        // FUN_00987630
        gGrassAlphaScaleLengthFar = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SCALEFACTORFAR", [&]( const char *pArg ) {
        // FUN_009876f0
        gGrassAlphaScaleFactorFar = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "ALPHAREF", [&]( const char *pArg ) {
        // FUN_00987550
        gGrassAlphaRef = atoi( pArg );
    } );

    registerCommand( "HEIGHTGRASS", [&]( const char *pArg ) {
        // FUN_00987530
        gGrassHeight = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "WIDTHRATIOGRASS", [&]( const char *pArg ) {
        // FUN_00987510
        gGrassWidthRatio = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SIZEDELTAGRASS", [&]( const char *pArg ) {
        // FUN_009874f0
        gGrassSizeDelta = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "HEIGHTBUSHES", [&]( const char *pArg ) {
        // FUN_009874d0
        gBushesHeight = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "WIDTHRATIOBUSHES", [&]( const char *pArg ) {
        // FUN_009874b0
        gBushesWidthRatio = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "SIZEDELTABUSHES", [&]( const char *pArg ) {
        // FUN_00987490
        gBushesSizeDelta = static_cast<float>( atof( pArg ) );
    } );
}
