#pragma once
#include <chrono>
#if defined(_WIN32)
  #include <pumex/platform/win32/HPClockWin32.h>
#endif

namespace pumex
{
	
#if defined(_WIN32)
  #define HPClock HPClockWin32
#else  
  #define HPClock std::chrono::high_resolution_clock
#endif
	
}