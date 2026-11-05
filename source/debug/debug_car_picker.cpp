#include "shared.h"
#include "debug_car_picker.h"

#ifdef OTDU_USE_IMGUI
#include "imgui.h"

#include <string>

#include "database/gs_database.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/vehicle_base.h"

namespace TestDrive {
    // Deluxe colors of a desc that never had any
    static const ColorRGBA kDefaultDeluxeColors[] = {
        ColorRGBA( 204, 25, 25, 255 ),
        ColorRGBA( 25, 25, 204, 255 ),
        ColorRGBA( 229, 229, 229, 255 ),
        ColorRGBA( 25, 153, 51, 255 ),
    };

    // TODO: Move to shared header
    static bool EditColor( const char *pLabel, ColorRGBA &color ) {
        float rgba[4];
        for ( int32_t i = 0; i < 4; i++ ) {
            rgba[i] = float( color[i] ) / 255.0f;
        }
        if ( !ImGui::ColorEdit4( pLabel, rgba ) ) {
            return false;
        }
        for ( int32_t i = 0; i < 4; i++ ) {
            color[i] = uint8_t( rgba[i] * 255.0f + 0.5f );
        }
        return true;
    }

    // TODO: Move to shared header
    template <typename Getter>
    static bool DrawNameList( const char *pID,
                              const int32_t count,
                              int32_t &selected,
                              Getter getName ) {
        bool bChanged = false;
        if ( ImGui::BeginListBox( pID ) ) {
            for ( int32_t i = 0; i < count; i++ ) {
                const char *pName = getName( i );
                if ( pName == nullptr ) {
                    continue;
                }
                const bool bSelected = ( selected == i );
                if ( ImGui::Selectable( pName, bSelected ) ) {
                    selected = i;
                    bChanged = true;
                }
                if ( bSelected ) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndListBox();
        }
        return bChanged;
    }

    // TODO: Move to shared header
    static const char *OrUnnamed( const char *pName ) {
        return ( pName != nullptr ) ? pName : "<unnamed>";
    }

    bool DrawCarList( int32_t &carID ) {
        bool bChanged = false;
        ImGui::SeparatorText( "Vehicle" );
        if ( gpDatabase == nullptr ||
             !ImGui::BeginListBox( "##DbgVehicleList" ) ) {
            return bChanged;
        }
        for ( const CarConfig &car : gpDatabase->getCarList() ) {
            const bool bSelected = ( car.Hash == carID );
            const std::string label =
                std::to_string( car.Hash ) + " - " + car.FullName;
            if ( ImGui::Selectable( label.c_str(), bSelected ) ) {
                bChanged = ( carID != car.Hash );
                carID = car.Hash;
            }
            if ( bSelected ) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndListBox();
        return bChanged;
    }

    uint32_t DrawCarPicker( CarDesc &desc, VehicleBase *pVehicle ) {
        uint32_t result = CPR_None;
        if ( gpDatabase == nullptr || gpDatabase->getCarList().empty() ) {
            return result;
        }

        ImGui::PushID( &desc );

        int32_t carID = desc.isValid() ? int32_t( desc.getCarConfigID() ) : -1;
        if ( DrawCarList( carID ) ) {
            // Database paint 0, default rims and interior
            desc.setArchetype( uint32_t( carID ) );
            desc.paintIndex = 0;
            desc.interiorID = 0;
            result |= CPR_CarChanged;
        }

        if ( !desc.isValid() ) {
            ImGui::PopID();
            return result;
        }

        const int32_t configID = int32_t( desc.getCarConfigID() );
        const int32_t numColors = gpDatabase->getNumColors( configID );
        if ( desc.paintIndex >= numColors ) {
            desc.paintIndex = 0;
        }

        // Deluxe paint: no database paint, ePaintType::PT_Custom*
        const bool bDeluxe = ( desc.paintIndex < 0 );
        ImGui::SeparatorText( "Paint" );
        if ( ImGui::BeginListBox( "##DbgPaintList" ) ) {
            if ( ImGui::Selectable( "Deluxe Paint", bDeluxe ) ) {
                if ( desc.deluxeMainColor.Color == 0u &&
                     desc.deluxeSecondColor.Color == 0u ) {
                    desc.setPaintColors(
                        ePaintType::PT_CustomSolid, kDefaultDeluxeColors[0],
                        kDefaultDeluxeColors[1], &kDefaultDeluxeColors[2],
                        &kDefaultDeluxeColors[3] );
                }
                desc.paintIndex = -1;
                desc.paintType = ePaintType::PT_CustomSolid;
                result |= CPR_PaintChanged;
            }
            if ( bDeluxe ) {
                ImGui::SetItemDefaultFocus();
            }

            for ( int32_t i = 0; i < numColors; i++ ) {
                DBEntryCarColor *pColor =
                    gpDatabase->getColorByIndex( configID, i );
                const bool bSelected = ( desc.paintIndex == i );
                if ( ImGui::Selectable(
                         OrUnnamed( pColor->ColorName.pAsString ),
                         bSelected ) ) {
                    desc.paintType = ePaintType::PT_Default;
                    desc.paintIndex = i;
                    result |= CPR_PaintChanged;
                }
                if ( bSelected ) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndListBox();
        }

        if ( desc.paintIndex < 0 ) {
            static const char *kPaintTypeNames[] = { "None", "Default",
                                                     "Metal" };

            ImGui::SeparatorText( "Deluxe paint" );
            int32_t paintType = int32_t( desc.paintType );
            if ( ImGui::Combo( "##DbgDeluxePaintType", &paintType,
                               kPaintTypeNames,
                               IM_ARRAYSIZE( kPaintTypeNames ) ) ) {
                desc.paintType = ePaintType( paintType );
                result |= CPR_PaintChanged;
            }

            if ( desc.paintType != ePaintType::PT_Default ) {
                bool bColorChanged = false;
                bColorChanged |= EditColor( "Primary", desc.deluxeMainColor );
                bColorChanged |=
                    EditColor( "Highlights", desc.deluxeSecondColor );
                if ( pVehicle != nullptr &&
                     pVehicle->shouldUseDeluxeTwoTones() ) {
                    bColorChanged |=
                        EditColor( "Secondary", desc.deluxeLogoColor );
                    bColorChanged |= EditColor( "Secondary Hightlights",
                                                desc.deluxeSecondColor2 );
                }
                if ( bColorChanged ) {
                    result |= CPR_PaintChanged;
                }
            }
        }

        DBEntryCarColor *pSelectedColor =
            ( desc.paintIndex >= 0 && numColors > 0 )
                ? gpDatabase->getColorByIndex( configID, desc.paintIndex )
                : nullptr;

        int32_t numInteriors = 0;
        while ( gpDatabase->getInteriorByIndex( pSelectedColor,
                                                numInteriors ) != nullptr ) {
            numInteriors++;
        }
        if ( desc.interiorID >= numInteriors ) {
            desc.interiorID = 0;
        }

        ImGui::SeparatorText( "Interior" );
        if ( numInteriors == 0 ) {
            ImGui::TextDisabled( "No interior for this paint." );
        } else if ( DrawNameList( "##DbgInteriorList", numInteriors,
                                  desc.interiorID, [&]( const int32_t i ) {
                                      return OrUnnamed(
                                          gpDatabase
                                              ->getInteriorByIndex(
                                                  pSelectedColor, i )
                                              ->InteriorName.pAsString );
                                  } ) ) {
            result |= CPR_PaintChanged;
        }

        // Index 0: database default (rimID -1)
        ImGui::SeparatorText( "Rims" );
        int32_t rimEntry = desc.rimID + 1;
        if ( DrawNameList(
                 "##DbgRimList", gpDatabase->getNumRims( configID ) + 1,
                 rimEntry, [&]( const int32_t i ) -> const char * {
                     if ( i == 0 ) {
                         return "Default";
                     }
                     DBEntryRim *pRim = gpDatabase->getRim( configID, i - 1 );
                     return ( pRim != nullptr )
                                ? OrUnnamed( pRim->FriendlyName.pAsString )
                                : nullptr;
                 } ) ) {
            desc.rimID = rimEntry - 1;
            result |= CPR_PaintChanged;
        }

        ImGui::PopID();
        return result;
    }
} // namespace TestDrive
#endif
