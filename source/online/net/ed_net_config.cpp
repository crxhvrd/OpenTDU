#include "shared.h"
#include "ed_net_config.h"

#include "core/markup.h"

edNetConfig gNetConfig; // DAT_014d4f98

// Registration order and built-in defaults of FUN_0086dcc0
static constexpr struct {
    const char *pName;
    double DefaultValue;
} kNetConfigVariables[] = {
    {"QOS_BANDWIDTH_RETIME_MINIMAL", 1000   },
    {"QOS_BANDWIDTH_GAMEOBJECTS",    8000   },
    {"CORRECTION_ACTIVE",            3      },
    {"CORRECTION_DT",                0.2    },
    {"DRIVING_LOOPBACK_ACTIVE",      0      },
    {"DRIVING_LOOPBACK_STACKSIZE",   3      },
    {"RETIME_DT",                    50     },
    {"P2P_RESOURCE_SYNC_TIMEOUT",    30000  },
    {"P2P_TIMEOUT",                  5000   },
    {"P2P_INITIALTIMEOUT",           30000  },
    {"P2P_PROBETIMEOUT",             15000  },
    {"P2P_CHECK_THIS_PLAYER_DT",     200    },
    {"P2P_LAGGY_DT",                 100    },
    {"P2P_LAGGY_DT_MAX",             300    },
    {"P2P_LAGGY_DT_GHOST",           150    },
    {"P2P_ASYNC_SESSION_JOIN",       0      },
    {"ORB_REFRESH_DT",               180000 },
    {"ORB_LOST_RPC_DT",              640000 },
    {"USERMAP_REFRESH_DT",           10000  },
    {"EBAY_REFRESH_DT",              120000 },
    {"EBAY_KEYDELAY_DT",             3.0    },
    {"USERSTATS_REFRESH_DT",         120000 },
    {"EDNET_DEFAULT_TIMEOUT",        2500   },
    {"EDNET_DEFAULT_MAXRETRY",       3      },
    {"P2PFR_TIMEOUT_START_SCAN",     10000  },
    {"P2PFR_TIMEOUT_ELECT",          15000  },
    {"P2PFR_TIMEOUT_INITIAL_CORJ",   5000   },
    {"P2PFR_TIMEOUT_ALONE",          15000  },
    {"P2PFR_ACTIVE",                 1      },
    {"P2PFR_FORCE_DEFAULT_OFF",      0      },
    {"P2PFR_STRENGHT_RADIUS",        1500   },
    {"P2PFR_ALLOW_STRICT",           1      },
    {"P2PFR_INIT_IDLE_TIME",         5000   },
    {"GR_DELAY_NEWS",                600000 },
    {"GR_DELAY_NEWS_FAST",           60000  },
    {"GR_DELAY_CUSTOMCHA",           600000 },
    {"GR_DELAY_EBAY",                10000  },
    {"GR_DELAY_CARDEALER",           1800000},
    {"DENSITY_PLAYERS_DT",           8      },
    {"DENSITY_SPOTS_DT",             30000  },
    {"DENSITY_MAP_DT",               10.0   },
    {"PHOTO_UPLOAD_ALLOWED",         0      },
    {"PHOTO_UPLOAD_FREQUENCY",       2      },
    {"DRIVEIN_MAX_FEE",              1000   },
    {"DRIVEIN_MAX_BASE_REWARD",      200000 },
    {"DRIVEIN_MAX_TOTAL_REWARD",     300000 },
    {"DRIVEIN_MAX_DURATION",         3      },
    {"DRIVEIN_REQUEST_RETRY",        1      },
    {"DRIVEIN_REQUEST_TIMEOUT",      5.0    },
    {"DRIVEIN_LIST_REFRESH_DT",      300000 },
    {"CLUB_ACTIVE",                  1      },
    {"CLUB_STATS_MAXTRETRY",         10     },
    {"CLUB_STATS_TIMEOUT",           1      },
    {"CLUB_MAXTRETRY",               3      },
    {"CLUB_TIMEOUT",                 1      },
    {"EBAY_ACTIVE",                  1      },
};

edNetConfig::edNetConfig() : variables() {
    // FUN_0086dcc0
    for ( const auto &variable : kNetConfigVariables ) {
        variables.push_back(
            { variable.pName, variable.DefaultValue, variable.DefaultValue } );
    }
}

void edNetConfig::resetDefaults() {
    for ( Variable &variable : variables ) {
        variable.Value = variable.DefaultValue;
    }
}

bool edNetConfig::parse( const char *pXml ) {
    // FUN_0086ee30
    CMarkup markup( pXml );
    if ( !markup.findElem( "NETCONFIG" ) ) {
        return false;
    }
    markup.intoElem();

    for ( Variable &variable : variables ) {
        if ( !markup.findElem( variable.pName ) ) {
            continue;
        }

        const std::string value = markup.getData();
        if ( !value.empty() ) {
            variable.Value = atof( value.c_str() );
        }
    }
    return true;
}

std::string edNetConfig::toXml() const {
    std::string xml = "<NETCONFIG>\n";
    char line[128];
    for ( const Variable &variable : variables ) {
        if ( variable.Value == double( int64_t( variable.Value ) ) ) {
            snprintf( line, sizeof( line ), "<%s>%lld</%s>\n", variable.pName,
                      (long long)variable.Value, variable.pName );
        } else {
            snprintf( line, sizeof( line ), "<%s>%.3f</%s>\n", variable.pName,
                      variable.Value, variable.pName );
        }
        xml += line;
    }
    xml += "</NETCONFIG>\n";
    return xml;
}

const edNetConfig::Variable *edNetConfig::find( const char *pName ) const {
    for ( const Variable &variable : variables ) {
        if ( strcmp( variable.pName, pName ) == 0 ) {
            return &variable;
        }
    }
    OTDU_LOG_WARN( "Unknown netconfig variable '%s'\n", pName );
    return nullptr;
}

int32_t edNetConfig::getInt( const char *pName ) const {
    const Variable *pVariable = find( pName );
    return ( pVariable != nullptr ) ? int32_t( pVariable->Value ) : 0;
}

float edNetConfig::getFloat( const char *pName ) const {
    const Variable *pVariable = find( pName );
    return ( pVariable != nullptr ) ? float( pVariable->Value ) : 0.0f;
}
