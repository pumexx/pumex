#include <pumex/PhysicalDevice.h>
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

std::vector<uint32_t> PhysicalDevice::matchingFamilyIndices(const pumex::QueueTraits& queueTraits)
{
  std::vector<uint32_t> results;
  for (uint32_t i = 0; i<queueFamilyProperties.size(); ++i)
  {
    if ((queueFamilyProperties[i].queueFlags & queueTraits.mustHave) != queueTraits.mustHave)
      continue;
    if ((~queueFamilyProperties[i].queueFlags & queueTraits.mustNotHave) != queueTraits.mustNotHave)
      continue;
    if (queueFamilyProperties[i].queueCount < queueTraits.priority.size())
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
    if (!strcmp(extensionName, e.extensionName))
      return true;
  return false;
}

