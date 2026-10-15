#include <3ds.h>
#include "activity_log.hpp"
#include <nlohmann/json.hpp>

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using nlohmann::json;

namespace {

const char* const kDirectory = "sdmc:/3ds/PhalkProfiles";
const char* const kConfig = "sdmc:/3ds/PhalkProfiles/config.txt";
const char* const kExport = "sdmc:/3ds/PhalkProfiles/export.json";
const char* const kDateCorrections = "sdmc:/3ds/PhalkProfiles/date_corrections.txt";
const char* const kEndpoint = "http://www.phalk.net/profiles/json_curl.php";

void ensureDirectory();

void presentFrame() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

struct Config {
    std::string username;
    std::string password;
};

struct DateCorrection {
    u16 originalFirst;
    u16 originalLast;
    u16 correctedFirst;
    u16 correctedLast;
};

struct EditableDate {
    int year;
    int month;
    int day;
    bool known;
};

bool leapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int daysInMonth(int year, int month) {
    static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return lengths[month - 1] + (month == 2 && leapYear(year) ? 1 : 0);
}

u32 activityDayNumber(int year, int month, int day) {
    u32 days = 0;
    for (int y = 2000; y < year; ++y) days += 365 + leapYear(y);
    for (int m = 1; m < month; ++m) days += daysInMonth(year, m);
    return days + day - 1;
}

std::string calendarDate(int year, int month, int day) {
    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", year, month, day);
    return text;
}

EditableDate editableDate(u16 days) {
    if (days == 0 || days == 0xFFFFu) return {0, 0, 0, false};
    u32 remaining = days;
    int year = 2000;
    while (remaining >= static_cast<u32>(365 + leapYear(year)))
        remaining -= 365 + leapYear(year++);
    int month = 1;
    while (remaining >= static_cast<u32>(daysInMonth(year, month)))
        remaining -= daysInMonth(year, month++);
    return {year, month, static_cast<int>(remaining) + 1, true};
}

bool currentActivityDay(u32& days, std::string& today) {
    const std::time_t now = std::time(NULL);
    if (now == static_cast<std::time_t>(-1)) return false;
    const std::tm* local = std::localtime(&now);
    if (!local) return false;
    const int year = local->tm_year + 1900;
    const int month = local->tm_mon + 1;
    if (year < 2000 || year > 2100 || month < 1 || month > 12 ||
        local->tm_mday < 1 || local->tm_mday > daysInMonth(year, month)) return false;
    days = activityDayNumber(year, month, local->tm_mday);
    today = calendarDate(year, month, local->tm_mday);
    return true;
}

bool futureDate(u16 days, u32 today) {
    return days != 0 && days != 0xFFFFu && days > today;
}

void changeDateField(EditableDate& date, int field, int direction) {
    if (field == 0) date.year = std::max(2000, std::min(2179, date.year + direction));
    else if (field == 1) date.month = (date.month - 1 + direction + 12) % 12 + 1;
    else {
        const int length = daysInMonth(date.year, date.month);
        date.day = (date.day - 1 + direction + length) % length + 1;
    }
    date.day = std::min(date.day, daysInMonth(date.year, date.month));
}

void printEditableDate(const char* label, const EditableDate& date, int firstField,
                       int selected, u32 today) {
    if (!date.known) {
        std::printf("%s: unknown\n", label);
        return;
    }
    char year[12], month[8], day[8];
    std::snprintf(year, sizeof(year), selected == firstField ? "[%04d]" : "%04d", date.year);
    std::snprintf(month, sizeof(month), selected == firstField + 1 ? "[%02d]" : "%02d", date.month);
    std::snprintf(day, sizeof(day), selected == firstField + 2 ? "[%02d]" : "%02d", date.day);
    std::printf("%s: %s-%s-%s%s\n", label, year, month, day,
                activityDayNumber(date.year, date.month, date.day) > today ? "  FUTURE" : "");
}

void applyDateCorrections(std::vector<ActivityTitle>& records, u32 today) {
    std::map<u64, std::vector<DateCorrection> > saved;
    std::ifstream file(kDateCorrections);
    std::string line;
    while (std::getline(file, line)) {
        // Recheck dates saved by the older automatic and keyboard-based editors.
        if (line.size() < 3 || line[0] != 'E' || line[1] != ' ') continue;
        unsigned long long id = 0;
        unsigned int originalFirst = 0, originalLast = 0, correctedFirst = 0, correctedLast = 0;
        char extra = 0;
        if (std::sscanf(line.c_str() + 2, "%16llx %u %u %u %u %c", &id, &originalFirst,
                        &originalLast, &correctedFirst, &correctedLast, &extra) != 5 ||
            originalFirst > 0xFFFFu || originalLast > 0xFFFFu ||
            correctedFirst > 0xFFFFu || correctedLast > 0xFFFFu) continue;
        saved[static_cast<u64>(id)].push_back({static_cast<u16>(originalFirst),
                                                static_cast<u16>(originalLast),
                                                static_cast<u16>(correctedFirst),
                                                static_cast<u16>(correctedLast)});
    }
    for (ActivityTitle& entry : records) {
        const std::map<u64, std::vector<DateCorrection> >::const_iterator found = saved.find(entry.id);
        const u16 originalFirst = entry.firstPlayedDays;
        const u16 originalLast = entry.lastPlayedDays;
        bool lastCorrectionApplied = false;
        if (found != saved.end()) {
            for (const DateCorrection& correction : found->second) {
                if (originalFirst == correction.originalFirst)
                    entry.firstPlayedDays = correction.correctedFirst;
                if (originalLast == correction.originalLast) {
                    entry.lastPlayedDays = correction.correctedLast;
                    lastCorrectionApplied = true;
                }
            }
        }

        if (entry.latestSessionDays != 0 &&
            ((!lastCorrectionApplied && futureDate(originalLast, today)) ||
             entry.latestSessionDays > entry.lastPlayedDays))
            entry.lastPlayedDays = entry.latestSessionDays;
        else if (entry.latestSessionDays == 0 && entry.latestFutureSessionDays != 0 &&
                 !futureDate(originalLast, today) &&
                 entry.latestFutureSessionDays > entry.lastPlayedDays)
            entry.lastPlayedDays = entry.latestFutureSessionDays;
    }
}

bool saveDateCorrection(const ActivityTitle& original, const ActivityTitle& corrected) {
    ensureDirectory();
    std::ofstream file(kDateCorrections, std::ios::binary | std::ios::app);
    if (!file) return false;
    char line[80];
    std::snprintf(line, sizeof(line), "E %016llX %u %u %u %u\n",
                  static_cast<unsigned long long>(original.id), original.firstPlayedDays,
                  original.lastPlayedDays, corrected.firstPlayedDays, corrected.lastPlayedDays);
    file << '\n' << line;
    file.flush();
    file.close();
    return !file.fail();
}

bool reviewFutureDates(std::vector<ActivityTitle>& records, const std::vector<ActivityTitle>& original,
                       u32 today, const std::string& todayText, std::string& error) {
    size_t affected = 0;
    for (const ActivityTitle& entry : records)
        if (futureDate(entry.firstPlayedDays, today) || futureDate(entry.lastPlayedDays, today)) ++affected;
    if (affected == 0) return true;

    size_t reviewed = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        ActivityTitle& entry = records[i];
        const bool firstFuture = futureDate(entry.firstPlayedDays, today);
        const bool lastFuture = futureDate(entry.lastPlayedDays, today);
        if (!firstFuture && !lastFuture) continue;
        ++reviewed;

        EditableDate first = editableDate(entry.firstPlayedDays);
        EditableDate last = editableDate(entry.lastPlayedDays);
        int selected = firstFuture ? 0 : 3;
        std::string inputError;
        const std::string name = activityTitleName(entry.id);
        char id[17];
        std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(entry.id));
        bool savedFix = false;
        bool dirty = true;
        while (aptMainLoop()) {
            if (dirty) {
                consoleClear();
                std::printf("FUTURE DATE (%u/%u)\n", static_cast<unsigned int>(reviewed),
                            static_cast<unsigned int>(affected));
                std::printf("------------------------------\n\n");
                std::printf("Game: %.32s\n", name.empty() ? "Unknown title" : name.c_str());
                std::printf("ID: %s\n", id);
                std::printf("Today: %s\n\n", todayText.c_str());
                printEditableDate("First", first, 0, selected, today);
                printEditableDate("Last ", last, 3, selected, today);
                std::printf("\nLeft/Right: choose year, month, day\n");
                std::printf("Up/Down: change selected value\n");
                std::printf("A: save   B: cancel export\n");
                if (!inputError.empty()) std::printf("\n%s\n", inputError.c_str());
                dirty = false;
            }
            presentFrame();
            hidScanInput();
            const u32 down = hidKeysDown();
            const u32 repeat = hidKeysDownRepeat();
            if (down & (KEY_B | KEY_START)) {
                error = "Export canceled: future dates remain";
                return false;
            }
            if (repeat & (KEY_LEFT | KEY_RIGHT)) {
                const int step = (repeat & KEY_RIGHT) ? 1 : -1;
                do selected = (selected + step + 6) % 6;
                while (!(selected < 3 ? first.known : last.known));
                inputError.clear();
                dirty = true;
            }
            if (repeat & (KEY_UP | KEY_DOWN)) {
                EditableDate& date = selected < 3 ? first : last;
                changeDateField(date, selected % 3, (repeat & KEY_UP) ? 1 : -1);
                inputError.clear();
                dirty = true;
            }
            if (down & KEY_A) {
                ActivityTitle edited = entry;
                const u32 firstDay = first.known ? activityDayNumber(first.year, first.month, first.day) : entry.firstPlayedDays;
                const u32 lastDay = last.known ? activityDayNumber(last.year, last.month, last.day) : entry.lastPlayedDays;
                if ((first.known && (firstDay == 0 || firstDay >= 0xFFFFu || firstDay > today)) ||
                    (last.known && (lastDay == 0 || lastDay >= 0xFFFFu || lastDay > today))) {
                    inputError = "Fix both future dates before saving";
                    dirty = true;
                    continue;
                }
                if (first.known && last.known && firstDay > lastDay) {
                    inputError = "First date must not follow last";
                    dirty = true;
                    continue;
                }
                edited.firstPlayedDays = static_cast<u16>(firstDay);
                edited.lastPlayedDays = static_cast<u16>(lastDay);
                if (!saveDateCorrection(original[i], edited)) {
                    error = "Could not save date correction";
                    return false;
                }
                entry = edited;
                savedFix = true;
                break;
            }
        }
        if (!savedFix) {
            error = "Export canceled";
            return false;
        }
    }
    for (const ActivityTitle& entry : records) {
        if (futureDate(entry.firstPlayedDays, today) || futureDate(entry.lastPlayedDays, today)) {
            error = "Future dates remain; export blocked";
            return false;
        }
    }
    return true;
}

