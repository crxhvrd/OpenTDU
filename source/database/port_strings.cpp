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
    {2026000008ull, "NIGHT LIGHTS"                                          },
    {57385810ull,   "LOW"                                                   },
    {2026000009ull, "ULTRA"                                                 },

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
