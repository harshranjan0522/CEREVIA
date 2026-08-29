#include "Encryption.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

namespace {

// ---- SHA-256 -------------------------------------------------------------
// Compact public-domain-style implementation; no external crypto library is
// vendored with this project and we only need a digest, not a full suite.

constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t value, uint32_t bits) { return (value >> bits) | (value << (32 - bits)); }

std::array<uint8_t, 32> sha256(const std::string &input)
{
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    std::string message = input;
    const uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8ull;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56) message.push_back('\0');
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<char>((bitLength >> (i * 8)) & 0xff));
    }

    for (size_t offset = 0; offset < message.size(); offset += 64) {
        uint32_t w[64] = {0};
        for (int i = 0; i < 16; ++i) {
            const size_t j = offset + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(static_cast<uint8_t>(message[j])) << 24) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(message[j + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(message[j + 2])) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(message[j + 3])));
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = hh + S1 + ch + kRoundConstants[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;

            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::array<uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xff);
    }
    return digest;
}

std::string toHex(const std::string &data)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : data) oss << std::setw(2) << static_cast<int>(c);
    return oss.str();
}

std::string fromHex(const std::string &hex)
{
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const std::string byteStr = hex.substr(i, 2);
        char *end = nullptr;
        const long value = std::strtol(byteStr.c_str(), &end, 16);
        if (!end || *end != '\0') return {};
        out.push_back(static_cast<char>(value));
    }
    return out;
}

// Generates a keystream by hashing key || nonce || blockCounter, which is the
// standard "hash in counter mode" construction.
std::string keystream(const std::string &key, const std::string &nonce, size_t length)
{
    std::string stream;
    stream.reserve(length + 32);
    uint32_t counter = 0;
    while (stream.size() < length) {
        std::string block = key + nonce;
        for (int i = 3; i >= 0; --i) {
            block.push_back(static_cast<char>((counter >> (i * 8)) & 0xff));
        }
        const auto digest = sha256(block);
        stream.append(reinterpret_cast<const char *>(digest.data()), digest.size());
        counter++;
    }
    stream.resize(length);
    return stream;
}

std::string xorWith(const std::string &data, const std::string &pad)
{
    std::string out = data;
    if (pad.empty()) return out;
    for (size_t i = 0; i < data.size(); ++i) {
        out[i] = static_cast<char>(data[i] ^ pad[i % pad.size()]);
    }
    return out;
}

} // namespace

std::string Encryption::randomHex(size_t bytes)
{
    // random_device is seeded from the OS on every platform we target; the
    // clock mix-in is belt-and-braces for the rare implementation that isn't.
    static thread_local std::mt19937_64 engine([] {
        std::random_device rd;
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return static_cast<uint64_t>(rd()) ^ static_cast<uint64_t>(now);
    }());
    std::uniform_int_distribution<int> dist(0, 255);

    std::string raw;
    raw.reserve(bytes);
    for (size_t i = 0; i < bytes; ++i) raw.push_back(static_cast<char>(dist(engine)));
    return toHex(raw);
}

std::string Encryption::sha256Hex(const std::string &input)
{
    const auto digest = sha256(input);
    return toHex(std::string(reinterpret_cast<const char *>(digest.data()), digest.size()));
}

std::string Encryption::hashSecret(const std::string &secret, const std::string &saltHex)
{
    const std::string salt = saltHex.empty() ? randomHex(16) : saltHex;
    // A few thousand rounds keeps a 4-digit PIN from being brute-forced
    // instantly off a stolen database file without making login feel slow.
    std::string digest = sha256Hex(salt + ":" + secret);
    for (int i = 0; i < 4096; ++i) digest = sha256Hex(digest + salt);
    return salt + "$" + digest;
}

bool Encryption::verifySecret(const std::string &secret, const std::string &stored)
{
    const size_t sep = stored.find('$');
    if (sep == std::string::npos) {
        // Legacy row from before hashing existed: compare in the clear so the
        // user can still get in; Database::ensureSchema upgrades it after.
        return stored == secret;
    }
    const std::string salt = stored.substr(0, sep);
    const std::string expected = hashSecret(secret, salt);

    // Constant-time-ish comparison so timing does not leak the digest.
    if (expected.size() != stored.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        diff |= static_cast<unsigned char>(expected[i] ^ stored[i]);
    }
    return diff == 0;
}

std::string Encryption::encrypt(const std::string &plain, const std::string &key)
{
    const std::string nonce = randomHex(12);
    const std::string pad = keystream(key, nonce, plain.size());
    return "v2:" + nonce + ":" + toHex(xorWith(plain, pad));
}

std::string Encryption::decrypt(const std::string &cipher, const std::string &key)
{
    if (cipher.rfind("v2:", 0) == 0) {
        const size_t firstColon = 2;
        const size_t secondColon = cipher.find(':', firstColon + 1);
        if (secondColon == std::string::npos) return {};
        const std::string nonce = cipher.substr(firstColon + 1, secondColon - firstColon - 1);
        const std::string payload = fromHex(cipher.substr(secondColon + 1));
        return xorWith(payload, keystream(key, nonce, payload.size()));
    }
    // Legacy format: fixed XOR against the literal key "secret".
    return xorWith(fromHex(cipher), "secret");
}
