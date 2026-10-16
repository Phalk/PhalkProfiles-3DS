#pragma once

#include "activity_log.hpp"

#include <map>
#include <string>
#include <vector>

struct PlayDataTitle {
    u64 id;
    u32 playtimeSeconds;
    u32 launches;
    u16 firstPlayedDays;
    u16 lastPlayedDays;
    u32 latestSessionSeconds;
};

struct PlayDataDatabase {
    bool initialized;
    std::map<u64, PlayDataTitle> titles;
    s32 nextEventIndex;
    s32 lastEventIndex;
    u64 lastEventSignature;
};

void initializePlayDataDatabase(PlayDataDatabase& database);
bool loadPlayDataDatabase(PlayDataDatabase& database, std::string& error);
bool resetPlayDataDatabase(std::string& error);
bool initializePlayDataFromPld(PlayDataDatabase& database,
                               const std::vector<ActivityTitle>& pld,
                               std::string& error);
bool syncPlayDataFromPtm(PlayDataDatabase& database, std::string& error);
bool savePlayDataDatabase(const PlayDataDatabase& database, std::string& error);
