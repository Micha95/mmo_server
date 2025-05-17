#include "Security.h"
#include <openssl/evp.h>
#include <lz4.h>
#include <cstring>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace Crypto {
    // AES-256-CBC, PKCS7 padding, IV is all zeroes (static key)
    bool encrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, const std::vector<uint8_t>& key) {
        if (key.size() != 32) return false; // AES-256 requires 32-byte key
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;
        unsigned char iv[32] = {0};
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        out.resize(in.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
        int outlen1 = 0, outlen2 = 0;
        if (EVP_EncryptUpdate(ctx, out.data(), &outlen1, in.data(), (int)in.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        if (EVP_EncryptFinal_ex(ctx, out.data() + outlen1, &outlen2) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        out.resize(outlen1 + outlen2);
        EVP_CIPHER_CTX_free(ctx);
        return true;
    }
    bool decrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, const std::vector<uint8_t>& key) {
        if (key.size() != 32) return false;
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;
        unsigned char iv[32] = {0};
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        out.resize(in.size());
        int outlen1 = 0, outlen2 = 0;
        if (EVP_DecryptUpdate(ctx, out.data(), &outlen1, in.data(), (int)in.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        if (EVP_DecryptFinal_ex(ctx, out.data() + outlen1, &outlen2) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        out.resize(outlen1 + outlen2);
        EVP_CIPHER_CTX_free(ctx);
        return true;
    }
}

namespace Compression {
    bool compress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
        if (in.empty()) return false;
        int maxDstSize = LZ4_compressBound((int)in.size());
        std::vector<uint8_t> compressed(maxDstSize);
        int compressedSize = LZ4_compress_default((const char*)in.data(), (char*)compressed.data(), (int)in.size(), maxDstSize);
        if (compressedSize <= 0) return false;
        out.resize(4 + compressedSize);
        // Store original size in network byte order
        uint32_t origSize = htonl((uint32_t)in.size());
        std::memcpy(out.data(), &origSize, 4);
        std::memcpy(out.data() + 4, compressed.data(), compressedSize);
        return true;
    }
    bool decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
        if (in.size() < 4) return false;
        uint32_t origSize = 0;
        std::memcpy(&origSize, in.data(), 4);
        origSize = ntohl(origSize);
        out.resize(origSize);
        int decompressedSize = LZ4_decompress_safe((const char*)in.data() + 4, (char*)out.data(), (int)in.size() - 4, origSize);
        if (decompressedSize < 0) return false;
        out.resize(decompressedSize);
        return true;
    }
}
