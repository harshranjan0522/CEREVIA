#ifndef CEREVIA_MOODTRACKER_H
#define CEREVIA_MOODTRACKER_H

#include "Database.h"
#include "libs/json/json.hpp"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Everything that reads or writes mood check-ins, plus the analytics the
// dashboard renders. All methods return nlohmann::json so the API layer only
// has to serialise.
// ---------------------------------------------------------------------------
class MoodTracker {
public:
    explicit MoodTracker(Database &db);

    // Canonical mood vocabulary shared with the frontend.
    static const std::vector<std::string> &vocabulary();
    static bool isKnownMood(const std::string &mood);
    static std::string normalise(const std::string &mood);      // unknown -> "Neutral"
    static std::string tryNormalise(const std::string &mood);   // unknown -> ""
    static double valence(const std::string &mood);       // -1.0 .. +1.0
    static int wellbeingScore(const std::string &mood, int level); // 0 .. 100

    // ---- Writes -----------------------------------------------------------
    // Returns the stored entry, or an object with "error" when the payload is
    // not usable.
    nlohmann::json add(const nlohmann::json &payload);
    bool remove(int id);
    int clear();

    // ---- Reads ------------------------------------------------------------
    nlohmann::json list(int days, int limit);
    nlohmann::json latest();
    nlohmann::json summary(int days);
    nlohmann::json suggestion();
    nlohmann::json eqResources();
    nlohmann::json crisisStatus();

private:
    nlohmann::json distribution(int days);
    nlohmann::json trend(int days);
    int streakDays();
    int countSince(const std::string &table, int days);

    Database &db_;
};

#endif // CEREVIA_MOODTRACKER_H
