#pragma once

// Havok resolves a convex shape against a hkpHeightFieldShape through
// hkpHeightFieldAgent (processCollision @0x004a0f60): the convex only
// contributes its collision spheres (8 corners for a hkpBoxShape) and the
// heightfield answers per sphere (collideSpheres). A box therefore only
// touches the terrain at its corners.
namespace HavokHeightFieldAgent {
    // Must run after JPH::RegisterTypes()
    void Register();
} // namespace HavokHeightFieldAgent
