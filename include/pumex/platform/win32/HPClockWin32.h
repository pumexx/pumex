#pragma once
#include <chrono>
#include <pumex/Export.h>
#include <pumex/utils/Log.h>

namespace pumex
{

// Implementation of HPClock has been adapted from stackoverflow ( thx to MS for high_resolution_clock implementation on VS :( ) :
// FIXME : there are rumours that high_resolution_clock has been properly implemented on newest versions of Visual Studio
// http://stackoverflow.com/questions/13263277/difference-between-stdsystem-clock-and-stdsteady-clock
	
struct PUMEX_EXPORT HPClockWin32
{
  typedef std::chrono::nanoseconds                        duration;
  typedef duration::rep                                   rep;
  typedef duration::period                                period;
  typedef std::chrono::time_point<HPClockWin32, duration> time_point;
  static bool is_steady;
  static time_point now()
  {
    if(!initialized) {
      init();
      initialized = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return time_point(duration(static_cast<rep>((double)counter.QuadPart / frequency.QuadPart * period::den / period::num)));
  }

private:
  static bool initialized;
  static LARGE_INTEGER frequency;
  static void init()
  {
	CHECK_LOG_THROW( QueryPerformanceFrequency(&frequency) == 0, "QueryPerformanceFrequency returns 0" );
  }
};	

}