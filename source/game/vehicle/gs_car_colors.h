#pragma once

#include "filesystem/bank.h"
#include "game/gs_base.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

class GSCarColors : public GameSystem, public Bank {
public:
    const char *getName() const override { return "GServiceCarColors"; }
    inline Render2DM &get2DM() { return render2DM; }

public:
    GSCarColors();
    ~GSCarColors();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;

    Eigen::Vector4f getColorFloat( const uint64_t colorHashcode );
    ColorRGBA getColor( const uint64_t colorHashcode );

private:
    FileCollection2D collection2D;
    Render2DM render2DM;
    char *p2DMFileResource;
};

extern GSCarColors *gpCarColors;
