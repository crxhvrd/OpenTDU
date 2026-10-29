#ifndef TDU_AVATAR_GLSL
#define TDU_AVATAR_GLSL

// Avatar shaders (retail avatar table).

#ifdef D3D9_VERTEX_SHADER
#define gVSLightAmbient D3D9_C( 94 )
#define gVSLightDiffuse D3D9_C( 95 )
#define gVSLightSpecular D3D9_C( 96 )
#define gVSCarIntegratorFactor D3D9_C( 242 )
#define gVSAttenuationFactor D3D9_C( 243 )

// 0 below the car interior height range (gVSCarIntegratorFactor.xy, object
// space), 1 above.
float tdu_avatar_car_factor( float height ) {
    return min( max( ( height + -gVSCarIntegratorFactor.x ) *
                         ( 1.0 / ( -gVSCarIntegratorFactor.x +
                                   gVSCarIntegratorFactor.y ) ),
                     0.0 ),
                1.0 );
}

// The light color scaled by the attenuation inside the car.
vec3 tdu_avatar_car_light( vec3 light, float attenuation, float factor ) {
    return light * attenuation + factor * ( light * -attenuation + light );
}

// OTDU_ADDITION: night lamps. The rebuilt avatar shaders always get the world
// view matrix (RenderContext::registerShader); the view position w tints the
// lamps (the car interior attenuation). Zero while the lamps are off.
#define gAvatarWorldView( i ) D3D9_CR( 20, i )

void tdu_avatar_lamp_varyings( vec4 position,
                               vec3 normal,
                               float tint,
                               out vec4 viewPosition,
                               out vec4 viewNormal ) {
    if ( !tdu_lamps_enabled() ) {
        viewPosition = vec4( 0.0 );
        viewNormal = vec4( 0.0 );
        return;
    }
    viewPosition = vec4( d3d9_dot( position, gAvatarWorldView( 0 ) ),
                         d3d9_dot( position, gAvatarWorldView( 1 ) ),
                         d3d9_dot( position, gAvatarWorldView( 2 ) ), tint );
    viewNormal = vec4( d3d9_dot( normal, gAvatarWorldView( 0 ).xyz ),
                       d3d9_dot( normal, gAvatarWorldView( 1 ).xyz ),
                       d3d9_dot( normal, gAvatarWorldView( 2 ).xyz ), 0.0 );
}
#endif

#ifdef D3D9_PIXEL_SHADER
// Lit albedo; bright light washes the albedo out towards a third of the
// light.
vec3 tdu_avatar_shade( vec3 light, vec3 albedo ) {
    vec3 washed = light * -albedo + ( light * vec3( 0.33 ) + albedo );
    washed *= min( dot( vec3( 0.299, 0.587, 0.114 ), light ), 1.0 );
    return light * albedo + washed;
}
#endif

#endif
