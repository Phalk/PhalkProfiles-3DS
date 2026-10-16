#include "play_data.hpp"

#include <3ds/services/ptmplays.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

namespace {

using nlohmann::json;
const char* const kDatabasePath = "sdmc:/3ds/PhalkProfiles/play_data_db.json";
const char* const kLegacyDatabasePath = "sdmc:/3ds/PhalkProfiles/ptm_test_db.json";
const char* const kTemporaryPath = "sdmc:/3ds/PhalkProfiles/play_data_db.tmp";
const size_t kReadChunk = 256;

struct PtmSnapshot {
    std::vector<PtmPlayEvent> events;
    s32 startIndex;
    s32 endIndex;
    s32 size;
    s32 lastIndex;
    u64 lastSignature;
};

struct PtmSession {
    u64 id;
    u32 startMinutes;
    u32 endMinutes;
    u32 durationSeconds;
};

u64 eventSignature(const PtmPlayEvent& event) {
    u64 value = ptmGetPlayEventTitleId(event);
    value ^= static_cast<u64>(event.minutesSince2000) << 17;
    value ^= static_cast<u64>(event.type) << 59;
    value ^= value >> 29;
    value *= 0x9E3779B185EBCA87ull;
    value ^= value >> 32;
    return value;
}

s32 advanceIndex(s32 index, s32 amount) {
    const s64 capacity = PTM_MAX_PLAY_EVENTS;
    s64 result = (static_cast<s64>(index) + amount) % capacity;
    if (result < 0) result += capacity;
    return static_cast<s32>(result);
}

s32 distanceFrom(s32 start, s32 index) {
    return index >= start ? index - start : index + static_cast<s32>(PTM_MAX_PLAY_EVENTS) - start;
}

bool readSnapshot(PtmSnapshot& snapshot, std::string& error) {
    snapshot.events.clear();
    Result rc = ptmPlaysInit();
    if (R_FAILED(rc)) {
        error = "PTM init failed (";
        char code[16]; std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(rc));
        error += code; error += ")";
        return false;
    }

    rc = PTMPLAYS_GetPlayHistoryStart(&snapshot.startIndex);
    if (R_SUCCEEDED(rc)) rc = PTMPLAYS_GetPlayHistorySize(&snapshot.size);
    if (R_FAILED(rc) || snapshot.startIndex < 0 || snapshot.startIndex >= static_cast<s32>(PTM_MAX_PLAY_EVENTS) ||
        snapshot.size < 0 || snapshot.size > static_cast<s32>(PTM_MAX_PLAY_EVENTS)) {
        ptmPlaysExit();
        error = "Could not read PTM history header";
        return false;
    }

    snapshot.events.reserve(static_cast<size_t>(snapshot.size));
    s32 currentIndex = snapshot.startIndex;
    s32 remaining = snapshot.size;
    while (remaining > 0) {
        const s32 count = std::min<s32>(remaining, static_cast<s32>(kReadChunk));
        PtmPlayEvent chunk[kReadChunk];
        s32 reportedEnd = -1;
        rc = PTMPLAYS_GetPlayHistory(&reportedEnd, chunk, currentIndex, count);
        if (R_FAILED(rc)) {
            ptmPlaysExit();
            error = "Could not read PTM events";
            return false;
        }
        snapshot.events.insert(snapshot.events.end(), chunk, chunk + count);
        currentIndex = advanceIndex(currentIndex, count);
        remaining -= count;
    }
    ptmPlaysExit();

    snapshot.endIndex = currentIndex;
    snapshot.lastIndex = snapshot.size == 0 ? -1 : advanceIndex(snapshot.endIndex, -1);
    snapshot.lastSignature = snapshot.size == 0 ? 0 : eventSignature(snapshot.events.back());
    return true;
}

