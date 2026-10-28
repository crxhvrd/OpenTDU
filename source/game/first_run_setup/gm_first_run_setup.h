#pragma once

#include "game/gm_base.h"

#include "render/draw_list.h"

// Game launch warmup (PSO compilation, asset extraction, etc.)
class GMFirstRunSetup : public GameMode {
public:
    static bool IsRequired();

public:
    GMFirstRunSetup();
    ~GMFirstRunSetup();

    void tick( float deltaTime, float totalTime ) override;

private:
    void drawProgress( const float progress );

private:
    DrawList drawList;
    bool bGeneratedMoviesBuilt;
    size_t numPSOsToBuild;
    float elapsedTime;
};
