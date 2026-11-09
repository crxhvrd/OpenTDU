#pragma once

// OTDU_ADDITION: per-pixel motion blur (camera + vehicles) with wheel spin blur,
// replacing the original radial speed blur. See postfx_motion_blur.cpp.
namespace MotionBlur {
// Creates the two frame graph nodes (world pass right after the radial blur's
// slot, player car pass after RP_CarCompo). Called by GSPostFX::initialize.
void Initialize();
void Destroy();

// True while the motion blur draws this frame: the radial blur steps then
// skip their draw (their nodes stay enabled, the player's car render pass
// selection depends on that).
bool IsActive();
} // namespace MotionBlur
