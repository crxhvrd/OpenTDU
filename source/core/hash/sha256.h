#pragma once

#include <array>
#include <string>

using SHA256Digest_t = std::array<uint8_t, 32>;

SHA256Digest_t SHA256( const void *pData, const size_t size );
std::string ToHexString( const uint8_t *pData, const size_t size );

// Iterated salted SHA-256 (password storage).
std::string HashPassword( const std::string &password,
                          const std::string &saltHex );
std::string GenerateSaltHex();
