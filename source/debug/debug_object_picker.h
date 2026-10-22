#pragma once

#ifdef OTDU_USE_IMGUI
class Instance;
class RenderScene;

namespace TestDrive {
    // ImGui world object picker: records the objects submitted to the near
    // pass, picks the one under the screen center (bounding sphere, then LOD0
    // triangles) and exports its LOD0 mesh to a Wavefront .obj (object space,
    // as OTDU_LIGHTMODEL offsets). Drawn from GSWorld::drawDebug.
    void DrawObjectPickerPanel();

    // Called once per frame (GSWorld::draw): the picker keeps the objects
    // submitted during the latest frame only.
    void BeginObjectPickerFrame();

    // Called by RenderContext::submitObject for every submitted object.
    void RecordObjectPickerCandidate( RenderScene *pScene,
                                      Instance *pInstance );
} // namespace TestDrive
#endif
