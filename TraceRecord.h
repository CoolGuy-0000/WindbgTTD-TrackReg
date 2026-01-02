#pragma once

#include <TTD/IReplayEngine.h> // For Position

using namespace TTD;
using namespace Replay;

// Binary struct for file
struct TraceRecord {
    int id;
    int parentId;
    Position pos;
};

