#pragma once

// OTDU_ADDITION: wheel spin blur drawn by the wheels' own geometry
// (wheel_blur.cpp).

struct TransformMatrixCommand;
struct Primitive;
struct InstanceLOD;
class LightSetupNode;

namespace WheelBlur {
// TransformMatrixCommand::execute: true when this draw is a spinning wheel's
// part in a main view scene; it is held for FlushScene instead of drawn.
bool Defer( const TransformMatrixCommand &command,
            Primitive *pPrimitive,
            InstanceLOD *pLOD );

// SceneSetupCommand::execute: the lights the next draws are made with
void OnSceneSetup( LightSetupNode *pLights );

// RenderContextBase::renderScene, after the buckets: draws the held wheels
void FlushScene();

// GSPostFX::releaseEffects
void Destroy();
} // namespace WheelBlur
