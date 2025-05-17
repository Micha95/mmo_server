#pragma once
#include <cstdint>
#include <vector>

// Encryption/Decryption using OpenSSL (stub)
namespace Crypto {
    bool encrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, const std::vector<uint8_t>& key);
    bool decrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, const std::vector<uint8_t>& key);
}

// Compression/Decompression using LZO (stub)
namespace Compression {
    bool compress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
    bool decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
}
