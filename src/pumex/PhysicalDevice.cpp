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
#include <pumex/Viewer.h>
#include <pumex/utils/Log.h>

using namespace pumex;

PhysicalDevice::PhysicalDevice(VkPhysicalDevice device, Viewer* viewer)
  : physicalDevice{ device }, properties{}, multiViewProperties{}, features{}, multiViewFeatures{}
{
  // collect all available data about the device with or without VK_KHR_get_physical_device_properties2 extension

  if (viewer->instanceExtensionEnabled(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
  {
    // get the physical device properties
    VkPhysicalDeviceProperties2 properties2;
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    properties2.pNext = &multiViewProperties;
    multiViewProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;

    vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
    properties = properties2.properties;

    // get the physical device features
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    features2.pNext         = &multiViewFeatures;
    multiViewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
    features = features2.features;
  }
  else
  {
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);
  }

  // physical device memory properties
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

  uint32_t extensionCount = 0;
  VK_CHECK_LOG_THROW( vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr), "failed vkEnumerateDeviceExtensionProperties");
  if (extensionCount>0)
  {
    extensionProperties.resize(extensionCount);
    VK_CHECK_LOG_THROW(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensionProperties.data()), "failed vkEnumerateDeviceExtensionProperties" << extensionCount);
  }

  uint32_t queueFamilyCount;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
  if(queueFamilyCount > 0)
  {
    queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
  }

  // it's just a begining of VK_KHR_DISPLAY. At the moment there's nothing more
  if (viewer->instanceExtensionEnabled(VK_KHR_DISPLAY_EXTENSION_NAME))
  {
    uint32_t displayCount = 0;
    VK_CHECK_LOG_THROW(vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayCount, nullptr), "failed vkGetPhysicalDeviceDisplayPropertiesKHR");
    if (displayCount>0)
    {
      displayProperties.resize(displayCount);
      VK_CHECK_LOG_THROW(vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayCount, displayProperties.data()), "failed vkGetPhysicalDeviceDisplayPropertiesKHR" << displayCount);
    }
  }
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

bool PhysicalDevice::deviceExtensionImplemented(const char* extensionName) const
{
  for (const auto& e : extensionProperties)
    if (!std::strcmp(extensionName, e.extensionName))
      return true;
  return false;
}