void ensureDirectory() {
    mkdir("sdmc:/3ds", 0777);
    mkdir(kDirectory, 0777);
}

void trimCR(std::string& value) {
    if (!value.empty() && value[value.size() - 1] == '\r') value.pop_back();
}

Config loadConfig() {
    Config config;
    std::ifstream file(kConfig);
    if (!file) return config;
    std::getline(file, config.username);
    std::getline(file, config.password);
    trimCR(config.username);
    trimCR(config.password);
    if (config.username.compare(0, 3, "\xef\xbb\xbf") == 0) config.username.erase(0, 3);
    return config;
}

bool saveConfig(const Config& config) {
    ensureDirectory();
    std::ofstream file(kConfig, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << config.username << '\n' << config.password << '\n';
    file.flush();
    return file.good();
}

bool prepareExport(const std::vector<ActivityTitle>& records, unsigned int& count, std::string& error) {
    count = 0;
    json titles = json::array();
    for (const ActivityTitle& entry : records) {
        char id[17];
        std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(entry.id));
        std::string name = activityTitleName(entry.id);
        if (name.empty()) name = std::string("Unknown Game (") + id + ")";

        titles.push_back({
            {"id", id},
            {"name", name},
            {"platform", "Nintendo 3DS"},
            {"summary", {
                {"firstPlayed", activityUnixTime(entry.firstPlayedDays)},
                {"lastPlayed", activityUnixTime(entry.lastPlayedDays)},
                {"playtime", entry.playtimeSeconds},
                {"launches", entry.launches}
            }}
        });
    }

    if (titles.empty()) {
        error = "No valid titles in Activity Log";
        return false;
    }
    json user = {{"name", "Nintendo 3DS"}, {"id", "3DS"}, {"titles", titles}};
    json output = {
        {"exportString", "Exported using PhalkProfiles 3DS"},
        {"platform", "Nintendo 3DS"},
        {"source", "PhalkProfiles-3DS"},
        {"exportTimestamp", static_cast<uint64_t>(std::time(NULL))},
        {"users", json::array({user})}
    };

    ensureDirectory();
    std::ofstream file(kExport, std::ios::binary | std::ios::trunc);
    if (!file) { error = "Could not open export.json on SD"; return false; }
    file << output.dump(2) << '\n';
    file.flush();
    if (!file.good()) { error = "Could not save export.json"; return false; }
    count = static_cast<unsigned int>(titles.size());
    return true;
}

