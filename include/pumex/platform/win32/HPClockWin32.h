//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

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