std::vector<PtmSession> makeSessions(const std::vector<PtmPlayEvent>& events, u32& incomplete) {
    std::vector<PtmSession> sessions;
    std::map<u64, u32> active;
    incomplete = 0;
    for (const PtmPlayEvent& event : events) {
        const u64 id = ptmGetPlayEventTitleId(event);
        // PTM event 10 is the global shutdown marker and uses an invalid Title ID.
        // Close every still-running application here so shutdowns do not leave
        // false incomplete sessions.
        if (id == PTM_INVALID_TITLE_ID && event.type == 10) {
            for (const auto& item : active) {
                const u32 start = item.second;
                if (event.minutesSince2000 <= start) {
                    ++incomplete;
                    continue;
                }
                const u32 seconds = (event.minutesSince2000 - start) * 60;
                sessions.push_back({item.first, start, event.minutesSince2000, seconds});
            }
            active.clear();
            continue;
        }
        if (id == 0 || id == PTM_INVALID_TITLE_ID) continue;
        if (event.type == PTMPLAYEVENT_APPLICATION_LAUNCH ||
            event.type == PTMPLAYEVENT_JUMP_TO_APPLICATION) {
            if (active.find(id) != active.end()) ++incomplete;
            active[id] = event.minutesSince2000;
        } else if (event.type == PTMPLAYEVENT_APPLICATION_EXIT ||
                   event.type == PTMPLAYEVENT_LEAVE_APPLICATION) {
            const std::map<u64, u32>::iterator found = active.find(id);
            if (found == active.end()) continue;
            const u32 start = found->second;
            active.erase(found);
            if (event.minutesSince2000 <= start) { ++incomplete; continue; }
            const u32 seconds = (event.minutesSince2000 - start) * 60;
            sessions.push_back({id, start, event.minutesSince2000, seconds});
        }
    }
    incomplete += static_cast<u32>(active.size());
    return sessions;
}

u16 dayForMinute(u32 minutes) {
    const u32 days = minutes / 1440;
    return days >= 0xFFFFu ? 0xFFFFu : static_cast<u16>(days);
}

u32 sessionEndSeconds(const PtmSession& session) {
    return session.endMinutes * 60;
}

PlayDataTitle fromPld(const ActivityTitle& title) {
    return {title.id, title.playtimeSeconds, title.launches, title.firstPlayedDays, title.lastPlayedDays,
            title.latestSessionSeconds};
}

void applySession(std::map<u64, PlayDataTitle>& titles, const PtmSession& session) {
    PlayDataTitle& title = titles[session.id];
    if (title.id == 0) title.id = session.id;
    const u16 first = dayForMinute(session.startMinutes);
    const u16 last = dayForMinute(session.endMinutes);
    if (title.firstPlayedDays == 0 || title.firstPlayedDays == 0xFFFFu ||
        (first != 0xFFFFu && first < title.firstPlayedDays)) title.firstPlayedDays = first;
    if (title.lastPlayedDays == 0 || title.lastPlayedDays == 0xFFFFu ||
        (last != 0xFFFFu && last > title.lastPlayedDays)) title.lastPlayedDays = last;
    title.playtimeSeconds += session.durationSeconds;
    ++title.launches;
    title.latestSessionSeconds = std::max(title.latestSessionSeconds, sessionEndSeconds(session));
}

json recordJson(const PlayDataTitle& title) {
    char id[17];
    std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(title.id));
    return json{{"id", id}, {"playtime", title.playtimeSeconds}, {"launches", title.launches},
                {"first", title.firstPlayedDays}, {"last", title.lastPlayedDays},
                {"latest", title.latestSessionSeconds}};
}

json recordsJson(const std::map<u64, PlayDataTitle>& records) {
    json output = json::array();
    for (const auto& item : records) output.push_back(recordJson(item.second));
    return output;
}

bool parseId(const json& value, u64& id) {
    if (!value.is_string()) return false;
    const std::string text = value.get<std::string>();
    char* end = NULL;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 16);
    if (end == text.c_str() || *end != '\0') return false;
    id = static_cast<u64>(parsed);
    return true;
}

