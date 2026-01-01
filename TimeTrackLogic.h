#pragma once
#include <map>
#include <vector>
#include <string>
#include <DbgEng.h>
#include "TraceRecord.h"

// Shared logic from timetrack.cpp
std::map<int, std::vector<TraceRecord>> _TimeTrack(IDebugClient* client, const std::string& arg);
