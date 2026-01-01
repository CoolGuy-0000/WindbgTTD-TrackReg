#pragma once
#include <TTD/IReplayEngine.h> // For Position

#pragma pack(push, 1)
// Binary struct for file
struct TraceRecord {
    int id;
    int parentId;
    Position pos;
};
#pragma pack(pop)