bool prepareValidatedExport(unsigned int& count, std::string& error) {
    std::vector<ActivityTitle> original;
    u32 today = 0;
    std::string todayText;
    if (!currentActivityDay(today, todayText)) {
        error = "Check the 3DS date and time";
        return false;
    }
    if (!readActivityLog(original, today, error)) return false;

    std::vector<ActivityTitle> records = original;
    applyDateCorrections(records, today);
    if (!reviewFutureDates(records, original, today, todayText, error)) return false;
    return prepareExport(records, count, error);
}

bool uploadExport(const Config& config, std::string& error) {
    if (config.username.empty() || config.password.empty()) {
        error = "Set username and password first";
        return false;
    }
    std::ifstream file(kExport, std::ios::binary);
    if (!file) { error = "Create the JSON before uploading"; return false; }
    std::ostringstream jsonData;
    jsonData << file.rdbuf();
    if (jsonData.str().empty()) { error = "Exported JSON is empty"; return false; }

    const char* boundary = "PhalkProfiles3DSBoundary42";
    std::string body;
    body.reserve(jsonData.str().size() + 512);
    body += std::string("--") + boundary + "\r\nContent-Disposition: form-data; name=\"user_name\"\r\n\r\n";
    body += config.username + "\r\n--" + boundary;
    body += "\r\nContent-Disposition: form-data; name=\"password\"\r\n\r\n";
    body += config.password + "\r\n--" + boundary;
    body += "\r\nContent-Disposition: form-data; name=\"json_file\"; filename=\"export.json\"\r\n";
    body += "Content-Type: application/json\r\n\r\n";
    body += jsonData.str();
    body += std::string("\r\n--") + boundary + "--\r\n";

    // IPC buffer is 4-byte aligned and owns the bytes until the POST is sent.
    std::vector<u32> aligned((body.size() + sizeof(u32) - 1) / sizeof(u32), 0);
    std::memcpy(aligned.data(), body.data(), body.size());

    // HTTPC needs shared upload memory for POST. Passing zero only works for GET.
    const size_t memoryBytes = (std::max(body.size(), size_t(0x20000)) + 0xFFF) & ~size_t(0xFFF);
    if (memoryBytes > 0x400000) {
        error = "JSON is too large to upload";
        return false;
    }

    const auto fail = [&error](const char* step, Result code) -> bool {
        char hex[12];
        std::snprintf(hex, sizeof(hex), "%08lX", static_cast<unsigned long>(code));
        error = std::string("HTTP ") + step + ": 0x" + hex;
        return false;
    };

    Result rc = httpcInit(static_cast<u32>(memoryBytes));
    if (R_FAILED(rc)) return fail("init", rc);

    httpcContext context;
    rc = httpcOpenContext(&context, HTTPC_METHOD_POST, kEndpoint, 0);
    if (R_FAILED(rc)) {
        httpcExit();
        return fail("open", rc);
    }

    const char* step = "headers";
    const std::string type = std::string("multipart/form-data; boundary=") + boundary;
    rc = httpcAddRequestHeaderField(&context, "Content-Type", type.c_str());
    if (R_SUCCEEDED(rc)) { step = "POST data"; rc = httpcAddPostDataRaw(&context, aligned.data(), static_cast<u32>(body.size())); }
    if (R_SUCCEEDED(rc)) { step = "connect"; rc = httpcBeginRequest(&context); }

    u32 status = 0;
    if (R_SUCCEEDED(rc)) {
        step = "response";
        rc = httpcGetResponseStatusCodeTimeout(&context, &status, 20000000000ULL);
    }

    std::string response;
    if (R_SUCCEEDED(rc)) {
        step = "read";
        Result readResult;
        do {
            u8 chunk[1024];
            u32 downloaded = 0;
            readResult = httpcDownloadData(&context, chunk, sizeof(chunk), &downloaded);
            if (downloaded != 0 && response.size() < 4096) {
                const size_t keep = std::min<size_t>(downloaded, 4096 - response.size());
                response.append(reinterpret_cast<const char*>(chunk), keep);
            }
        } while (readResult == static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING));
        rc = readResult;
    }

    httpcCloseContext(&context);
    httpcExit();

    if (R_FAILED(rc)) return fail(step, rc);
    if (status != 200) {
        error = "Server returned HTTP " + std::to_string(status);
        return false;
    }
    // json_curl.php returns HTTP 200 even for invalid credentials or import errors.
    if (response.find("JSON Data imported successfully!") == std::string::npos) {
        error = response.find("Error importing JSON") != std::string::npos
              ? "Server could not import JSON"
              : "Import not confirmed (check login)";
        return false;
    }
    return true;
}

