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
#include <string>
#include <iostream>
#include <sstream>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

// Set of functions and classes implementing possibility to log any information to output files ( only std::cout is used for now )

// TODO for current version : 
// - does not add additional elements to stream ( timestamp, severity, etc )
// - is not multithreaded
// - cannot send results to streams other than std::cout

#define SET_LOG_JUNK    setLogSeverity(100.0f)
#define SET_LOG_INFO    setLogSeverity(75.0f)
#define SET_LOG_WARNING setLogSeverity(50.0f)
#define SET_LOG_ERROR   setLogSeverity(25.0f)
#define SET_LOG_FATAL   setLogSeverity(0.0f)
#define SET_LOG_NONE    setLogSeverity(-100.0f)

#define LOG_JUNK    doLog(100.0f)
#define LOG_INFO    doLog(75.0f)
#define LOG_WARNING doLog(50.0f)
#define LOG_ERROR   doLog(25.0f)
#define LOG_FATAL   doLog(0.0f)

#define FLUSH_LOG  doLog(0.0f).flush();

PUMEX_EXPORT std::string vulkanErrorString(VkResult errorCode);

#define VK_CHECK_LOG_THROW( expression, loginfo ) \
{ \
  VkResult res = (expression); \
  if (res != VK_SUCCESS) \
  { \
    std::ostringstream stream; \
    stream << "[ " << __FILE__<<" : " << __LINE__ << " : " << vulkanErrorString(res) << " ] : "<< loginfo; \
    throw std::runtime_error(stream.str()); \
  } \
} 

#define CHECK_LOG_THROW( expression, loginfo ) \
{ \
  if((expression)) \
  { \
    std::ostringstream stream; \
    stream << "[ " << __FILE__ << " : " << __LINE__ << " ] : " << loginfo; \
    throw std::runtime_error(stream.str()); \
  } \
} 

#define CHECK_LOG_RETURN_VOID( expression, loginfo ) \
{ \
	if((expression)) \
    { \
	  LOG_ERROR << "[ " << __FILE__<<" : " << __LINE__ << " ] : "<< loginfo << std::endl; \
		return; \
   	} \
} 

#define CHECK_LOG_RETURN_VALUE( expression, value, loginfo ) \
{ \
	if((expression)) \
    { \
	  LOG_ERROR << "[ " << __FILE__<<" : " << __LINE__ << " ] : "<< loginfo << std::endl; \
		return (value); \
   	} \
} 

extern PUMEX_EXPORT bool isLogEnabled(float severity);
extern PUMEX_EXPORT std::ostream& doLog(float severity);
extern PUMEX_EXPORT void setLogSeverity(float severity);

//		LOG_ERROR << "VkResult is \"" << vkTools::errorString(res) << "\" in function " << #expression << __FILE__ << " at line " << __LINE__ << std::endl;
