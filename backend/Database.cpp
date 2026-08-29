#include "Database.h"

#include "Encryption.h"
#include "Paths.h"
#include "Sql.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {

const char *kDefaultPin              = "1234";
const char *kDefaultSecurityQuestion = "What is your favourite colour?";
const char *kDefaultSecurityAnswer   = "blue";
const char *kDefaultEmergency        = "112";

} // namespace

Database::Database() : db_(nullptr)
{
    paths::ensureDirectory(paths::databaseDir());
    path_ = paths::databaseFile();

    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[db] Unable to open " << path_ << ": "
                  << (db_ ? sqlite3_errmsg(db_) : "unknown error") << std::endl;
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return;
    }

    // WAL keeps reads from blocking behind a write, and the busy timeout means
    // a concurrent request waits instead of failing outright.
    exec("PRAGMA journal_mode = WAL;");
    exec("PRAGMA foreign_keys = ON;");
    sqlite3_busy_timeout(db_, 3000);

    ensureSchema();
    migrate();
    loadJournalKey();

    std::cout << "[db] Ready at " << path_ << std::endl;
}

Database::~Database()
{
    if (db_) sqlite3_close(db_);
}

bool Database::exec(const std::string &statement)
{
    if (!db_) return false;
    char *error = nullptr;
    if (sqlite3_exec(db_, statement.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::cerr << "[db] " << (error ? error : "unknown SQL error") << std::endl;
        sqlite3_free(error);
        return false;
    }
    return true;
}

void Database::ensureSchema()
{
    exec("CREATE TABLE IF NOT EXISTS users ("
         "  id                INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  pin               TEXT    NOT NULL,"
         "  security_question TEXT    NOT NULL,"
         "  security_answer   TEXT    NOT NULL,"
         "  emergency_contact TEXT    NOT NULL DEFAULT '112',"
         "  display_name      TEXT    NOT NULL DEFAULT 'friend',"
         "  created_at        TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))"
         ");");

    exec("CREATE TABLE IF NOT EXISTS moods ("
         "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  mood       TEXT    NOT NULL,"
         "  level      INTEGER NOT NULL CHECK (level BETWEEN 1 AND 10),"
         "  note       TEXT    NOT NULL DEFAULT '',"
         "  tags       TEXT    NOT NULL DEFAULT '',"
         "  date       TEXT    NOT NULL,"                      // human label, kept for continuity
         "  created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))"
         ");");

    exec("CREATE TABLE IF NOT EXISTS journals ("
         "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  text       TEXT    NOT NULL,"                      // encrypted at rest
         "  mood       TEXT    NOT NULL DEFAULT '',"
         "  prompt     TEXT    NOT NULL DEFAULT '',"
         "  word_count INTEGER NOT NULL DEFAULT 0,"
         "  date       TEXT    NOT NULL,"
         "  created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))"
         ");");

    exec("CREATE TABLE IF NOT EXISTS breathing_sessions ("
         "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  technique  TEXT    NOT NULL DEFAULT 'box',"
         "  cycles     INTEGER NOT NULL DEFAULT 0,"
         "  seconds    INTEGER NOT NULL DEFAULT 0,"
         "  created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))"
         ");");

    exec("CREATE TABLE IF NOT EXISTS settings ("
         "  key   TEXT PRIMARY KEY,"
         "  value TEXT NOT NULL"
         ");");

    // Every dashboard query filters or orders by time, so index it.
    exec("CREATE INDEX IF NOT EXISTS idx_moods_created    ON moods(created_at DESC);");
    exec("CREATE INDEX IF NOT EXISTS idx_journals_created ON journals(created_at DESC);");
    exec("CREATE INDEX IF NOT EXISTS idx_breathing_created ON breathing_sessions(created_at DESC);");

    exec("INSERT INTO users (id, pin, security_question, security_answer, emergency_contact, display_name) "
         "SELECT 1, '" + std::string(kDefaultPin) + "', '" + kDefaultSecurityQuestion + "', '" +
         kDefaultSecurityAnswer + "', '" + kDefaultEmergency + "', 'friend' "
         "WHERE NOT EXISTS (SELECT 1 FROM users WHERE id = 1);");
}

bool Database::columnExists(const std::string &table, const std::string &column)
{
    sql::Stmt stmt(db_, ("PRAGMA table_info(" + table + ");").c_str());
    if (!stmt) return false;
    while (stmt.step()) {
        if (stmt.text(1) == column) return true;
    }
    return false;
}

void Database::addColumnIfMissing(const std::string &table,
                                  const std::string &column,
                                  const std::string &definition)
{
    if (columnExists(table, column)) return;
    std::cout << "[db] Migrating: adding " << table << "." << column << std::endl;
    exec("ALTER TABLE " + table + " ADD COLUMN " + column + " " + definition + ";");
}

void Database::migrate()
{
    if (!db_) return;

    // Databases created by earlier versions are missing these columns.
    addColumnIfMissing("users",    "emergency_contact", "TEXT NOT NULL DEFAULT '112'");
    addColumnIfMissing("users",    "display_name",      "TEXT NOT NULL DEFAULT 'friend'");
    addColumnIfMissing("users",    "created_at",        "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing("moods",    "note",              "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing("moods",    "tags",              "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing("moods",    "created_at",        "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing("journals", "mood",              "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing("journals", "prompt",            "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing("journals", "word_count",        "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing("journals", "created_at",        "TEXT NOT NULL DEFAULT ''");

    // Rows that predate created_at get a timestamp so ordering and the streak
    // calculation still have something to work with.
    exec("UPDATE moods    SET created_at = datetime('now', 'localtime') WHERE created_at IS NULL OR created_at = '';");
    exec("UPDATE journals SET created_at = datetime('now', 'localtime') WHERE created_at IS NULL OR created_at = '';");

    // Upgrade any plaintext PIN / security answer left over from v1.
    sql::Stmt stmt(db_, "SELECT pin, security_answer FROM users WHERE id = 1");
    if (stmt.step()) {
        const std::string storedPin    = stmt.text(0);
        const std::string storedAnswer = stmt.text(1);

        if (storedPin.find('$') == std::string::npos) {
            std::cout << "[db] Migrating: hashing stored PIN" << std::endl;
            sql::Stmt update(db_, "UPDATE users SET pin = ? WHERE id = 1");
            update.bind(1, Encryption::hashSecret(storedPin));
            update.run();
        }
        if (storedAnswer.find('$') == std::string::npos) {
            sql::Stmt update(db_, "UPDATE users SET security_answer = ? WHERE id = 1");
            update.bind(1, Encryption::hashSecret(storedAnswer));
            update.run();
        }
    }
}

void Database::loadJournalKey()
{
    if (const char *fromEnv = std::getenv("CEREVIA_JOURNAL_KEY")) {
        if (*fromEnv) {
            journalKey_ = fromEnv;
            return;
        }
    }

    // Otherwise keep a generated key beside the database. It stays on this
    // machine and is regenerated only if deleted (which makes old entries
    // unreadable, so the file is worth backing up alongside the .db).
    const std::string keyFile = paths::databaseDir() + "/journal.key";
    std::ifstream existing(keyFile);
    if (existing.is_open()) {
        std::getline(existing, journalKey_);
        if (!journalKey_.empty()) return;
    }

    journalKey_ = Encryption::randomHex(32);
    std::ofstream created(keyFile, std::ios::trunc);
    if (created.is_open()) {
        created << journalKey_ << std::endl;
    } else {
        std::cerr << "[db] Could not persist journal key at " << keyFile
                  << "; entries written now will not be readable after restart." << std::endl;
    }
}

// ---- Authentication -------------------------------------------------------

bool Database::hasPIN()
{
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "SELECT pin FROM users WHERE id = 1");
    return stmt.step() && !stmt.text(0).empty();
}

bool Database::checkPIN(const std::string &pin)
{
    if (pin.empty()) return false;
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "SELECT pin FROM users WHERE id = 1");
    if (!stmt.step()) return false;
    return Encryption::verifySecret(pin, stmt.text(0));
}

bool Database::setPIN(const std::string &pin)
{
    if (pin.empty()) return false;
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "UPDATE users SET pin = ? WHERE id = 1");
    stmt.bind(1, Encryption::hashSecret(pin));
    return stmt.run();
}

bool Database::verifySecurityAnswer(const std::string &answer)
{
    if (answer.empty()) return false;

    // Recovery answers are compared case-insensitively with the surrounding
    // whitespace stripped — "Blue " should not lock someone out of their own app.
    std::string normalised;
    for (char c : answer) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        normalised.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "SELECT security_answer FROM users WHERE id = 1");
    if (!stmt.step()) return false;
    const std::string stored = stmt.text(0);
    return Encryption::verifySecret(normalised, stored) || Encryption::verifySecret(answer, stored);
}

bool Database::setSecurityQuestion(const std::string &question, const std::string &answer)
{
    if (question.empty() || answer.empty()) return false;

    std::string normalised;
    for (char c : answer) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        normalised.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "UPDATE users SET security_question = ?, security_answer = ? WHERE id = 1");
    stmt.bind(1, question).bind(2, Encryption::hashSecret(normalised));
    return stmt.run();
}

