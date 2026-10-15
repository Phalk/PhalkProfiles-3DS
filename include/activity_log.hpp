#pragma once

#include <3ds.h>

#include <string>
#include <vector>

struct ActivityTitle {
    u64 id;
    u32 playtimeSeconds;
    u16 launches;
    u16 firstPlayedDays;
    u16 lastPlayedDays;
};

// Reads the console's own Activity Log summary from NAND without modifying it.
bool readActivityLog(std::vector<ActivityTitle>& titles, std::string& error);

// Looks up a title name in the bundled RomFS database by its 3DS TitleID.
// Returns an empty string when the TitleID is not in the database.
std::string activityTitleName(u64 id);

// Converts days since 2000-01-01 into a Unix timestamp (seconds).
u64 activityUnixTime(u16 days);
