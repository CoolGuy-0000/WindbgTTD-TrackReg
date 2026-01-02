#pragma once
#include "stdafx.h"

#include <Windows.h>
#include <WDBGEXTS.H>
#include <atlcomcli.h>
#include <map>
#include <vector>
#include <string>
#include <dbgeng.h>
#include "TraceRecord.h"


// Shared logic from timetrack.cpp
std::map<int, std::vector<TraceRecord>> _TimeTrack(IDebugClient* client, const std::string& arg);
