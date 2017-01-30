#include <pumex/platform/win32/HPClockWin32.h>

namespace pumex
{

bool          HPClockWin32::is_steady   = true;
bool          HPClockWin32::initialized = false;
LARGE_INTEGER HPClockWin32::frequency   = { 0, 0 };
	
}
