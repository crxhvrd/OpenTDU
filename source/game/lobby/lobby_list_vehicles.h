#pragma once

#include "flash/entities/flash_list.h"
#include "flash/entities/flash_panel.h"

class LobbyListVehicles;
class MngLobby;

// Refreshes the list whenever the player garage changes.
class LobbyListVehiclesNotifier : public FlashPanel {
public:
    LobbyListVehiclesNotifier( LobbyListVehicles *pOwner );

    void tick() override;

private:
    LobbyListVehicles *pList;
};

// Lobby vehicle selection ("case" widget): the garage cars allowed by the
// challenge restrictions.
class LobbyListVehicles : public FlashList {
public:
    static constexpr int32_t kNumSlots = 0x281;
    static constexpr int32_t kRentalSlot = 0x280;

public:
    inline void setOwner( MngLobby *pNewOwner ) { pOwner = pNewOwner; }
    inline int32_t getSelectedSlot() const {
        return slots[firstLine + selectedLineIndex];
    }

public:
    LobbyListVehicles();

    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void sort( int32_t param_1 ) override;
    void update( const int32_t param_2, const int32_t param_3 ) override;
    void selectedLineChanged() override;

    void onGarageUpdated();

private:
    void populate();
    void rebuild();
    bool compareSlots( const int32_t slotA, const int32_t slotB ) const;

private:
    LobbyListVehiclesNotifier notifier;
    MngLobby *pOwner;
    bool bEmpty;
    int32_t slots[kNumSlots];
    FlashPanelCarSpecs carSpecs;
    int32_t numCars;
};
