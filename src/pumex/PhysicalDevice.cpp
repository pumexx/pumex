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

#include <pumex/PhysicalDevice.h>
#include <cstring>
#include <pumex/Device.h>
#include <pumex/utils/Log.h>

using namespace pumex;

PhysicalDevice::PhysicalDevice(VkPhysicalDevice device)
  : physicalDevice{device}
{
  // collect all available data about the device
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);
  vkGetPhysicalDeviceFeatures(physicalDevice, &features);
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

  uint32_t extensionCount = 0;
  VK_CHECK_LOG_THROW( vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr), "failed vkEnumerateDeviceExtensionProperties");
  if (extensionCount>0)
  {
    extensions.resize(extensionCount);
    VK_CHECK_LOG_THROW(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data()), "failed vkEnumerateDeviceExtensionProperties" << extensionCount);
  }

  uint32_t queueFamilyCount;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
  if(queueFamilyCount > 0)
  {
    queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
  }

  // only when VK_EXT_KHR_display is present
  //uint32_t displayCount = 0;
  //vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayCount, nullptr);
  //if (displayCount>0)
  //{
  //  displayProperties.resize(displayCount);
  //  vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayCount, displayProperties.data());
  //}

}

PhysicalDevice::~PhysicalDevice()
{
	
}

std::vector<uint32_t> PhysicalDevice::matchingFamilyIndices(const QueueTraits& queueTraits)
{
  std::vector<uint32_t> results;
  for (uint32_t i = 0; i<queueFamilyProperties.size(); ++i)
  {
    if ((queueFamilyProperties[i].queueFlags & queueTraits.mustHave) != queueTraits.mustHave)
      continue;
    if ((~queueFamilyProperties[i].queueFlags & queueTraits.mustNotHave) != queueTraits.mustNotHave)
      continue;
    results.push_back(i);
  }
  return std::move(results);
}

uint32_t PhysicalDevice::getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound)
{
  for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
  {
    if ((typeBits & 1) == 1)
    {
      if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
      {
        if (memTypeFound)
        {
          *memTypeFound = true;
        }
        return i;
      }
    }
    typeBits >>= 1;
  }

#if defined(__ANDROID__)
  //todo : Exceptions are disabled by default on Android (need to add LOCAL_CPP_FEATURES += exceptions to Android.mk), so for now just return zero
  if (memTypeFound)
  {
    *memTypeFound = false;
  }
  return 0;
#else
  if (memTypeFound)
  {
    *memTypeFound = false;
    return 0;
  }
  else
  {
    throw std::runtime_error("Could not find a matching memory type");
  }
#endif
}

bool PhysicalDevice::hasExtension(const char* extensionName)
{
  for (auto e : extensions)
    if (!std::strcmp(extensionName, e.extensionName))
      return true;
  return false;
}

