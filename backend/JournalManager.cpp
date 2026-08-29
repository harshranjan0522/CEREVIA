#include "JournalManager.h"

#include "Encryption.h"
#include "Sql.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

using json = nlohmann::json;

namespace {

const std::vector<std::string> kPrompts = {
    "What is taking up the most space in your head right now?",
    "Name one thing that went better than you expected today.",
    "If today had a colour, which one would it be, and why?",
    "What is one thing you needed today that you did not get?",
    "Who or what made today feel lighter?",
    "What would you say to a friend who had your exact day?",
    "What is one small thing you can let go of tonight?",
    "Where did you feel tension in your body today?",
    "What are you quietly proud of this week?",
    "What do you want tomorrow to feel like?",
    "What is one thing you are avoiding, and what makes it hard?",
    "Describe a moment today when you felt like yourself.",
};

std::string humanTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    std::ostringstream oss;
    oss << std::put_time(&parts, "%a, %d %b %Y %H:%M");
    return oss.str();
}

std::string trimCopy(const std::string &value, size_t maxLength)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
    std::string trimmed = value.substr(start, end - start);
    if (trimmed.size() > maxLength) trimmed.resize(maxLength);
    return trimmed;
}

int countWords(const std::string &text)
{
    int words = 0;
    bool inWord = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            words++;
        }
    }
    return words;
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

JournalManager::JournalManager(Database &db) : db_(db) {}

json JournalManager::prompts()
{
    json list = json::array();
    for (const auto &prompt : kPrompts) list.push_back(prompt);
    return list;
}

std::string JournalManager::randomPrompt()
{
    static thread_local std::mt19937 engine(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<size_t> dist(0, kPrompts.size() - 1);
    return kPrompts[dist(engine)];
}

json JournalManager::add(const json &payload)
{
    if (!payload.is_object()) return json{{"error", "Expected a JSON object."}};

    const std::string text = trimCopy(payload.value("text", std::string()), 20000);
    if (text.empty()) return json{{"error", "Write something before saving."}};

    const std::string mood = trimCopy(payload.value("mood", std::string()), 40);
    const std::string prompt = trimCopy(payload.value("prompt", std::string()), 300);
    const std::string label = humanTimestamp();
    const int words = countWords(text);

    const std::string encrypted = Encryption::encrypt(text, db_.journalKey());

    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(),
                   "INSERT INTO journals(text, mood, prompt, word_count, date, created_at) "
                   "VALUES (?, ?, ?, ?, ?, datetime('now', 'localtime'))");
    if (!stmt) return json{{"error", "Could not prepare the insert."}};

    stmt.bind(1, encrypted).bind(2, mood).bind(3, prompt).bind(4, words).bind(5, label);
    if (!stmt.run()) return json{{"error", "Could not save the entry."}};

    return json{
        {"id", static_cast<int>(sqlite3_last_insert_rowid(db_.getHandle()))},
        {"text", text},
        {"mood", mood},
        {"prompt", prompt},
        {"wordCount", words},
        {"date", label},
    };
}

json JournalManager::list(const std::string &search, int limit)
{
    const int maxRows = (limit <= 0) ? 200 : std::min(limit, 1000);
    const std::string needle = toLower(trimCopy(search, 200));

    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(),
                   "SELECT id, text, mood, prompt, word_count, date, created_at FROM journals "
                   "ORDER BY datetime(created_at) DESC, id DESC");
    if (!stmt) return json::array();

    // Entries are encrypted at rest, so a LIKE query against the stored blob
    // would match nothing. Searching therefore happens after decryption.
    json rows = json::array();
    while (stmt.step() && static_cast<int>(rows.size()) < maxRows) {
        const std::string plain = Encryption::decrypt(stmt.text(1), db_.journalKey());
        if (!needle.empty() && toLower(plain).find(needle) == std::string::npos) continue;

        rows.push_back({
            {"id", stmt.integer(0)},
            {"text", plain},
            {"mood", stmt.text(2)},
            {"prompt", stmt.text(3)},
            {"wordCount", stmt.integer(4, countWords(plain))},
            {"date", stmt.text(5)},
            {"createdAt", stmt.text(6)},
        });
    }
    return rows;
}

json JournalManager::stats()
{
    std::lock_guard<std::mutex> guard(db_.mutex());

    int count = 0;
    int words = 0;
    {
        sql::Stmt stmt(db_.getHandle(),
                       "SELECT COUNT(*), COALESCE(SUM(word_count), 0) FROM journals");
        if (stmt && stmt.step()) {
            count = stmt.integer(0);
            words = stmt.integer(1);
        }
    }

    int daysWritten = 0;
    {
        sql::Stmt stmt(db_.getHandle(), "SELECT COUNT(DISTINCT date(created_at)) FROM journals");
        if (stmt && stmt.step()) daysWritten = stmt.integer(0);
    }

    std::string lastEntry;
    {
        sql::Stmt stmt(db_.getHandle(),
                       "SELECT date FROM journals ORDER BY datetime(created_at) DESC, id DESC LIMIT 1");
        if (stmt && stmt.step()) lastEntry = stmt.text(0);
    }

    return json{
        {"count", count},
        {"words", words},
        {"daysWritten", daysWritten},
        {"lastEntry", lastEntry},
    };
}

bool JournalManager::remove(int id)
{
    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(), "DELETE FROM journals WHERE id = ?");
    stmt.bind(1, id);
    if (!stmt.run()) return false;
    return sqlite3_changes(db_.getHandle()) > 0;
}

int JournalManager::clear()
{
    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(), "DELETE FROM journals");
    stmt.run();
    return sqlite3_changes(db_.getHandle());
}