// ---- Profile --------------------------------------------------------------

std::string Database::getSecurityQuestion()
{
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "SELECT security_question FROM users WHERE id = 1");
    if (!stmt.step()) return kDefaultSecurityQuestion;
    const std::string question = stmt.text(0);
    return question.empty() ? kDefaultSecurityQuestion : question;
}

std::string Database::getEmergencyContact()
{
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "SELECT emergency_contact FROM users WHERE id = 1");
    if (!stmt.step()) return kDefaultEmergency;
    const std::string contact = stmt.text(0);
    return contact.empty() ? kDefaultEmergency : contact;
}

bool Database::setEmergencyContact(const std::string &contact)
{
    if (contact.empty()) return false;
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "UPDATE users SET emergency_contact = ? WHERE id = 1");
    stmt.bind(1, contact);
    return stmt.run();
}

std::string Database::getDisplayName()
{
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "SELECT display_name FROM users WHERE id = 1");
    if (!stmt.step()) return "friend";
    const std::string name = stmt.text(0);
    return name.empty() ? "friend" : name;
}

bool Database::setDisplayName(const std::string &name)
{
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "UPDATE users SET display_name = ? WHERE id = 1");
    stmt.bind(1, name.empty() ? std::string("friend") : name);
    return stmt.run();
}

// ---- Settings -------------------------------------------------------------

std::string Database::getSetting(const std::string &key, const std::string &fallback)
{
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "SELECT value FROM settings WHERE key = ?");
    stmt.bind(1, key);
    if (!stmt.step()) return fallback;
    return stmt.text(0, fallback);
}

bool Database::setSetting(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> guard(mutex_);
    sql::Stmt stmt(db_, "INSERT INTO settings(key, value) VALUES (?, ?) "
                        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    stmt.bind(1, key).bind(2, value);
    return stmt.run();
}
