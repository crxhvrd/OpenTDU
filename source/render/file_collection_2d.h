#pragma once

#include "2db.h"
#include "2dm.h"
#include "ed_render.h"
#include "uva.h"

struct FileCollection2D {
    std::list<Render2DM *> Render2DMs;
    std::list<Render2DB *> Render2DBs;
    std::list<RenderUVA *> RenderUVAs;

    FileCollection2D();

    void registerUVA( RenderUVA *renderUVA );
    void register2DB( Render2DB *render2DB );
    void register2DM( Render2DM *render2DM );
    void unregister2DM( Render2DM *render2DM );
    void unregister2DB( Render2DB *render2DB );
    void unregisterUVA( RenderUVA *renderUVA );
    void bindBitmapReferences( Render2DM *render2DM );
    void bindUVAReferences( Render2DM *render2DM );
};
