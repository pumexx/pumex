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
#include <vector>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

class QueueTraits;
class Viewer;

// Implementation of a Vulkan physical device, works mainly as a database of device properties.
// Class is not created by user. Instead of this its built during Viewer ( VkInstance ) creation
class PUMEX_EXPORT PhysicalDevice
{
public:
  PhysicalDevice()                                 = delete;
  explicit PhysicalDevice(VkPhysicalDevice aDevice, Viewer* viewer);
  PhysicalDevice(const PhysicalDevice&)            = delete;
  PhysicalDevice& operator=(const PhysicalDevice&) = delete;
  PhysicalDevice(PhysicalDevice&&)                 = delete;
  PhysicalDevice& operator=(PhysicalDevice&&)      = delete;
  virtual ~PhysicalDevice();

  std::vector<uint32_t> matchingFamilyIndices(const QueueTraits& queueDescription);
  uint32_t              getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr);

  bool                  deviceExtensionImplemented(const char* extensionName) const;

  // physical device
  VkPhysicalDevice                       physicalDevice        = VK_NULL_HANDLE;

  VkPhysicalDeviceProperties             properties;
  VkPhysicalDeviceMultiviewPropertiesKHR multiViewProperties;

  VkPhysicalDeviceFeatures               features;
  VkPhysicalDeviceMultiviewFeaturesKHR   multiViewFeatures;

  VkPhysicalDeviceMemoryProperties       memoryProperties;

  std::vector<VkExtensionProperties>     extensionProperties;

  std::vector<VkQueueFamilyProperties>   queueFamilyProperties;
  // only when VK_EXT_KHR_display extension is present ( we are not using it atm )
  //std::vector<VkDisplayPropertiesKHR>    displayProperties;

};

}