bool readRecords(const json& value, std::map<u64, PlayDataTitle>& records,
                 bool& launchesMissing) {
    if (!value.is_array()) return false;
    records.clear();
    for (json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (!it->is_object() || !it->contains("id") || !it->contains("playtime") ||
            !it->contains("first") || !it->contains("last") || !it->contains("latest")) return false;
        u64 id = 0;
        if (!parseId((*it)["id"], id)) return false;
        if (!it->contains("launches")) launchesMissing = true;
        PlayDataTitle title = {id, (*it)["playtime"].get<u32>(),
                              it->contains("launches") ? (*it)["launches"].get<u32>() : 0,
                              (*it)["first"].get<u16>(),
                              (*it)["last"].get<u16>(), (*it)["latest"].get<u32>()};
        records[id] = title;
    }
    return true;
}

u64 jsonU64(const json& value) {
    if (value.is_string()) return static_cast<u64>(std::strtoull(value.get<std::string>().c_str(), NULL, 10));
    return value.get<u64>();
}

bool cursorIsRetained(const PlayDataDatabase& database, const PtmSnapshot& snapshot, s32& offset) {
    if (database.lastEventIndex < 0) {
        offset = distanceFrom(snapshot.startIndex, database.nextEventIndex);
        return offset <= snapshot.size;
    }
    const s32 lastOffset = distanceFrom(snapshot.startIndex, database.lastEventIndex);
    if (lastOffset >= snapshot.size) return false;
    if (eventSignature(snapshot.events[static_cast<size_t>(lastOffset)]) != database.lastEventSignature)
        return false;
    offset = distanceFrom(snapshot.startIndex, database.nextEventIndex);
    return offset <= snapshot.size;
}

void updateCursor(PlayDataDatabase& database, const PtmSnapshot& snapshot) {
    database.nextEventIndex = snapshot.endIndex;
    database.lastEventIndex = snapshot.lastIndex;
    database.lastEventSignature = snapshot.lastSignature;
}

bool newEvents(const PlayDataDatabase& database, const PtmSnapshot& snapshot,
               std::vector<PtmPlayEvent>& events, std::string& error) {
    s32 firstOffset = 0;
    if (!cursorIsRetained(database, snapshot, firstOffset)) {
        error = "PTM history wrapped. Open Activity Log, then reset the local database.";
        return false;
    }
    const s32 count = distanceFrom(database.nextEventIndex, snapshot.endIndex);
    if (count == 0) return true;
    if (firstOffset > snapshot.size || count > snapshot.size - firstOffset) {
        error = "PTM cursor is no longer available. Open Activity Log, then reset the local database.";
        return false;
    }
    events.assign(snapshot.events.begin() + firstOffset,
                  snapshot.events.begin() + firstOffset + count);
    return true;
}

} // namespace

void initializePlayDataDatabase(PlayDataDatabase& database) {
    database = {false, {}, 0, -1, 0};
}

