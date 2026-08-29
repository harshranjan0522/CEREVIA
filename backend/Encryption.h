#ifndef CEREVIA_ENCRYPTION_H
#define CEREVIA_ENCRYPTION_H

#include <string>

// ---------------------------------------------------------------------------
// Local-only cryptography helpers.
//
// CEREVIA keeps everything on the user's own machine, so the goal here is
// "nobody can read your journal by opening the .db file in a text editor",
// not "resists a determined attacker with the disk". See README > Security for
// an honest description of what this does and does not protect against.
// ---------------------------------------------------------------------------
class Encryption {
public:
    // SHA-256 of `input`, returned as 64 lower-case hex characters.
    static std::string sha256Hex(const std::string &input);

    // Salted digest used for PINs and security answers.
    // Stored form is "<saltHex>$<digestHex>" so the salt travels with the hash.
    static std::string hashSecret(const std::string &secret, const std::string &saltHex = "");
    static bool verifySecret(const std::string &secret, const std::string &stored);

    // Journal body cipher. Output is "v2:<nonceHex>:<cipherHex>" — a fresh
    // random nonce per entry keeps two identical entries from looking alike.
    static std::string encrypt(const std::string &plain, const std::string &key);

    // Accepts both the v2 format and the original fixed-XOR hex blobs so
    // journals written by the previous version still open.
    static std::string decrypt(const std::string &cipher, const std::string &key);

    static std::string randomHex(size_t bytes);
};

#endif // CEREVIA_ENCRYPTION_H
