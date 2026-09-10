#include "shared.h"
#include "entity_boat_tour.h"

#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/bank_scene_arrays.h"
#include "filesystem/binary.h"

EntityBoatTour::EntityBoatTour()
    : Entity(),
      pPathRef( nullptr ),
      pBoatRef( nullptr ),
      pCameraRef( nullptr ),
      TourType( 0 ) {
    // FUN_004242c0
}

EntityBoatTour::~EntityBoatTour() {
    // FUN_00424320
    if ( Spline.pKeys != nullptr ) {
        delete[] Spline.pKeys;
        delete[] Spline.pPolygons;
        delete[] Spline.pLengths;
    }
}

void EntityBoatTour::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dbe10
    ResolvePathBlock( pPathRef, param_2 );
    ResolveEntityRef( pBoatRef, param_2 );
    ResolveEntityRef( pCameraRef, param_2 );
    bReferencesResolved = true;

    const Path *pPath = pPathRef != nullptr
                            ? *reinterpret_cast<const Path **>( pPathRef )
                            : nullptr;
    if ( pPath == nullptr ) {
        return;
    }

    const uint32_t numWaypoints = pPath->getNumWaypoints();
    if ( numWaypoints < 2 ) {
        return;
    }

    Spline.initialize( numWaypoints );
    Spline.NumKeys = numWaypoints;

    for ( uint32_t i = 0; i < numWaypoints; i++ ) {
        const float *pWaypoint = pPath->getWaypoint( i );

        TCBKeyPos &key = Spline.pKeys[i];
        key.Position = pWaypoint[0];
        key.Vec4 =
            Eigen::Vector4f( pWaypoint[1], pWaypoint[2], pWaypoint[3], kOne );
        key.T = pWaypoint[5];
        key.C = pWaypoint[6];
        key.B = pWaypoint[7];
    }

    Spline.compute();
}

void EntityBoatTour::unloadResources() {
    // FUN_007dbda0
    delete[] Spline.pKeys;
    delete[] Spline.pPolygons;
    delete[] Spline.pLengths;
    Spline = TCBSplinePosition{};
}

void EntityBoatTour::deserialize( BinaryReader *param_2 ) {
    // FUN_007dbdb0
    Entity::deserialize( param_2 );

    pPathRef = param_2->pCursor;
    param_2->pCursor += sizeof( uint64_t );

    pBoatRef = param_2->pCursor;
    param_2->pCursor += sizeof( uint64_t );

    pCameraRef = param_2->pCursor;
    param_2->pCursor += sizeof( uint64_t );

    TourType = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );
}
