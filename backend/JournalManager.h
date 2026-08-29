#ifndef CEREVIA_JOURNALMANAGER_H
#define CEREVIA_JOURNALMANAGER_H

#include "Database.h"
#include "libs/json/json.hpp"

#include <string>

// ---------------------------------------------------------------------------
// Journal entries. Bodies are encrypted before they touch the database and
// decrypted on the way out, so the raw .db file never contains readable prose.
//
// Unlike the previous version, nothing here silently deletes the user's
// writing — entries only disappear when they ask.
// ---------------------------------------------------------------------------
class JournalManager {
public:
    explicit JournalManager(Database &db);

    // Returns the stored entry, or an object with "error".
    nlohmann::json add(const nlohmann::json &payload);
    nlohmann::json list(const std::string &search, int limit);
    nlohmann::json stats();
    bool remove(int id);
    int clear();

    // Rotating writing prompts, so a blank page is never the only option.
    static nlohmann::json prompts();
    static std::string randomPrompt();

private:
    Database &db_;
};

#endif // CEREVIA_JOURNALMANAGER_H
