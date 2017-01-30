#pragma once
#include <string>
#include <iostream>
#include <pumex/Export.h>
#include <vulkan/vulkan.h>

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

class vulkan_exception : public std::exception
{
};

PUMEX_EXPORT std::string vulkanErrorString(VkResult errorCode);

#define VK_CHECK_LOG_THROW( expression, loginfo ) \
{ \
	VkResult res = (expression); \
	if (res != VK_SUCCESS) \
  { \
		LOG_ERROR << "[ " << __FILE__<<" : " << __LINE__ << " : " << vulkanErrorString(res) << " ] : "<< loginfo << std::endl; \
		throw vulkan_exception(); \
 	} \
} 

#define CHECK_LOG_THROW( expression, loginfo ) \
{ \
	if((expression)) \
    { \
	  LOG_ERROR << "[ " << __FILE__<<" : " << __LINE__ << " ] : "<< loginfo << std::endl; \
		throw vulkan_exception(); \
   	} \
} 

extern PUMEX_EXPORT bool isLogEnabled(float severity);
extern PUMEX_EXPORT std::ostream& doLog(float severity);
extern PUMEX_EXPORT void setLogSeverity(float severity);

//		LOG_ERROR << "VkResult is \"" << vkTools::errorString(res) << "\" in function " << #expression << __FILE__ << " at line " << __LINE__ << std::endl;
