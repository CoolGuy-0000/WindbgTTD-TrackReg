#include <Windows.h>
#include "utils.h"

void SafeRelease(IUnknown*& i) {
	if (i) {
		i->Release();
		i = nullptr;
	}
}