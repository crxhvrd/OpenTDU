#pragma once

#include "flash/entities/flash_list.h"

class GMEstates;
class DrawList;
class FlashResource;
struct FlashRECT;

// Owned houses list ("/ss_menu/array_houses")
class FlashListHouses : public FlashList {
public:
    static constexpr int32_t kMaxNumHouses = 65;

public:
    inline void setOwner( GMEstates *pEstates ) { pOwner = pEstates; }
    inline int32_t getNumHouses() const { return numHouses; }

    static void HomeLureDrawCallback( FlashRECT *pViewport,
                                      float depth,
                                      DrawList *pDrawList,
                                      void *pUserData,
                                      FlashResource *pResource,
                                      const char *pClipName );

public:
    FlashListHouses();

    int32_t getListLength() const override {
        // FUN_007984a0
        return numHouses;
    }
    void open() override;
    void close() override;
    void sort( int32_t param_1 ) override;
    void update( const int32_t param_2, const int32_t param_3 ) override;
    void selectedLineChanged() override;

    void refresh();

private:
    void sortHouses();
    bool compareHouses( const int32_t houseA, const int32_t houseB ) const;

private:
    GMEstates *pOwner;
    int32_t numHouses;
    int32_t sortedHouses[kMaxNumHouses];
    uint64_t houseHashcodes[kMaxNumHouses];
    uint64_t displayedHouseHashcode;
};
