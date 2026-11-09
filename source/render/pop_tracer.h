#pragma once

// OTDU_ADDITION: -poptrace, a developer log of objects appearing, vanishing
// and switching LOD in the main view (pop_tracer.cpp).

class Instance;
struct InstanceLOD;

namespace PopTracer {
// Impostor sprite counters for the current frame (MngImpostor = 0,
// MngImpostorSimple = 1): requested, dropped at add (material / batch caps),
// dropped at draw (per frame sprite budget)
struct ImpostorCounters {
    uint32_t Added[2] = {};
    uint32_t DroppedAtAdd[2] = {};
    uint32_t DroppedAtDraw[2] = {};
    uint32_t Batches[2] = {};
    // submitSectorImpostors: instances after a cell's early break that would
    // have passed their own distance test (held back by a smaller one)
    uint32_t HeldByBreak = 0;
    uint32_t SubmitCapHits = 0;
};
extern ImpostorCounters gImpostorFrame;

bool IsEnabled();
// Every main view draw of an instance (TransformMatrixCommand::execute and the
// instanced path)
void OnDraw( const Instance *pInstance, const Matrix4x4f_t &modelMatrix );
// Who queued an instance for drawing (RenderScene::enqueue*), by return
// address, reported for the pops
void TagCaller( const Instance *pInstance, void *pReturnAddress );

// An impostor sprite of the main view (MngImpostor::addInstance): the same
// object as its mesh, drawn far away
void OnSprite( const Instance *pInstance );
} // namespace PopTracer
