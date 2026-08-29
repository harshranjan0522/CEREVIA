#include "MoodTracker.h"

#include "Paths.h"
#include "Sql.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>

using json = nlohmann::json;

namespace {

// Valence is how the feeling sits on a pleasant/unpleasant axis. Level (1-10)
// is how strongly it is felt, so the two multiply: "slightly sad" scores much
// closer to neutral than "overwhelmingly sad".
const std::vector<std::pair<std::string, double>> kMoodValence = {
    {"Happy",   1.00},
    {"Calm",    0.85},
    {"Neutral", 0.10},
    {"Anxious", -0.55},
    {"Angry",   -0.70},
    {"Sad",     -0.90},
};

// Moods that mean "check in on this person" when they repeat.
bool isLowMood(const std::string &mood)
{
    return mood == "Sad" || mood == "Angry" || mood == "Anxious";
}

std::string todayIso()
{
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    std::ostringstream oss;
    oss << std::put_time(&parts, "%Y-%m-%d");
    return oss.str();
}

std::string humanDate()
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

// Loads a JSON data file from the backend directory.
bool loadDataFile(const std::string &filename, json &out)
{
    const std::vector<std::string> candidates = {
        paths::backendDir() + "/" + filename,
        paths::resolve(filename),
        filename,
    };
    for (const auto &candidate : candidates) {
        std::ifstream file(candidate);
        if (!file.is_open()) continue;
        try {
            file >> out;
            return true;
        } catch (const std::exception &) {
            return false; // present but corrupt: report rather than silently skip
        }
    }
    return false;
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

int pickIndex(size_t size)
{
    if (size == 0) return 0;
    // Seeded from the clock once per process, so suggestions differ between
    // runs (the old code called rand() without ever calling srand()).
    static thread_local std::mt19937 engine(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<size_t> dist(0, size - 1);
    return static_cast<int>(dist(engine));
}

} // namespace

MoodTracker::MoodTracker(Database &db) : db_(db) {}

const std::vector<std::string> &MoodTracker::vocabulary()
{
    static const std::vector<std::string> moods = [] {
        std::vector<std::string> names;
        names.reserve(kMoodValence.size());
        for (const auto &entry : kMoodValence) names.push_back(entry.first);
        return names;
    }();
    return moods;
}

bool MoodTracker::isKnownMood(const std::string &mood)
{
    const auto &moods = vocabulary();
    return std::find(moods.begin(), moods.end(), mood) != moods.end();
}

std::string MoodTracker::tryNormalise(const std::string &mood)
{
    if (mood.empty()) return {};
    // Accept any casing from the client ("happy", "HAPPY") and map it back to
    // the canonical spelling used everywhere else.
    std::string lowered;
    for (char c : mood) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (const auto &entry : kMoodValence) {
        std::string candidate;
        for (char c : entry.first) {
            candidate.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (candidate == lowered) return entry.first;
    }
    return {};
}

std::string MoodTracker::normalise(const std::string &mood)
{
    const std::string match = tryNormalise(mood);
    return match.empty() ? "Neutral" : match;
}

double MoodTracker::valence(const std::string &mood)
{
    for (const auto &entry : kMoodValence) {
        if (entry.first == mood) return entry.second;
    }
    return 0.0;
}

int MoodTracker::wellbeingScore(const std::string &mood, int level)
{
    const int clamped = std::max(1, std::min(10, level));
    const double intensity = static_cast<double>(clamped) / 10.0;
    const double score = 50.0 + valence(normalise(mood)) * intensity * 50.0;
    return static_cast<int>(std::lround(std::max(0.0, std::min(100.0, score))));
}

// ---- Writes ---------------------------------------------------------------

json MoodTracker::add(const json &payload)
{
    if (!payload.is_object()) {
        return json{{"error", "Expected a JSON object."}};
    }

    const std::string rawMood = payload.value("mood", std::string());
    if (rawMood.empty()) {
        return json{{"error", "A mood is required."}};
    }
    // An unrecognised mood is rejected rather than quietly filed as Neutral,
    // which would silently corrupt the user's own history.
    const std::string mood = tryNormalise(rawMood);
    if (mood.empty()) {
        return json{{"error", "Unknown mood. Expected one of: Happy, Calm, Neutral, Anxious, Angry, Sad."}};
    }
    int level = 5;
    if (payload.contains("level") && payload["level"].is_number()) {
        level = payload["level"].get<int>();
    }
    if (level < 1 || level > 10) {
        return json{{"error", "Level must be between 1 and 10."}};
    }

    const std::string note = trimCopy(payload.value("note", std::string()), 2000);

    std::string tags;
    if (payload.contains("tags") && payload["tags"].is_array()) {
        for (const auto &tag : payload["tags"]) {
            if (!tag.is_string()) continue;
            const std::string cleaned = trimCopy(tag.get<std::string>(), 40);
            if (cleaned.empty()) continue;
            if (!tags.empty()) tags.push_back(',');
            tags += cleaned;
        }
    }

    const std::string label = humanDate();

    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(),
                   "INSERT INTO moods(mood, level, note, tags, date, created_at) "
                   "VALUES (?, ?, ?, ?, ?, datetime('now', 'localtime'))");
    if (!stmt) return json{{"error", "Could not prepare the insert."}};

    stmt.bind(1, mood).bind(2, level).bind(3, note).bind(4, tags).bind(5, label);
    if (!stmt.run()) return json{{"error", "Could not save the check-in."}};

    const int id = static_cast<int>(sqlite3_last_insert_rowid(db_.getHandle()));
    return json{
        {"id", id},
        {"mood", mood},
        {"level", level},
        {"note", note},
        {"tags", tags},
        {"date", label},
        {"score", wellbeingScore(mood, level)},
    };
}

bool MoodTracker::remove(int id)
{
    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(), "DELETE FROM moods WHERE id = ?");
    stmt.bind(1, id);
    if (!stmt.run()) return false;
    return sqlite3_changes(db_.getHandle()) > 0;
}

int MoodTracker::clear()
{
    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(), "DELETE FROM moods");
    stmt.run();
    return sqlite3_changes(db_.getHandle());
}

