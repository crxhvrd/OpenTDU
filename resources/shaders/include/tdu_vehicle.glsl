// Vehicle shading helpers shared by the rebuilt vehicle shaders. Include
// after tdu_common.glsl, in pixel shaders.

#ifndef TDU_VEHICLE_GLSL
#define TDU_VEHICLE_GLSL

#ifdef D3D9_PIXEL_SHADER

// Car lights (gRampCarLights = per light intensity): the mask map channels
// select the lights (alpha inverted), the light map gives their color. The
// emissive is added; the mask darkens the lit color under a light.
struct TDUCarLights {
    vec3 emissive;
    float mask;
};

TDUCarLights tdu_car_lights( vec4 maskMap, vec4 lightMap, vec4 intensity ) {
    vec4 selection =
        maskMap * vec4( 1.0, 1.0, 1.0, -1.0 ) + vec4( 0.0, 0.0, 0.0, 1.0 );
    vec4 lit = selection * intensity;
    vec4 unlit = intensity * -selection + vec4( 1.0 );

    TDUCarLights lights;
    vec3 emissive = lightMap.xyz * lit.yyy;
    emissive = lit.xxx * lightMap.xyz + emissive;
    emissive = lit.zzz * lightMap.xyz + emissive;
    lights.emissive = lit.www * lightMap.xyz + emissive;
    lights.mask = unlit.w * ( unlit.z * ( unlit.y * unlit.x ) );
    return lights;
}

#endif // D3D9_PIXEL_SHADER

#endif // TDU_VEHICLE_GLSL
