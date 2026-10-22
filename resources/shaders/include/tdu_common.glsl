// Engine-wide constant registers and shading helpers shared by the rebuilt
// shaders. Include after d3d9_abi.glsl.

#ifndef TDU_COMMON_GLSL
#define TDU_COMMON_GLSL

#ifdef D3D9_VERTEX_SHADER

#define gFogParametersVS c.f[107]
#define gFogColorVS c.f[123]
// OTDU_ADDITION: night parameters (RenderContext, kNightParamsRegisterVS):
// x = unlit/emissive scale, y = baked shadow strength (retail 1), z = star
// brightness, w = moon brightness (retail 0).
#define tdu_night_params c.f[255]
// OTDU_ADDITION: camera to world rotation columns (kCamToWorldRegisterVS).
#define tdu_cam_to_world( i ) c.f[252 + ( i )]
// OTDU_ADDITION: night lamps (kLampParamsRegisterVS): x = lamp count.
#define tdu_lamp_params c.f[251]
bool tdu_lamps_enabled() { return tdu_lamp_params.x > 0.0; }
// OTDU_ADDITION: per draw emissive scale (kEmissiveRegisterVS, 0 = none).
#define tdu_emissive_params c.f[250]
// Unlit materials: the night unlit scale, or the emissive scale for signs.
float tdu_unlit_scale() {
    return max( tdu_night_params.x, tdu_emissive_params.x );
}

// UV set transform by the first two rows of a 4x4 texture matrix.
#define TDU_APPLY_UV_MATRIX( uv, M )                                           \
    vec2( d3d9_dot( vec3( uv, 1.0 ), M( 0 ).xyw ),                             \
          d3d9_dot( vec3( uv, 1.0 ), M( 1 ).xyw ) )

// Linear fog factor (1 = no fog), floored by gFogColorVS.w. Fog is disabled
// when gFogParametersVS.x is 0.
float tdu_vertex_fog( float viewDistance ) {
    float fog =
        ( gFogParametersVS.z - viewDistance ) * ( 1.0 / gFogParametersVS.w );
    fog = max( min( max( fog, 0.0 ), 1.0 ), gFogColorVS.w );
    float fogParameterSquared = gFogParametersVS.x * gFogParametersVS.x;
    float bEnabled = float( -fogParameterSquared < fogParameterSquared );
    return bEnabled * ( fog - 1.0 ) + 1.0;
}

// 4 bone skinning (3 rows per bone, the last weight is 1 - the others), in
// retail's operation order: shadow receivers redraw skinned meshes and must
// land on the exact same positions.
#define gVSWorldMatrixArray( i ) c.f[129 + ( i )]

ivec4 tdu_skin_bones( vec4 blendIndices ) {
    return ivec4( round( blendIndices * vec4( 3.0 ) ) );
}

vec4 tdu_skin_weights( vec4 blendWeights ) {
    return vec4( blendWeights.xyz,
                 -( ( blendWeights.y + blendWeights.x ) + blendWeights.z ) +
                     1.0 );
}

vec3 tdu_skin_bone_position( vec4 position, int bone ) {
    return vec3( d3d9_dot( position, gVSWorldMatrixArray( bone ) ),
                 d3d9_dot( position, gVSWorldMatrixArray( bone + 1 ) ),
                 d3d9_dot( position, gVSWorldMatrixArray( bone + 2 ) ) );
}

vec3 tdu_skin_bone_normal( vec3 normal, int bone ) {
    return vec3( d3d9_dot( normal, gVSWorldMatrixArray( bone ).xyz ),
                 d3d9_dot( normal, gVSWorldMatrixArray( bone + 1 ).xyz ),
                 d3d9_dot( normal, gVSWorldMatrixArray( bone + 2 ).xyz ) );
}

vec4 tdu_skin_position( vec4 position, ivec4 bones, vec4 weights ) {
    vec3 skinned = tdu_skin_bone_position( position, bones.y ) * weights.y;
    skinned = tdu_skin_bone_position( position, bones.x ) * weights.x + skinned;
    skinned = tdu_skin_bone_position( position, bones.z ) * weights.z + skinned;
    skinned = tdu_skin_bone_position( position, bones.w ) * weights.w + skinned;
    return vec4( skinned, 1.0 );
}