// ---- Reads ----------------------------------------------------------------

json MoodTracker::list(int days, int limit)
{
    const int windowDays = std::max(0, days);
    const int maxRows = (limit <= 0) ? 500 : std::min(limit, 1000);

    std::lock_guard<std::mutex> guard(db_.mutex());
    const std::string query =
        "SELECT id, mood, level, note, tags, date, created_at FROM moods " +
        std::string(windowDays > 0 ? "WHERE created_at >= datetime('now', 'localtime', ?) " : "") +
        "ORDER BY datetime(created_at) DESC, id DESC LIMIT ?";

    sql::Stmt stmt(db_.getHandle(), query.c_str());
    if (!stmt) return json::array();

    int index = 1;
    if (windowDays > 0) stmt.bind(index++, "-" + std::to_string(windowDays) + " days");
    stmt.bind(index, maxRows);

    json rows = json::array();
    while (stmt.step()) {
        const std::string mood = stmt.text(1, "Neutral");
        const int level = stmt.integer(2, 5);
        rows.push_back({
            {"id", stmt.integer(0)},
            {"mood", mood},
            {"level", level},
            {"note", stmt.text(3)},
            {"tags", stmt.text(4)},
            {"date", stmt.text(5)},
            {"createdAt", stmt.text(6)},
            {"score", wellbeingScore(mood, level)},
        });
    }
    return rows;
}

json MoodTracker::latest()
{
    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(),
                   "SELECT id, mood, level, note, date, created_at FROM moods "
                   "ORDER BY datetime(created_at) DESC, id DESC LIMIT 1");
    if (!stmt || !stmt.step()) {
        return json{{"hasMood", false}, {"mood", nullptr}};
    }

    const std::string mood = stmt.text(1, "Neutral");
    const int level = stmt.integer(2, 5);
    return json{
        {"hasMood", true},
        {"id", stmt.integer(0)},
        {"mood", mood},
        {"level", level},
        {"note", stmt.text(3)},
        {"date", stmt.text(4)},
        {"createdAt", stmt.text(5)},
        {"score", wellbeingScore(mood, level)},
    };
}