bool loadPlayDataDatabase(PlayDataDatabase& database, std::string& error) {
    initializePlayDataDatabase(database);
    std::ifstream file(kDatabasePath, std::ios::binary);
    bool migratingLegacyDatabase = false;
    if (!file) {
        file.clear();
        file.open(kLegacyDatabasePath, std::ios::binary);
        if (!file) return true;
        migratingLegacyDatabase = true;
    }
    const json value = json::parse(file, nullptr, false);
    if (value.is_discarded() || !value.is_object() || !value.contains("format") ||
        !value.contains("titles") || !value.contains("ptm")) {
        error = "Local play data database is invalid";
        return false;
    }
    const int format = value["format"].get<int>();
    if ((format != 1 && format != 2) || (format == 1 && !value.contains("baseline"))) {
        error = "Local play data database format is unsupported";
        return false;
    }
    bool missingLaunchCounts = false;
    if (!readRecords(value["titles"], database.titles, missingLaunchCounts)) {
        error = "Local play data database records are invalid";
        return false;
    }
    if (format == 1) {
        std::map<u64, PlayDataTitle> ignoredBaseline;
        if (!readRecords(value["baseline"], ignoredBaseline, missingLaunchCounts)) {
            error = "Legacy local play data baseline is invalid";
            return false;
        }
    }
    const json& cursor = value["ptm"];
    database.nextEventIndex = cursor.value("next", 0);
    database.lastEventIndex = cursor.value("lastIndex", -1);
    database.lastEventSignature = cursor.contains("signature") ? jsonU64(cursor["signature"]) : 0;
    database.initialized = value.value("initialized", false);
    if (migratingLegacyDatabase || format == 1) {
        if (missingLaunchCounts) {
            std::vector<ActivityTitle> currentPld;
            std::string ignoredError;
            if (readActivityLog(currentPld, 0, ignoredError)) {
                std::map<u64, u32> launchCounts;
                for (const ActivityTitle& title : currentPld) launchCounts[title.id] = title.launches;
                for (auto& item : database.titles) {
                    const auto found = launchCounts.find(item.first);
                    if (found != launchCounts.end()) item.second.launches = found->second;
                }
            }
        }
        if (!savePlayDataDatabase(database, error)) return false;
    }
    return true;
}

bool savePlayDataDatabase(const PlayDataDatabase& database, std::string& error) {
    json value = {
        {"format", 2}, {"initialized", database.initialized},
        {"titles", recordsJson(database.titles)},
        {"ptm", {{"next", database.nextEventIndex}, {"lastIndex", database.lastEventIndex},
                  {"signature", std::to_string(database.lastEventSignature)}}}
    };
    std::ofstream file(kTemporaryPath, std::ios::binary | std::ios::trunc);
    if (!file) { error = "Could not create local play data database"; return false; }
    file << value.dump(2) << '\n';
    file.flush();
    bool okay = file.good();
    file.close();
    okay = okay && !file.fail();
    if (!okay) { std::remove(kTemporaryPath); error = "Could not write local play data database"; return false; }
    std::remove(kDatabasePath);
    if (std::rename(kTemporaryPath, kDatabasePath) != 0) {
        std::remove(kTemporaryPath);
        error = "Could not replace local play data database";
        return false;
    }
    return true;
}

bool resetPlayDataDatabase(std::string& error) {
    if (std::remove(kDatabasePath) != 0 && errno != ENOENT) {
        error = "Could not remove local play data database";
        return false;
    }
    std::remove(kTemporaryPath);
    std::remove(kLegacyDatabasePath);
    return true;
}

bool initializePlayDataFromPld(PlayDataDatabase& database, const std::vector<ActivityTitle>& pld,
                            std::string& error) {
    PtmSnapshot snapshot;
    if (!readSnapshot(snapshot, error)) return false;

    PlayDataDatabase result;
    initializePlayDataDatabase(result);
    for (const ActivityTitle& title : pld) {
        result.titles[title.id] = fromPld(title);
    }

    // Seed once from PLD and checkpoint the PTM tail. Subsequent uploads only
    // consume events that arrive after this point.
    result.initialized = true;
    updateCursor(result, snapshot);
    if (!savePlayDataDatabase(result, error)) return false;
    database = result;
    return true;
}

bool syncPlayDataFromPtm(PlayDataDatabase& database, std::string& error) {
    if (!database.initialized) { error = "Initialize the local database from PLD first"; return false; }
    PtmSnapshot snapshot;
    if (!readSnapshot(snapshot, error)) return false;
    std::vector<PtmPlayEvent> events;
    if (!newEvents(database, snapshot, events, error)) return false;
    u32 incomplete = 0;
    const std::vector<PtmSession> sessions = makeSessions(events, incomplete);
    PlayDataDatabase result = database;
    for (const PtmSession& session : sessions) {
        applySession(result.titles, session);
    }
    updateCursor(result, snapshot);
    if (!savePlayDataDatabase(result, error)) return false;
    database = result;
    return true;
}
