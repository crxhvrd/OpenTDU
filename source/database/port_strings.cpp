#include "shared.h"
#include "port_strings.h"

#include "gs_database.h"

static constexpr uint32_t kMenusDB = 0;

struct PortStringDesc {
    uint64_t Hashcode;
    const char *pFallback;
};

// Make sure to start hashes from 2026000000 to avoid collisions!!!!
static constexpr PortStringDesc kPortStrings[PS_Count] = {
    {2026000001ull, "DISPLAY MODE"                                          },
    {2026000002ull, "FRAME RATE"                                            },
    {2026000003ull, "REFLECTIONS"                                           },
    {2026000004ull, "VSYNC"                                                 },
    {61025810ull,   "FULL SCREEN"                                           },
    {2026000005ull, "BORDERLESS"                                            },
    {2026000006ull, "WINDOWED"                                              },
    {2026000007ull, "UNLIMITED"                                             },
    {63275810ull,   "HIGH"                                                  },
    {63075810ull,   "DEFAULT"                                               },
    {2026000008ull, "DRAW DISTANCE"                                         },
    {57385810ull,   "LOW"                                                   },
    {2026000009ull, "ULTRA"                                                 },
    {2026000010ull, "HDR DISPLAY"                                           },

    {2026000100ull, "'./opentdu_data/mods' folder is empty!"                },
    {2026000101ull, "Enabled"                                               },
    {2026000102ull, "Disabled"                                              },
    {2026000103ull, "Built-in (always on)"                                  },
    {2026000104ull, "Files"                                                 },
    {2026000105ull, "Mounted on map"                                        },
    {2026000106ull, "row(s)"                                                },
    {2026000107ull, "string(s)"                                             },

    {2026000201ull, "§DEVICE§ detected. Use the §TEMPLATE§ controls preset?"},
    {2026000202ull, "Also use the §TEMPLATE§ controls preset for §DEVICE§?" },

    {2026000301ull, "OPENTDU"                                               },
    {2026000302ull, "Controls for OpenTDU actions"                          },
    {2026000303ull, "OpenTDU gameplay options"                              },
    {2026000304ull, "OpenTDU video options"                                 },
    {2026000305ull, "QUALITY"                                               },
    {2026000306ull, "HDR"                                                   },
    {2026000307ull, "PAPER WHITE"                                           },
    {2026000308ull, "PEAK BRIGHTNESS"                                       },
    {2026000309ull, "HIGHLIGHTS"                                            },
    {2026000310ull, "SOFT HIGHLIGHTS"                                       },
    {2026000311ull, "AUTO"                                                  },
    {2026000312ull, "MODS"                                                  },
    {2026000313ull, "Enable or disable mod packs"                           },
    {2026000314ull, "Status"                                                },
};

const char *GetPortString( const ePortString id ) {
    OTDU_ASSERT( id < PS_Count );
    const PortStringDesc &desc = kPortStrings[id];

    if ( gpDatabase != nullptr &&
         gpDatabase->getGameDB().Databases[kMenusDB].List != nullptr ) {
        const char *pString =
            gpDatabase->getStringByHashcode( kMenusDB, desc.Hashcode );
        if ( pString != nullptr && *pString != '\0' ) {
            return pString;
        }
    }

    return desc.pFallback;
}

uint64_t GetPortStringHashcode( const ePortString id ) {
    OTDU_ASSERT( id < PS_Count );
    return kPortStrings[id].Hashcode;
}