json MoodTracker::distribution(int days)
{
    sql::Stmt stmt(db_.getHandle(),
                   "SELECT mood, COUNT(*) FROM moods "
                   "WHERE created_at >= datetime('now', 'localtime', ?) GROUP BY mood");
    json counts = json::object();
    for (const auto &mood : vocabulary()) counts[mood] = 0;
    if (!stmt) return counts;

    stmt.bind(1, "-" + std::to_string(std::max(1, days)) + " days");
    while (stmt.step()) {
        counts[normalise(stmt.text(0))] = stmt.integer(1);
    }
    return counts;
}

json MoodTracker::trend(int days)
{
    const int windowDays = std::max(1, std::min(days, 90));

    // One point per calendar day, including days with no check-in so the chart
    // shows gaps honestly instead of compressing them away.
    sql::Stmt stmt(db_.getHandle(),
                   "SELECT date(created_at) AS day, mood, level, COUNT(*) "
                   "FROM moods WHERE created_at >= datetime('now', 'localtime', ?) "
                   "GROUP BY day, mood ORDER BY day ASC");

    std::map<std::string, std::pair<double, int>> dayScores; // day -> (scoreSum, count)
    std::map<std::string, std::pair<std::string, int>> dayTop; // day -> (mood, count)

    if (stmt) {
        stmt.bind(1, "-" + std::to_string(windowDays - 1) + " days");
        while (stmt.step()) {
            const std::string day = stmt.text(0);
            const std::string mood = normalise(stmt.text(1));
            const int level = stmt.integer(2, 5);
            const int count = stmt.integer(3, 1);

            auto &bucket = dayScores[day];
            bucket.first += static_cast<double>(wellbeingScore(mood, level)) * count;
            bucket.second += count;

            auto &top = dayTop[day];
            if (count > top.second) top = {mood, count};
        }
    }

    json points = json::array();
    const std::time_t now = std::time(nullptr);
    for (int offset = windowDays - 1; offset >= 0; --offset) {
        const std::time_t stamp = now - static_cast<std::time_t>(offset) * 86400;
        std::tm parts{};
#if defined(_WIN32)
        localtime_s(&parts, &stamp);
#else
        localtime_r(&stamp, &parts);
#endif
        std::ostringstream iso, label;
        iso << std::put_time(&parts, "%Y-%m-%d");
        label << std::put_time(&parts, "%a");

        const std::string day = iso.str();
        const auto scoreIt = dayScores.find(day);
        const bool hasData = scoreIt != dayScores.end() && scoreIt->second.second > 0;

        points.push_back({
            {"date", day},
            {"label", label.str()},
            {"hasData", hasData},
            {"score", hasData ? static_cast<int>(std::lround(scoreIt->second.first / scoreIt->second.second)) : 0},
            {"entries", hasData ? scoreIt->second.second : 0},
            {"mood", dayTop.count(day) ? dayTop[day].first : std::string()},
        });
    }
    return points;
}

int MoodTracker::streakDays()
{
    sql::Stmt stmt(db_.getHandle(),
                   "SELECT DISTINCT date(created_at) FROM moods ORDER BY date(created_at) DESC LIMIT 400");
    if (!stmt) return 0;

    std::vector<std::string> days;
    while (stmt.step()) days.push_back(stmt.text(0));
    if (days.empty()) return 0;

    const std::string today = todayIso();

    // Build the list of expected calendar days walking backwards from today.
    // A streak stays alive if the newest entry is today or yesterday.
    const std::time_t now = std::time(nullptr);
    auto dayAt = [&](int offset) {
        const std::time_t stamp = now - static_cast<std::time_t>(offset) * 86400;
        std::tm parts{};
#if defined(_WIN32)
        localtime_s(&parts, &stamp);
#else
        localtime_r(&stamp, &parts);
#endif
        std::ostringstream oss;
        oss << std::put_time(&parts, "%Y-%m-%d");
        return oss.str();
    };

    int start = 0;
    if (days.front() != today) {
        if (days.front() != dayAt(1)) return 0; // last check-in is older than yesterday
        start = 1;
    }

    int streak = 0;
    size_t cursor = 0;
    for (int offset = start; offset < 400 && cursor < days.size(); ++offset) {
        if (days[cursor] == dayAt(offset)) {
            streak++;
            cursor++;
        } else {
            break;
        }
    }
    return streak;
}