bool askText(const char* hint, bool password, std::string& output) {
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 128);
    swkbdSetHintText(&keyboard, hint);
    if (password) swkbdSetPasswordMode(&keyboard, SWKBD_PASSWORD_HIDE);
    char buffer[256] = {0};
    if (swkbdInputText(&keyboard, buffer, sizeof(buffer)) != SWKBD_BUTTON_CONFIRM) return false;
    if (!buffer[0]) return false;
    output = buffer;
    if (output.find('\n') != std::string::npos || output.find('\r') != std::string::npos) return false;
    return true;
}

void draw(const Config& config, const std::string& status, int selected,
          const ActivityLogInfo& activityLogInfo) {
    consoleClear();
    std::printf("PHALK PROFILES - NINTENDO 3DS\n");
    std::printf("--------------------------------\n\n");
    std::printf("Account: %.26s\n", config.username.empty() ? "(not set)" : config.username.c_str());
    std::printf("Password: %s\n", config.password.empty() ? "(not set)" : "********");
    const std::string lastOpened = formatActivityLogLastOpened(activityLogInfo);
    std::printf("Activity Log last opened: %s\n", lastOpened.c_str());
    std::printf("Open it before exporting to refresh sessions.\n\n");

    const std::string updateAction = std::string("Update Activity Log (Last update: ") +
        (activityLogInfo.lastOpenedKnown ? lastOpened.substr(0, 10) : "unknown") + ")";
    const std::string actions[] = {"Create JSON", "Create and upload", "Set username", "Set password",
                                   updateAction, "Exit"};
    for (int i = 0; i < 6; ++i)
        std::printf(" %c %s\n", i == selected ? '>' : ' ', actions[i].c_str());
    std::printf("\nStatus:\n");
    for (size_t start = 0; start < status.size() && start < 175; start += 35)
        std::printf("%.*s\n", 35, status.c_str() + start);
    std::printf("\nSource: Activity Log (NAND)\n");
    std::printf("A: select  START: exit\n");
}

} // namespace