vec3 tdu_skin_normal( vec3 normal, ivec4 bones, vec4 weights ) {
    vec3 skinned = tdu_skin_bone_normal( normal, bones.y ) * weights.y;
    skinned = tdu_skin_bone_normal( normal, bones.x ) * weights.x + skinned;
    skinned = tdu_skin_bone_normal( normal, bones.z ) * weights.z + skinned;
    return tdu_skin_bone_normal( normal, bones.w ) * weights.w + skinned;
}

#endif // D3D9_VERTEX_SHADER

#ifdef D3D9_PIXEL_SHADER

#define gFogParametersPS c.f[12]
#define gFogColorPS c.f[13]
// OTDU_ADDITION: see the VS definition (kNightParamsRegisterPS).
#define tdu_night_params c.f[223]
// OTDU_ADDITION: night lamps (kLampRegisterPS): c31 = (count, intensity),
// c32 = view space up, c33 + 2i = (view position, radius), c34 + 2i = (color,
// fade weight).
#define TDU_MAX_NUM_LAMPS 32
#define tdu_lamp_params c.f[31]
#define tdu_view_up c.f[32]
#define tdu_lamp_position( i ) c.f[33 + 2 * ( i )]
#define tdu_lamp_color( i ) c.f[34 + 2 * ( i )]
// OTDU_ADDITION: car headlights (kHeadlightRegisterPS): c100 = (count,
// intensity), c101 + 3i = (view position, range), c102 + 3i = (view
// direction, cos outer), c103 + 3i = (color, cos inner).
#define TDU_MAX_NUM_HEADLIGHTS 8
#define tdu_headlight_params c.f[100]
#define tdu_headlight_position( i ) c.f[101 + 3 * ( i )]
#define tdu_headlight_direction( i ) c.f[102 + 3 * ( i )]
#define tdu_headlight_color( i ) c.f[103 + 3 * ( i )]

bool tdu_lamps_enabled() {
    return tdu_lamp_params.x > 0.0 || tdu_headlight_params.x > 0.0;
}

// OTDU_ADDITION: lit windows (kWindowRegisterPS): c125 = (warm color, lit
// fraction), c126 = (cool color, cool ratio), c127 = (intensity, seed).
#define tdu_window_warm c.f[125]
#define tdu_window_cool c.f[126]
#define tdu_window_params c.f[127]

vec3 tdu_hash3( vec3 p ) {
    uvec3 v = uvec3( ivec3( p ) ) * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return vec3( v ) * ( 1.0 / 4294967295.0 );
}

// Street lamps point down: smooth radius falloff, Lambert (two sided for
// foliage cards), and a soft cone below the lamp. Car headlights: same
// falloff and Lambert, cone around their direction.
vec3 tdu_lamp_light_sided( vec3 viewPosition,
                           vec3 viewNormal,
                           bool bTwoSided ) {
    vec3 light = vec3( 0.0 );
    int numLamps = int( tdu_lamp_params.x );
    for ( int i = 0; i < TDU_MAX_NUM_LAMPS; i++ ) {
        if ( i >= numLamps ) {
            break;
        }
        vec4 lamp = tdu_lamp_position( i );
        vec3 toLamp = lamp.xyz - viewPosition;
        float distanceSquared = dot( toLamp, toLamp );
        float radiusSquared = lamp.w * lamp.w;
        if ( distanceSquared >= radiusSquared ) {
            continue;
        }
        vec3 direction = toLamp * inversesqrt( max( distanceSquared, 1e-4 ) );
        float falloff = 1.0 - distanceSquared / radiusSquared;
        falloff *= falloff;
        float facing = dot( viewNormal, direction );
        float lambert = bTwoSided ? abs( facing ) : max( facing, 0.0 );
        float cone = smoothstep( 0.15, 0.6, dot( direction, tdu_view_up.xyz ) );
        vec4 color = tdu_lamp_color( i );
        light += color.rgb * ( color.w * falloff * lambert * cone );
    }
    light *= tdu_lamp_params.y;

    vec3 headlights = vec3( 0.0 );
    int numHeadlights = int( tdu_headlight_params.x );
    for ( int i = 0; i < TDU_MAX_NUM_HEADLIGHTS; i++ ) {
        if ( i >= numHeadlights ) {
            break;
        }
        vec4 spot = tdu_headlight_position( i );
        vec3 toLight = spot.xyz - viewPosition;
        float distanceSquared = dot( toLight, toLight );
        float rangeSquared = spot.w * spot.w;
        if ( distanceSquared >= rangeSquared ) {
            continue;
        }
        vec3 direction = toLight * inversesqrt( max( distanceSquared, 1e-4 ) );
        float falloff = 1.0 - distanceSquared / rangeSquared;
        falloff *= falloff;
        float facing = dot( viewNormal, direction );
        float lambert = bTwoSided ? abs( facing ) : max( facing, 0.0 );
        vec4 axis = tdu_headlight_direction( i );
        vec4 color = tdu_headlight_color( i );
        float cone = smoothstep( axis.w, color.w, dot( -direction, axis.xyz ) );
        headlights += color.rgb * ( falloff * lambert * cone );
    }
    return light + headlights * tdu_headlight_params.y;
}

