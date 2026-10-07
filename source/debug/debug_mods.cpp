#include "shared.h"
#include "debug_mods.h"

#ifdef OTDU_USE_IMGUI
#include "imgui.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "filesystem/mod_installer.h"
#include "filesystem/mod_manager.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"

namespace TestDrive {
    static void ApplyModChanges() {
        gpModManager->save();

        if ( gpDatabase != nullptr && gpPlayerData != nullptr ) {
            gpDatabase->releaseDatabases( /* bRetainBuffers */ true );
            gpDatabase->reloadDatabases( gpPlayerData->getLanguage(), true );
            gpDatabase->republishFrontEndStrings();
        } else if ( gpFile != nullptr ) {
            gpFile->initCarPacks( false );
        }
    }

    void DrawModManagerPanel() {
        if ( gpModManager == nullptr ) {
            ImGui::TextDisabled( "Mod manager unavailable." );
            return;
        }

        std::vector<ModPack> &packs = gpModManager->getPacks();

        ImGui::Spacing();

        if ( packs.empty() ) {
            ImGui::TextDisabled( "No mods found under Mods/." );
            if ( ImGui::Button( "Rescan" ) ) {
                gpModManager->refresh();
            }
            return;
        }

        if ( ImGui::BeginTable( "##modlist", 4,
                                ImGuiTableFlags_Borders |
                                    ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_SizingStretchProp ) ) {
            ImGui::TableSetupColumn( "On", ImGuiTableColumnFlags_WidthFixed,
                                     32.0f );
            ImGui::TableSetupColumn( "Pack" );
            ImGui::TableSetupColumn( "Files", ImGuiTableColumnFlags_WidthFixed,
                                     56.0f );
            ImGui::TableSetupColumn( "State" );
            ImGui::TableHeadersRow();

            for ( ModPack &pack : packs ) {
                ImGui::TableNextRow();
                ImGui::PushID( pack.Id.c_str() );

                ImGui::TableNextColumn();
                bool bEnabled = pack.bEnabled;
                ImGui::BeginDisabled( pack.bMandatory );
                if ( ImGui::Checkbox( "##enabled", &bEnabled ) ) {
                    gpModManager->setEnabled( pack.Id.c_str(), bEnabled );
                }
                ImGui::EndDisabled();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted( pack.Id.c_str() );

                ImGui::TableNextColumn();
                if ( pack.NumEntries != 0u ) {
                    ImGui::Text( "%u", pack.NumEntries );
                } else {
                    ImGui::TextDisabled( "-" );
                }

                ImGui::TableNextColumn();
                if ( pack.bConflicting ) {
                    ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ),
                                        "conflict" );
                    if ( !pack.Status.empty() &&
                         ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNone ) ) {
                        ImGui::SetTooltip( "%s", pack.Status.c_str() );
                    }
                } else if ( pack.bDescriptorOnly && pack.bEnabled ) {
                    ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ),
                                        pack.bMandatory ? "built-in"
                                                        : "descriptor only" );
                } else if ( pack.bMounted ) {
                    ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ),
                                        "mounted on map %d",
                                        pack.FilemapIndex );
                } else {
                    ImGui::TextDisabled( "%s", pack.Status.empty()
                                                   ? "off"
                                                   : pack.Status.c_str() );
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        const bool bPending = gpModManager->hasPendingChanges();
        ImGui::BeginDisabled( !bPending );
        if ( ImGui::Button( "Apply and reload" ) ) {
            ApplyModChanges();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if ( ImGui::Button( "Rescan" ) ) {
            gpModManager->refresh();
        }

        if ( bPending ) {
            ImGui::SameLine();
            ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.3f, 1.0f ),
                                "changes not applied yet" );
        }

        // A save keeps a car by its database id. Turn the pack that supplied
        // one off and the id stops resolving -- every getCarDBEntry() caller in
        // the tree already tolerates the null, so nothing crashes, but the car
        // quietly stops working with no explanation. Say so out loud instead.
        if ( ImGui::CollapsingHeader( "Saved cars vs the loaded database" ) ) {
            IPlayerData *pSave = ( gpPlayerData != nullptr )
                                     ? gpPlayerData->getLoadedSave()
                                     : nullptr;
            Garage *pGarage =
                ( pSave != nullptr ) ? pSave->getGarage() : nullptr;

            if ( pGarage == nullptr || gpDatabase == nullptr ) {
                ImGui::TextDisabled( "No save is loaded." );
            } else {
                uint32_t numMissing = 0u;
                for ( int32_t slot = 0; slot < Garage::kNumSavedCarSlots;
                      slot++ ) {
                    const uint32_t carID = pGarage->pCars[slot].getCarID();
                    if ( carID == 0u ) {
                        continue;
                    }

                    if ( gpDatabase->getCarDBEntry( (int32_t)carID ) !=
                         nullptr ) {
                        continue;
                    }

                    numMissing++;
                    ImGui::BulletText(
                        "slot %d holds car id %u, which no loaded database "
                        "provides",
                        slot, carID );
                }

                if ( numMissing == 0u ) {
                    ImGui::TextDisabled(
                        "Every saved car resolves against the loaded "
                        "database." );
                } else {
                    ImGui::TextColored(
                        ImVec4( 1.0f, 0.8f, 0.3f, 1.0f ),
                        "%u saved car(s) are missing. Re-enabling the pack "
                        "that added them brings them back; the save itself is "
                        "untouched.",
                        numMissing );
                }
            }
        }

        if ( gpModInstaller != nullptr &&
             ImGui::CollapsingHeader( "Installed content" ) ) {
            const std::vector<InstalledContent> &content =
                gpModInstaller->getInstalledContent();
            if ( content.empty() ) {
                ImGui::TextDisabled( "No pack declared any content." );
            }

            for ( const InstalledContent &entry : content ) {
                std::string actions;
                if ( ( entry.ActionMask & CA_AddVehicle ) != 0 ) {
                    actions += "adds a vehicle";
                }
                if ( ( entry.ActionMask & CA_UnlockVehicle ) != 0 ) {
                    actions += actions.empty() ? "" : ", ";
                    actions += "unlocks a vehicle";
                }
                if ( ( entry.ActionMask & CA_AddRadioStation ) != 0 ) {
                    actions += actions.empty() ? "" : ", ";
                    actions += "adds a radio station";
                }

                ImGui::BulletText( "%S - %s", entry.DisplayName.c_str(),
                                   actions.empty() ? "no declared action"
                                                   : actions.c_str() );
            }

            const std::vector<ModDBInjection> &injections =
                gpModInstaller->getDatabaseInjections();
            for ( const ModDBInjection &injection : injections ) {
                ImGui::BulletText(
                    "%S -> %s: %zu row(s), %zu string(s)",
                    injection.PackName.c_str(), injection.Table.c_str(),
                    injection.Rows.size(), injection.Strings.size() );
            }
        }
    }
} // namespace TestDrive
#endif
