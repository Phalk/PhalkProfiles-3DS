#include "activity_log.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

namespace {

// Activity Log system save IDs in USA, EUR, JPN and KOR respectively.
const u32 kSaveIds[] = {0x00020212, 0x00020222, 0x00020202, 0x00020272};
const u32 kSessionOffset = 0x10;
const size_t kSessionCount = 50000;
const u32 kSummaryOffset = 0xC3510;
const size_t kSummaryCount = 256;
const size_t kSessionChunkCount = 256;
const char* const kTitleDatabasePath = "romfs:/3ds_db.json";

struct PldSession {
    u64 id;
    u32 timestamp;
    u32 playSeconds;
};
static_assert(sizeof(PldSession) == 16, "Unexpected pld.dat session layout");

struct PldSummary {
    u64 id;
    u32 totalSeconds;
    u16 launchCount;
    u16 reserved;
    u16 firstDays;
    u16 lastDays;
    u32 reserved2;
};
static_assert(sizeof(PldSummary) == 24, "Unexpected pld.dat summary layout");

std::map<u64, std::string> sNames;
bool sTitleDatabaseLoaded = false;

void loadBundledTitleDatabase() {
    if (sTitleDatabaseLoaded) return;
    sTitleDatabaseLoaded = true;

    std::ifstream file(kTitleDatabasePath, std::ios::binary);
    if (!file) return;

    const nlohmann::json database = nlohmann::json::parse(file, nullptr, false);
    if (database.is_discarded() || !database.is_array()) return;

    for (nlohmann::json::const_iterator it = database.begin(); it != database.end(); ++it) {
        if (!it->is_object() || !it->contains("TitleID") || !it->contains("Name") ||
            !(*it)["TitleID"].is_string() || !(*it)["Name"].is_string()) continue;

        const std::string titleId = (*it)["TitleID"].get<std::string>();
        const std::string name = (*it)["Name"].get<std::string>();
        if (titleId.empty() || name.empty()) continue;

        char* end = NULL;
        const unsigned long long parsedId = std::strtoull(titleId.c_str(), &end, 16);
        if (end == titleId.c_str() || *end != '\0') continue;
        sNames[static_cast<u64>(parsedId)] = name;
    }
}

std::string hexCode(Result code) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(code));
    return text;
}

// A successful archive open alone does not guarantee the save contains pld.dat.
bool readSummaries(PldSummary* summaries, u16* latestSessionDays,
                   u16* latestFutureSessionDays, u32 todayDays, Result& lastError) {
    for (size_t i = 0; i < sizeof(kSaveIds) / sizeof(kSaveIds[0]); ++i) {
        const u32 pathData[2] = {MEDIATYPE_NAND, kSaveIds[i]};
        const FS_Path archivePath = {PATH_BINARY, sizeof(pathData), pathData};
        FS_Archive archive;
        Result rc = FSUSER_OpenArchive(&archive, ARCHIVE_SYSTEM_SAVEDATA, archivePath);
        if (R_FAILED(rc)) { lastError = rc; continue; }

        Handle file = 0;
        rc = FSUSER_OpenFile(&file, archive, fsMakePath(PATH_ASCII, "/pld.dat"), FS_OPEN_READ, 0);
        if (R_SUCCEEDED(rc)) {
            u64 size = 0;
            rc = FSFILE_GetSize(file, &size);
            if (R_SUCCEEDED(rc) && size < kSummaryOffset + sizeof(PldSummary) * kSummaryCount)
                rc = static_cast<Result>(-1);
            if (R_SUCCEEDED(rc)) {
                u32 bytesRead = 0;
                rc = FSFILE_Read(file, &bytesRead, kSummaryOffset, summaries,
                                 sizeof(PldSummary) * kSummaryCount);
                if (R_SUCCEEDED(rc) && bytesRead != sizeof(PldSummary) * kSummaryCount)
                    rc = static_cast<Result>(-1);
            }
            if (R_SUCCEEDED(rc)) {
                std::map<u64, size_t> summaryById;
                for (size_t j = 0; j < kSummaryCount; ++j) {
                    latestSessionDays[j] = 0;
                    latestFutureSessionDays[j] = 0;
                    if (summaries[j].id != 0 && summaries[j].id != ~u64(0))
                        summaryById[summaries[j].id] = j;
                }

                PldSession chunk[kSessionChunkCount];
                for (size_t start = 0; start < kSessionCount && R_SUCCEEDED(rc);
                     start += kSessionChunkCount) {
                    const size_t count = std::min(kSessionChunkCount, kSessionCount - start);
                    const u32 offset = kSessionOffset + static_cast<u32>(start * sizeof(PldSession));
                    const u32 bytes = static_cast<u32>(count * sizeof(PldSession));
                    u32 bytesRead = 0;
                    rc = FSFILE_Read(file, &bytesRead, offset, chunk, bytes);
                    if (R_SUCCEEDED(rc) && bytesRead != bytes) rc = static_cast<Result>(-1);
                    if (R_FAILED(rc)) break;

                    for (size_t j = 0; j < count; ++j) {
                        const PldSession& session = chunk[j];
                        if (session.id == 0 || session.id == ~u64(0) ||
                            session.timestamp == 0 || session.timestamp % 3600 != 0 ||
                            session.playSeconds > 3600) continue;
                        const std::map<u64, size_t>::const_iterator found = summaryById.find(session.id);
                        if (found == summaryById.end()) continue;
                        const u32 days = session.timestamp / 86400;
                        if (days == 0 || days >= 0xFFFFu) continue;
                        u16* latest = days <= todayDays + 1
                            ? &latestSessionDays[found->second]
                            : &latestFutureSessionDays[found->second];
                        if (days > *latest) *latest = static_cast<u16>(days);
                    }
                }
            }
            FSFILE_Close(file);
        }
        FSUSER_CloseArchive(archive);
        if (R_SUCCEEDED(rc)) return true;
        lastError = rc;
    }
    return false;
}

} // namespace

bool readActivityLog(std::vector<ActivityTitle>& titles, u32 todayDays, std::string& error) {
    titles.clear();
    PldSummary records[kSummaryCount];
    u16 latestSessionDays[kSummaryCount] = {};
    u16 latestFutureSessionDays[kSummaryCount] = {};
    Result rc = static_cast<Result>(-1);
    if (!readSummaries(records, latestSessionDays, latestFutureSessionDays, todayDays, rc)) {
        error = "Could not read pld.dat (" + hexCode(rc) + "). Check CFW/Activity Log";
        return false;
    }

    for (size_t i = 0; i < kSummaryCount; ++i) {
        const PldSummary& s = records[i];
        if (s.id == 0 || s.id == ~u64(0)) continue;
        if (s.totalSeconds == 0xFFFFFFFFu || (s.totalSeconds == 0 && s.launchCount == 0))
            continue;
        ActivityTitle entry = {s.id, s.totalSeconds, s.launchCount, s.firstDays, s.lastDays,
                               latestSessionDays[i], latestFutureSessionDays[i]};
        titles.push_back(entry);
    }
    if (titles.empty()) {
        error = "No valid titles in Activity Log";
        return false;
    }
    return true;
}

std::string activityTitleName(u64 id) {
    loadBundledTitleDatabase();
    const std::map<u64, std::string>::const_iterator found = sNames.find(id);
    return found == sNames.end() ? "" : found->second;
}

u64 activityUnixTime(u16 days) {
    if (days == 0 || days == 0xFFFFu) return 0;
    return 946684800ULL + static_cast<u64>(days) * 86400ULL;
}