vec3 tdu_lamp_light( vec3 viewPosition, vec3 viewNormal ) {
    return tdu_lamp_light_sided( viewPosition, viewNormal, false );
}

// Lamp light of a vertex lit surface, from the OTDU varyings: view position
// (w = surface tint) and view normal (zero while the lamps are off).
vec3 tdu_lamp_pixel_sided( vec4 viewPosition,
                           vec4 viewNormal,
                           bool bTwoSided ) {
    if ( !tdu_lamps_enabled() ) {
        return vec3( 0.0 );
    }
    return tdu_lamp_light_sided( viewPosition.xyz, normalize( viewNormal.xyz ),
                                 bTwoSided ) *
           viewPosition.w;
}

vec3 tdu_lamp_pixel( vec4 viewPosition, vec4 viewNormal ) {
    return tdu_lamp_pixel_sided( viewPosition, viewNormal, false );
}

// Normal mapped variant: the varyings at OTDU_SLOT_VIEW_BASIS hold the view
// space images of the normal map axes.
vec3 tdu_lamp_pixel_mapped(
    vec4 viewPosition, vec4 basisX, vec4 basisY, vec4 basisZ, vec3 mapNormal ) {
    vec3 viewNormal = mapNormal.x * basisX.xyz + mapNormal.y * basisY.xyz +
                      mapNormal.z * basisZ.xyz;
    return tdu_lamp_pixel( viewPosition, vec4( viewNormal, 0.0 ) );
}

// OTDU_ADDITION: world direction toward the moon (kMoonDirectionRegisterPS).
#define tdu_moon_direction c.f[222]

// OTDU_ADDITION: fades a baked sun shadow factor (1 = lit) toward 1 as the
// baked shadow strength drops; exact at strength 1.
float tdu_baked_shadow( float shadow ) {
    return shadow + ( 1.0 - shadow ) * ( 1.0 - tdu_night_params.y );
}
vec3 tdu_baked_shadow( vec3 shadow ) {
    return shadow + ( vec3( 1.0 ) - shadow ) * ( 1.0 - tdu_night_params.y );
}

// Per pixel variant of tdu_vertex_fog.
float tdu_pixel_fog( float viewDistance ) {
    float fog = d3d9_saturate( ( gFogParametersPS.z - viewDistance ) *
                               ( 1.0 / gFogParametersPS.w ) );
    fog = max( fog, gFogColorPS.w );
    return ( -( gFogParametersPS.x * gFogParametersPS.x ) >= 0.0 ) ? 1.0 : fog;
}

vec3 tdu_apply_fog( vec3 color, float fog ) {
    return d3d9_lerp( gFogColorPS.rgb, color, fog );
}

#endif // D3D9_PIXEL_SHADER

#endif // TDU_COMMON_GLSL
