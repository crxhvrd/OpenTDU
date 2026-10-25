#version 450
#extension GL_GOOGLE_include_directive : require

// Port made screen space overlays (no retail counterpart): debug text, imgui
// and the debug font. Paired with common/unlit.frag (UNLIT_TEXTURED
// UNLIT_VERTEX_COLOR).
//
// Permutations:
//   DEBUG_OVERLAY_TEXT      Debug Text (Common): position in [0, 1] screen
//                           space, color from c0
//   DEBUG_OVERLAY_TEXCOORD  dbg_font: UV set 0 in its own stream (imgui packs
//                           the UVs in Position0.zw)
//
// Without DEBUG_OVERLAY_TEXT the position is scaled and offset by
// gScaleTransition and the color is a BGRA Color0.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gDebugTextColor c.f[0]
#define gScaleTransition c.f[225]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef DEBUG_OVERLAY_TEXT
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#else
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#ifdef DEBUG_OVERLAY_TEXCOORD
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;

void main() {
#ifdef DEBUG_OVERLAY_TEXT
    gl_Position = vec4( in_Position0.xy * 2.0 + -1.0, 0.0, 1.0 );
    out_Color0 = gDebugTextColor;
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#else
    gl_Position = vec4( in_Position0.xy * gScaleTransition.xy +
                            gScaleTransition.zw,
                        0.0, 1.0 );
    out_Color0 = in_Color0.zyxw;
#ifdef DEBUG_OVERLAY_TEXCOORD
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#else
    out_Texcoord0 = vec4( in_Position0.zw, 0.0, 0.0 );
#endif
#endif
    out_Color0 = clamp( out_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
