#include "shared.h"
#include "file_collection_2d.h"

FileCollection2D::FileCollection2D() {}

void FileCollection2D::registerUVA( RenderUVA *renderUVA ) {
    // FUN_00505b20
    RenderUVAs.push_front( renderUVA );
}

void FileCollection2D::register2DB( Render2DB *render2DB ) {
    OTDU_LOG_DEBUG( "Adding 2DB %p to collection...\n",
                    (char *)render2DB->getBitmapHashcode() );
    Render2DBs.push_front( render2DB );
}

void FileCollection2D::register2DM( Render2DM *render2DM ) {
    Render2DMs.push_front( render2DM );

    bindBitmapReferences( render2DM );
    bindUVAReferences( render2DM );
}

void FileCollection2D::bindBitmapReferences( Render2DM *render2DM ) {
    for ( const Render2DB *render2DB : Render2DBs ) {
        render2DM->bindBitmapReference( render2DB );
    }
}

void FileCollection2D::bindBitmapReferences( Material *pMaterial ) {
    // FUN_005056b0
    for ( const Render2DB *render2DB : Render2DBs ) {
        BindMaterialBitmapReference( pMaterial, render2DB );
    }
}

void FileCollection2D::bindUVAReferences( Render2DM *render2DM ) {
    // FUN_005057a0
    for ( const RenderUVA *renderUVA : RenderUVAs ) {
        render2DM->bindUVAnimationReference( renderUVA );
    }
}

void FileCollection2D::unregister2DM( Render2DM *render2DM ) {
    Render2DMs.remove( render2DM );
}

void FileCollection2D::unregister2DB( Render2DB *render2DB ) {
    Render2DBs.remove( render2DB );
}

void FileCollection2D::unregisterUVA( RenderUVA *renderUVA ) {
    // FUN_00505b80
    RenderUVAs.remove( renderUVA );
}
