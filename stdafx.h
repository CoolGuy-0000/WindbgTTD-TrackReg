#pragma once

#if defined(_M_ARM64)
	
#elif defined(_M_ARM)

#elif defined(_M_X64)
	#define KDEXT_64BIT
#elif defined(_M_IX86)

#else
	#error Unknown architecture
#endif

#include <crtdbg.h>

#ifndef DBG_ASSERT
#ifdef _DEBUG
#define DBG_ASSERT(x) _ASSERT(x)
#else
#define DBG_ASSERT(x)
#endif
#endif