int MoodTracker::countSince(const std::string &table, int days)
{
    const std::string query = days > 0
        ? "SELECT COUNT(*) FROM " + table + " WHERE created_at >= datetime('now', 'localtime', ?)"
        : "SELECT COUNT(*) FROM " + table;
    sql::Stmt stmt(db_.getHandle(), query.c_str());
    if (!stmt) return 0;
    if (days > 0) stmt.bind(1, "-" + std::to_string(days) + " days");
    return stmt.step() ? stmt.integer(0) : 0;
}

json MoodTracker::summary(int days)
{
    const int windowDays = (days <= 0) ? 14 : std::min(days, 365);

    const json latestEntry = latest();

    std::lock_guard<std::mutex> guard(db_.mutex());

    // Wellbeing score: mean of the per-entry scores inside the window. Falls
    // back to the all-time mean when the window is empty, so a user returning
    // after a break still sees their history instead of a bare zero.
    auto averageScore = [&](int scopeDays) -> std::pair<int, int> {
        const std::string query = scopeDays > 0
            ? "SELECT mood, level FROM moods WHERE created_at >= datetime('now', 'localtime', ?)"
            : "SELECT mood, level FROM moods";
        sql::Stmt stmt(db_.getHandle(), query.c_str());
        if (!stmt) return {0, 0};
        if (scopeDays > 0) stmt.bind(1, "-" + std::to_string(scopeDays) + " days");

        double total = 0;
        int count = 0;
        while (stmt.step()) {
            total += wellbeingScore(stmt.text(0, "Neutral"), stmt.integer(1, 5));
            count++;
        }
        return {count == 0 ? 0 : static_cast<int>(std::lround(total / count)), count};
    };

    auto windowed = averageScore(windowDays);
    bool usedFallback = false;
    if (windowed.second == 0) {
        const auto allTime = averageScore(0);
        if (allTime.second > 0) {
            windowed = allTime;
            usedFallback = true;
        }
    }

    std::string frequent;
    {
        sql::Stmt stmt(db_.getHandle(),
                       "SELECT mood, COUNT(*) AS c FROM moods "
                       "WHERE created_at >= datetime('now', 'localtime', ?) "
                       "GROUP BY mood ORDER BY c DESC, mood ASC LIMIT 1");
        if (stmt) {
            stmt.bind(1, "-" + std::to_string(windowDays) + " days");
            if (stmt.step()) frequent = stmt.text(0);
        }
        if (frequent.empty()) {
            sql::Stmt allTime(db_.getHandle(),
                              "SELECT mood, COUNT(*) AS c FROM moods GROUP BY mood ORDER BY c DESC, mood ASC LIMIT 1");
            if (allTime && allTime.step()) frequent = allTime.text(0);
        }
    }

    int breathingCycles = 0;
    {
        sql::Stmt stmt(db_.getHandle(), "SELECT COALESCE(SUM(cycles), 0) FROM breathing_sessions");
        if (stmt && stmt.step()) breathingCycles = stmt.integer(0);
    }

    return json{
        {"windowDays", windowDays},
        {"score", windowed.first},
        {"scoreSampleSize", windowed.second},
        {"scoreFromAllTime", usedFallback},
        {"latest", latestEntry},
        {"frequentMood", frequent},
        {"streakDays", streakDays()},
        {"moodCount", countSince("moods", 0)},
        {"moodCountInWindow", countSince("moods", windowDays)},
        {"journalCount", countSince("journals", 0)},
        {"breathingSessions", countSince("breathing_sessions", 0)},
        {"breathingCycles", breathingCycles},
        {"distribution", distribution(windowDays)},
        {"trend", trend(std::min(windowDays, 14))},
    };
}

