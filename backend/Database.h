#ifndef CEREVIA_DATABASE_H
#define CEREVIA_DATABASE_H

#include <sqlite3.h>

#include <mutex>
#include <string>

// ---------------------------------------------------------------------------
// Owns the SQLite connection, the schema, and everything that belongs to the
// single local user (PIN, recovery question, emergency contact, preferences).
//
// One connection is shared by every request thread, so callers that touch the
// raw handle must hold `mutex()`. The Database methods take it themselves.
// ---------------------------------------------------------------------------
class Database {
public:
    Database();
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool isOpen() const { return db_ != nullptr; }
    sqlite3 *getHandle() { return db_; }
    std::mutex &mutex() { return mutex_; }
    const std::string &path() const { return path_; }

    // The key journals are encrypted with. Read from CEREVIA_JOURNAL_KEY when
    // set, otherwise generated once and kept next to the database file.
    const std::string &journalKey() const { return journalKey_; }

    // ---- Authentication ---------------------------------------------------
    bool hasPIN();
    bool checkPIN(const std::string &pin);
    bool setPIN(const std::string &pin);
    bool verifySecurityAnswer(const std::string &answer);
    bool setSecurityQuestion(const std::string &question, const std::string &answer);

    // ---- Profile ----------------------------------------------------------
    std::string getSecurityQuestion();          // raw text
    std::string getEmergencyContact();          // raw text
    bool setEmergencyContact(const std::string &contact);
    std::string getDisplayName();
    bool setDisplayName(const std::string &name);

    // ---- Free-form key/value preferences ----------------------------------
    std::string getSetting(const std::string &key, const std::string &fallback = "");
    bool setSetting(const std::string &key, const std::string &value);

    // Runs a statement with no result set. Returns false and logs on error.
    bool exec(const std::string &statement);

private:
    void ensureSchema();
    void migrate();
    void loadJournalKey();
    bool columnExists(const std::string &table, const std::string &column);
    void addColumnIfMissing(const std::string &table,
                            const std::string &column,
                            const std::string &definition);

    sqlite3 *db_;
    std::string path_;
    std::string journalKey_;
    std::mutex mutex_;
};

#endif // CEREVIA_DATABASE_H
