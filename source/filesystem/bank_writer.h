#pragma once

// OTDU_ADDITION: rewrites a .bnk with one entry's content replaced. Does not
// generate valid banks for the vanilla game (CRC table is stale)
bool ReplaceBankEntry( const std::vector<uint8_t> &bank,
                       const int32_t entryIndex,
                       const std::vector<uint8_t> &content,
                       std::vector<uint8_t> &outBank,
                       std::string &outError );

// Index of the first entry whose content starts with 'magic', or -1
int32_t FindBankEntryByMagic( const std::vector<uint8_t> &bank,
                              const uint32_t magic );
