#pragma once

// OTDU_ADDITION: night time point lights attached to world models (street
// lamps). Light models are declared by a mod pack (OTDU_LIGHTMODEL, see
// FX/Lights_OpenTDU.ini); every mass instance of such a model gets a light,
// and the nearest ones are handed to the rebuilt world shaders each frame
// (see RenderContext, kLampRegisterPS).

#include "shared.h"

#include <string>

struct ImpostorGrid;
class Instance;

struct OTDULightModel {
    uint64_t Hashcode = 0;
    Eigen::Vector3f Offset = Eigen::Vector3f::Zero(); // object space
    Eigen::Vector3f Color = Eigen::Vector3f::Ones();
    float Radius = 0.0f;
};

// Absolute world position (xyz) and light model index (w).
using OTDUPlacedLight = Eigen::Vector4f;

void RegisterOTDULightModel( const OTDULightModel &model );
const OTDULightModel *FindOTDULightModel( uint64_t hashcode,
                                          int32_t *pOutIndex );

// Called for every mass instance of a light model (instance matrix in the
// streaming frame, row vectors).
void PlaceOTDULight( ImpostorGrid &grid,
                     int32_t modelIndex,
                     const Matrix4x4f_t &instanceMatrix,
                     const Eigen::Vector4f &streamOrigin );

// Picks the lights closest to the camera (render space) among the loaded
// sectors and publishes them to gEdRender.
void UpdateOTDULights();

// Halo sprites on the nearest lights of the level (every sector once the
// background scan is done, else the loaded ones) and light shafts below the
// nearest ones (faded by the lamp intensity), drawn in the near pass.
void DrawOTDULightGlows();
void ReleaseOTDULightGlows();

// How many lamp glows are drawn and how far (set by SetDrawDistance).
enum eOTDUGlowQuality : int32_t {
    GQ_Low = 0,
    GQ_Default,
    GQ_High,
    GQ_Ultra,

    GQ_Count
};

void SetOTDUGlowQuality( eOTDUGlowQuality quality );
eOTDUGlowQuality GetOTDUGlowQuality();

// Car headlights (OTDU_HEADLIGHTS, FX/Lights_OpenTDU.ini): spot lights at
// each car's front headlight flare nodes, on while the night lamps are lit.
struct OTDUHeadlightConfig {
    Eigen::Vector3f Color = Eigen::Vector3f::Ones();
    float LowBeamRange = 0.0f;  // meters
    float HighBeamRange = 0.0f; // meters
    float OuterAngle = 0.0f;    // cone half angles, degrees
    float InnerAngle = 0.0f;
    float Pitch = 0.0f; // degrees below the car's forward axis
    float Intensity = 0.0f;
    float TailLightLevel = 0.35f; // player car brake lights at night
};

void SetOTDUHeadlightConfig( const OTDUHeadlightConfig &config );
// Headlights are configured and the night lamps are lit: cars keep their
// headlights on.
bool AreOTDUHeadlightsOn();
float GetOTDUTailLightLevel();
// The player holds the light key (retail: a flash): high beams.
extern bool gbOTDUPlayerHighBeams;

// Picks the headlights of the player car and the nearest displayed cars and
// publishes them to gEdRender.
void UpdateOTDUHeadlights();

// Lit windows (OTDU_WINDOWS / OTDU_WINDOWPROFILE, FX/Lights_OpenTDU.ini): the
// building gloss maps mark the windows; a fraction of them glow at night, in
// a warm or cool color. Profiles pick the look per model name prefix.
struct OTDUWindowProfile {
    std::string Prefix; // empty = default profile
    Eigen::Vector3f WarmColor = Eigen::Vector3f( 1.0f, 0.75f, 0.45f );
    float LitFraction = 0.35f;
    Eigen::Vector3f CoolColor = Eigen::Vector3f( 0.75f, 0.85f, 1.0f );
    float CoolRatio = 0.25f;
    float Intensity = 0.0f;
};

// A profile with zero intensity disables the windows of its models (e.g.
// parked cars, whose paint gloss is not a window mask).
void RegisterOTDUWindowProfile( const OTDUWindowProfile &profile );
// Materials whose gloss map is one of these textures get no windows (spots use
// the gloss slot for their baked light maps).
void ExcludeOTDUWindowTexture( uint64_t textureHashcode );
void SetOTDUWindowSpotRadius( float radius );
// Uploads the window constants of the instance about to be drawn (PS c125 to
// c127, zero intensity by day or without profiles).
void BindOTDUWindowConstants( const Instance *pInstance );

// Unlit materials with an emissive color (signs, neons) or one of these base
// textures keep the night emissive scale instead of the unlit scale.
void RegisterOTDUEmissiveTexture( uint64_t textureHashcode );
// Uploads the emissive scale of the material about to be drawn (VS c250),
// reset after the draw.
void BindOTDUEmissiveConstants();
void UnbindOTDUEmissiveConstants();