json MoodTracker::crisisStatus()
{
    const std::string contact = db_.getEmergencyContact();

    std::lock_guard<std::mutex> guard(db_.mutex());
    sql::Stmt stmt(db_.getHandle(),
                   "SELECT mood, level FROM moods ORDER BY datetime(created_at) DESC, id DESC LIMIT 3");

    int considered = 0;
    int lowCount = 0;
    int intenseLow = 0;
    if (stmt) {
        while (stmt.step()) {
            const std::string mood = normalise(stmt.text(0));
            const int level = stmt.integer(1, 5);
            considered++;
            if (isLowMood(mood)) {
                lowCount++;
                if (level >= 7) intenseLow++;
            }
        }
    }

    // Three low check-ins in a row is the trigger; the "severe" flag (all three
    // intense) is what the UI uses to lead with the emergency contact.
    const bool crisis = (considered == 3 && lowCount == 3);
    const bool severe = crisis && intenseLow >= 2;

    std::string message;
    if (severe) {
        message = "Your last three check-ins have all been difficult and intense. "
                  "Please reach out to someone you trust, or call " + contact + " now.";
    } else if (crisis) {
        message = "That is three hard check-ins in a row. You do not have to sit with this alone — "
                  "talking to someone helps, and " + contact + " is there if you need it.";
    } else {
        message = "No distress pattern in your recent check-ins.";
    }

    return json{
        {"crisis", crisis},
        {"severe", severe},
        {"considered", considered},
        {"lowCount", lowCount},
        {"contact", contact},
        {"message", message},
    };
}

json MoodTracker::suggestion()
{
    const json crisis = crisisStatus();
    const json latestEntry = latest();
    const std::string mood = latestEntry.value("hasMood", false)
        ? normalise(latestEntry.value("mood", std::string("Neutral")))
        : std::string("Neutral");

    if (crisis.value("crisis", false)) {
        return json{
            {"mood", mood},
            {"urgent", true},
            {"message", crisis.value("message", std::string())},
            {"contact", crisis.value("contact", std::string("112"))},
        };
    }

    json library;
    if (!loadDataFile("suggestion.json", library) || !library.is_object()) {
        return json{
            {"mood", mood},
            {"urgent", false},
            {"message", "Take one slow breath and notice one thing you can see right now."},
            {"warning", "suggestion.json could not be read; using a built-in fallback."},
        };
    }

    std::string message = "Take care of yourself today.";
    if (library.contains(mood) && library[mood].is_array() && !library[mood].empty()) {
        message = library[mood][pickIndex(library[mood].size())].get<std::string>();
    } else if (library.contains("Neutral") && library["Neutral"].is_array() && !library["Neutral"].empty()) {
        message = library["Neutral"][pickIndex(library["Neutral"].size())].get<std::string>();
    }

    return json{
        {"mood", mood},
        {"urgent", false},
        {"hasMood", latestEntry.value("hasMood", false)},
        {"message", message},
    };
}

json MoodTracker::eqResources()
{
    const json latestEntry = latest();
    const bool hasMood = latestEntry.value("hasMood", false);
    const std::string displayMood = hasMood ? latestEntry.value("mood", std::string("Neutral")) : "Neutral";
    const std::string moodKey = normalise(displayMood);

    json result{
        {"hasMood", hasMood},
        {"displayMood", displayMood},
        {"latestMood", moodKey},
        {"level", hasMood ? latestEntry.value("level", 5) : 0},
        {"date", hasMood ? latestEntry.value("date", std::string()) : std::string()},
    };

    json library;
    if (!loadDataFile("eq_resources.json", library) || !library.is_object()) {
        result["error"] = "The EQ resource library could not be read.";
        result["resources"] = json{
            {"articles", json::array()},
            {"videos", json::array()},
            {"activities", json::array()},
            {"songs", json::array()},
        };
        return result;
    }

    if (library.contains(moodKey) && library[moodKey].is_object()) {
        result["resources"] = library[moodKey];
    } else {
        result["resources"] = library.value("Neutral", json::object());
    }
    return result;
}
