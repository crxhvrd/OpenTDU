#pragma once

class Server;
class ActionService;

// OpenTDU-only RPCs used by opentdu_netprobe to exercise the reliable RPC and
// NetBuffer paths. Only registered when the server runs with -debug_rpcs.
namespace DebugRequests {
    static constexpr const char *kCounterRequest = "OpenTDU::r_debug_counter()";
    static constexpr const char *kCounterReply =
        "OpenTDU::a_debug_counter(uint)";
    static constexpr const char *kNetBufferRequest =
        "OpenTDU::r_debug_netbuffer(uint size,uint seed)";

    // Deterministic content of the r_debug_netbuffer payload.
    inline uint8_t GetNetBufferByte( const uint32_t seed,
                                     const uint32_t index ) {
        const uint32_t value = ( index + seed ) * 2654435761u;
        return uint8_t( value >> 24 );
    }
} // namespace DebugRequests

void RegisterDebugRequests( Server &server, ActionService &actions );
