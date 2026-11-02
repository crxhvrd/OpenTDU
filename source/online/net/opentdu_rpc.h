#pragma once

// OpenTDU-specific RPCs
namespace OpenTDURPC {
    // Replaces the GameSpy GP login done by edNetGamespyManager
    static constexpr const char *kIdentityLogin =
        "OpenTDU::Identity::r_login(string nick,string email,string "
        "password,string cdkey,byte mode)";
    //   error: edNetGamespyManager::eError (0 = success)
    //   profile_id: account id, sent back to the server in r_login_pc
    static constexpr const char *kIdentityLoginReply =
        "OpenTDU::Identity::a_login(byte error,uint profile_id)";
} // namespace OpenTDURPC
