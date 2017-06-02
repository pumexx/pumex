#pragma once
#include <chrono>
#if defined(_WIN32)
  #include <pumex/platform/win32/HPClockWin32.h>
#endif

namespace pumex
{

#if defined(_WIN32)
  using HPClock = HPClockWin32;
#else  
  using HPClock = std::chrono::high_resolution_clock;
#endif

inline double inSeconds(const pumex::HPClock::duration& duration)
{
  return std::chrono::duration<double, std::ratio<1, 1>>(duration).count();
}

	
}