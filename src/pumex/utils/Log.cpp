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

#include <pumex/utils/Log.h>
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
  #include <android/log.h>
#endif

class NullStreamBuffer : public std::streambuf
{
public:
  std::streamsize xsputn(const char*s, std::streamsize num) override
  {
    return num;
  }
};

// This code is for redirecting std::cout to Android logcat
// found here : http://code.i-harness.com/en/q/87591e
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
class AndroidStreamBuffer : public std::streambuf 
{
public:
  enum { bufsize = 256 };
  AndroidStreamBuffer() 
  { 
    this->setp(buffer, buffer + bufsize - 1); 
  }
private:
  int overflow(int c)
  {
    if (c == traits_type::eof()) 
    {
       *this->pptr() = traits_type::to_char_type(c);
       this->sbumpc();
    }
    return this->sync()? traits_type::eof(): traits_type::not_eof(c);
  }

  int sync()
  {
    int rc = 0;
    if (this->pbase() != this->pptr()) 
    {
      char writebuf[bufsize+1];
      memcpy(writebuf, this->pbase(), this->pptr() - this->pbase());
      writebuf[this->pptr() - this->pbase()] = '\0';

      rc = __android_log_write(ANDROID_LOG_INFO, "std", writebuf) > 0;
      this->setp(buffer, buffer + bufsize - 1);
    }
    return rc;
  }

  char buffer[bufsize];
};
#endif
static float logSeverity = 75.0f;
static NullStreamBuffer nullStreamBuffer;
static std::ostream nullStream(&nullStreamBuffer);

bool isLogEnabled(float severity)
{
  return severity <= logSeverity;
}

std::ostream& doLog(float severity)
{
  if( isLogEnabled(severity) )
  {
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    static bool logcatInitialized = false;
    if(!logcatInitialized)
    {
      std::cout.rdbuf(new AndroidStreamBuffer);
      logcatInitialized = true;
    }
#endif
    return std::cout;
  }
  return nullStream;
}

void setLogSeverity(float severity)
{
  logSeverity = severity;
}

std::string vulkanErrorString(VkResult errorCode)
{
  switch (errorCode)
  {
#define STR(r) case VK_ ##r: return #r
    STR(NOT_READY);
    STR(TIMEOUT);
    STR(EVENT_SET);
    STR(EVENT_RESET);
    STR(INCOMPLETE);
    STR(ERROR_OUT_OF_HOST_MEMORY);
    STR(ERROR_OUT_OF_DEVICE_MEMORY);
    STR(ERROR_INITIALIZATION_FAILED);
    STR(ERROR_DEVICE_LOST);
    STR(ERROR_MEMORY_MAP_FAILED);
    STR(ERROR_LAYER_NOT_PRESENT);
    STR(ERROR_EXTENSION_NOT_PRESENT);
    STR(ERROR_FEATURE_NOT_PRESENT);
    STR(ERROR_INCOMPATIBLE_DRIVER);
    STR(ERROR_TOO_MANY_OBJECTS);
    STR(ERROR_FORMAT_NOT_SUPPORTED);
    STR(ERROR_SURFACE_LOST_KHR);
    STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
    STR(SUBOPTIMAL_KHR);
    STR(ERROR_OUT_OF_DATE_KHR);
    STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
    STR(ERROR_VALIDATION_FAILED_EXT);
    STR(ERROR_INVALID_SHADER_NV);
#undef STR
default:
  return "UNKNOWN_ERROR";
  }
}