int main() {
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_TOP, NULL);
    const Result fsResult = fsInit();
    const Result romfsResult = romfsInit();
    Config config = loadConfig();
    std::string status = R_FAILED(fsResult) ? "File system init failed"
        : (R_SUCCEEDED(romfsResult) ? "Ready" : "Game database unavailable");
    const ActivityLogInfo activityLogInfo = readActivityLogInfo();
    int selected = 0;
    bool dirty = true;
    bool openActivityLog = false;

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_START) break;
        if (down & KEY_UP) { selected = (selected + 5) % 6; dirty = true; }
        if (down & KEY_DOWN) { selected = (selected + 1) % 6; dirty = true; }

        if (down & KEY_A) {
            if (selected == 5) break;
            if (selected == 4) {
                const u64 titleId = activityLogInfo.titleId != 0
                    ? activityLogInfo.titleId : activityLogTitleIdForSystemRegion();
                if (titleId == 0) {
                    status = "Could not identify the system region";
                } else {
                    status = "Opening Activity Log...";
                    draw(config, status, selected, activityLogInfo);
                    presentFrame();
                    openActivityLog = true;
                    break;
                }
            } else if (selected == 2 || selected == 3) {
                std::string value;
                if (askText(selected == 2 ? "Phalk username" : "Phalk password", selected == 3, value)) {
                    if (selected == 2) config.username = value;
                    else config.password = value;
                    status = saveConfig(config) ? "Settings saved" : "Could not save settings";
                }
            } else {
                unsigned int count = 0;
                status = "Reading Activity Log...";
                draw(config, status, selected, activityLogInfo);
                presentFrame();
                std::string error;
                if (R_SUCCEEDED(fsResult) && R_SUCCEEDED(romfsResult) && prepareValidatedExport(count, error)) {
                    status = "JSON created: " + std::to_string(count) + " titles";
                    if (selected == 1) {
                        status = "Uploading via HTTP...";
                        draw(config, status, selected, activityLogInfo);
                        presentFrame();
                        status = uploadExport(config, error) ? "Upload complete (HTTP 200)" : error;
                    }
                } else {
                    status = R_FAILED(fsResult) ? "File system init failed"
                        : (R_FAILED(romfsResult) ? "Game database unavailable" : error);
                }
            }
            dirty = true;
        }
        if (dirty) { draw(config, status, selected, activityLogInfo); dirty = false; }
        presentFrame();
    }
    if (openActivityLog)
        aptSetChainloader(activityLogInfo.titleId != 0
                              ? activityLogInfo.titleId : activityLogTitleIdForSystemRegion(),
                          MEDIATYPE_NAND);
    if (R_SUCCEEDED(romfsResult)) romfsExit();
    if (R_SUCCEEDED(fsResult)) fsExit();
    gfxExit();
    return 0;
}
