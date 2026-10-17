#include "shared.h"
#include "gs_car_colors.h"

#include "gs_car_common.h"
#include "paint.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "core/maths_helper.h"

GSCarColors *gpCarColors = nullptr;

static constexpr float kSpecularContribution = 0.2f; // DAT_00f41730
static constexpr float kColorSaturation = 1.0f;      // FLOAT_00f4165c
static constexpr float kVelourColorDivisor = 2.0f;   // DAT_00f416b0

static Eigen::Vector4f MakeStaticParamColor( const float *pParamData ) {
    return Eigen::Vector4f( pParamData[0], pParamData[1], pParamData[2], 1.0f );
}

static Eigen::Vector4f SaturateColor( const Eigen::Vector4f &color ) {
    Eigen::Vector4f saturated;
    for ( int32_t i = 0; i < 4; i++ ) {
        saturated[i] = Clamp( color[i], 0.0f, kColorSaturation );
    }
    return saturated;
}

static Eigen::Vector4f
GetMaterialDominantColorCarInterior( Material *pMaterial ) {
    // FUN_008b33a0
    return SaturateColor(
        ( pMaterial->AmbientColor + pMaterial->DiffuseColor ) /
            kVelourColorDivisor +
        pMaterial->SpecularColor * kSpecularContribution );
}

static Eigen::Vector4f
GetMaterialDominantColorCarMaterial( Material *pMaterial ) {
    // FUN_008b34e0
    MaterialShaderParameterArray *pParameters =
        pMaterial->getShaderParameters();
    if ( pParameters == nullptr ) {
        return Eigen::Vector4f::Zero();
    }

    const float *pFirstColor = pParameters->getStaticParamDataPS( 0x7a );
    const float *pSecondColor = pParameters->getStaticParamDataPS( 0x7b );
    const float *pBlendFactor = pParameters->getStaticParamDataPS( 0x7c );
    if ( pFirstColor == nullptr ) {
        return Eigen::Vector4f::Zero();
    }

    Eigen::Vector4f color = MakeStaticParamColor( pFirstColor );

    if ( pSecondColor != nullptr && pBlendFactor != nullptr ) {
        color = Vector4DLerp( color, MakeStaticParamColor( pSecondColor ),
                              pBlendFactor[0] * kSpecularContribution );
    }

    color += pMaterial->SpecularColor * kSpecularContribution;
    return SaturateColor( color );
}

static Eigen::Vector4f GetMaterialDominantColorCarPaint( Material *pMaterial ) {
    // FUN_008b3180
    MaterialShaderParameterArray *pParameters =
        pMaterial->getShaderParameters();
    if ( pParameters == nullptr ) {
        return Eigen::Vector4f::Zero();
    }

    const float *pFirstColor = pParameters->getStaticParamDataPS( 0x7a );
    const float *pSecondColor = pParameters->getStaticParamDataPS( 0x7b );
    const float *pBlendFactor = pParameters->getStaticParamDataPS( 0xd8 );
    if ( pFirstColor == nullptr || pSecondColor == nullptr ||
         pBlendFactor == nullptr ) {
        return Eigen::Vector4f::Zero();
    }

    Eigen::Vector4f color =
        Vector4DLerp( MakeStaticParamColor( pFirstColor ),
                      MakeStaticParamColor( pSecondColor ),
                      pBlendFactor[0] * kSpecularContribution );

    color += pMaterial->SpecularColor * kSpecularContribution;
    return SaturateColor( color );
}

GSCarColors::GSCarColors()
    : GameSystem(),
      Bank(),
      collection2D(),
      render2DM(),
      p2DMFileResource( nullptr ) {
    // FUN_008b37a0
    gpCarColors = this;
}

GSCarColors::~GSCarColors() {
    // FUN_008b3720
}

bool GSCarColors::initialize( TestDriveGameInstance * ) {
    // FUN_008b3c70
    if ( bLoaded ) {
        return true;
    }

    std::string colorBankPath = gpConfig->getResRootPath();
    colorBankPath += "Vehicules/colors.bnk";

    bool bLoadingResult = loadBank( gpFile, colorBankPath.c_str() );
    if ( !bLoadingResult ) {
        return false;
    }

    loadContentAsync();

    p2DMFileResource = (char *)getFirstEntry( AG_Common, AT_2DM, nullptr );
    if ( p2DMFileResource == nullptr ||
         !render2DM.initialize( p2DMFileResource ) ) {
        OTDU_LOG_ERROR( "GSCarColors .2DM (%s) not found!\n",
                        colorBankPath.c_str() );
        return false;
    }

    collection2D.register2DM( &render2DM );
    if ( gpCarCommon != nullptr ) {
        gpCarCommon->getCollection2D().bindBitmapReferences( &render2DM );
    }
    return true;
}

ColorRGBA GSCarColors::getColor( const uint64_t colorHashcode ) {
    // FUN_008b38a0
    const Eigen::Vector4f color = getColorFloat( colorHashcode );

    ColorRGBA result;
    result.R = uint8_t( uint64_t( color.x() * 255.0f ) );
    result.G = uint8_t( uint64_t( color.y() * 255.0f ) );
    result.B = uint8_t( uint64_t( color.z() * 255.0f ) );
    result.A = uint8_t( uint64_t( color.w() * 255.0f ) );
    return result;
}

Eigen::Vector4f GSCarColors::getColorFloat( const uint64_t colorHashcode ) {
    // FUN_008b3a70
    Eigen::Vector4f color( 0.0f, 0.0f, 0.0f, 1.0f );

    if ( render2DM.getMaterialArray() == nullptr ) {
        return color;
    }

    RenderFile::Section *pColorSection = render2DM.getMaterial( colorHashcode );
    if ( pColorSection == nullptr ) {
        return color;
    }

    Material *pColorMaterial = (Material *)( pColorSection + 1 );
    MaterialParameter *pParameters = pColorMaterial->getParameterByIndex( 0 );
    if ( pParameters == nullptr ) {
        return color;
    }

    const uint64_t shaderGUID = pParameters->Hashcode;
    if ( IsCarInteriorShaderGUID( shaderGUID ) ) {
        color = GetMaterialDominantColorCarInterior( pColorMaterial );
    } else if ( IsCarMaterialShaderGUID( shaderGUID ) ) {
        color = GetMaterialDominantColorCarMaterial( pColorMaterial );
    } else if ( IsCarPaintShaderGUID( shaderGUID ) ) {
        color = GetMaterialDominantColorCarPaint( pColorMaterial );
    }

    return color;
}

void GSCarColors::terminate() {
    collection2D.unregister2DM( &render2DM );
    render2DM.destroy();

    TestDrive::Free( pBankFile );
    pBankFile = nullptr;
    bLoaded = false;
}
