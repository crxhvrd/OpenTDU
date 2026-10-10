#pragma once

#include <cstddef>

void *UpgradeRoadMapStream( const void *pDiskStream,
                            size_t diskSize,
                            size_t *pOutSize = nullptr );
void *UpgradeRoadDetailsStream( const void *pDiskStream,
                                size_t diskSize,
                                size_t *pOutSize = nullptr );
