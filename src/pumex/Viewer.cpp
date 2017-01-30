#include <pumex/Viewer.h>
#include <pumex/utils/Log.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Device.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/Thread.h>
#include <pumex/SurfaceThread.h>
#if defined(_WIN32)
  #include <pumex/platform/win32/WindowWin32.h>
  #include <direct.h>
  #define getcwd _getcwd
#else
  #include <unistd.h>
#endif
#include <sys/stat.h>

using namespace pumex;

ViewerTraits::ViewerTraits(const std::string& aName, bool uv, const std::vector<std::string>& rl)
  : applicationName(aName), useValidation{ uv }, requestedLayers(rl)
{
}

const uint32_t MAX_PATH_LENGTH = 256;

Viewer::Viewer(const pumex::ViewerTraits& vt)
  : viewerTraits{ vt }
{
  startTime = pumex::HPClock::now();

  // register basic directories - current directory
  char strCurrentPath[MAX_PATH_LENGTH];
  if (getcwd(strCurrentPath, MAX_PATH_LENGTH))
  {
    std::string currentDir(strCurrentPath);
    defaultDirectories.push_back(currentDir);
#if defined(_WIN32)
    defaultDirectories.push_back(currentDir + "\\data");
    // also register data directory specific for development
    currentDir = currentDir.substr(0, currentDir.find_last_of("\\"));
    defaultDirectories.push_back(currentDir + "\\data");
    defaultDirectories.push_back(currentDir + "\\data\\textures");
#else
    defaultDirectories.push_back(currentDir + "/data");
    currentDir = currentDir.substr(0, currentDir.find_last_of("/"));
    defaultDirectories.push_back(currentDir + "/data");
    defaultDirectories.push_back(currentDir + "/data/textures");
#endif
  }
  // register basic directories - executable directory and data directory
  char strExePath[MAX_PATH_LENGTH];
#if defined(_WIN32)
  GetModuleFileNameA(NULL, strExePath, MAX_PATH_LENGTH);
  std::string exeDir = strExePath;
  exeDir = exeDir.substr(0, exeDir.find_last_of("\\"));
  defaultDirectories.push_back(exeDir);
  defaultDirectories.push_back(exeDir + "\\data");
  defaultDirectories.push_back(exeDir + "\\data\\textures");
#else
  {
    char id[MAX_PATH_LENGTH];
    sprintf(id, "/proc/%d/exe", getpid());
    ssize_t size = readlink(id, strExePath, 255);
    if ((size < 0) || (size > MAX_PATH_LENGTH))
      size = 0;
    strExePath[size] = '\0';
  }
  std::string exeDir = strExePath;
  if(!exeDir.empty())
  {
    exeDir = appDir.substr(0, appDir.find_last_of("/"));
    defaultDirectories.push_back(exeDir);
    defaultDirectories.push_back(exeDir+"/data");
    defaultDirectories.push_back(exeDir+"/data/textures");
}
#endif


  // create vulkan instance with requierd extensions
  std::vector<const char*> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };
#if defined(_WIN32)
  enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
  enabledExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(__ANDROID__)
  enabledExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
  if (viewerTraits.useValidation)
  {
    enabledExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
  }

  VkApplicationInfo applicationInfo{};
    applicationInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName   = viewerTraits.applicationName.c_str();
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName        = "pumex";
    applicationInfo.engineVersion      = 1;
    applicationInfo.apiVersion         = VK_API_VERSION_1_0;

  VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo        = &applicationInfo;
    instanceCreateInfo.enabledExtensionCount   = (uint32_t)enabledExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();

  char** layerTab = nullptr;
  if (viewerTraits.useValidation)
  {
    instanceCreateInfo.enabledLayerCount   = viewerTraits.requestedLayers.size();
    layerTab = new char*[viewerTraits.requestedLayers.size()];
    for (uint32_t i=0; i<viewerTraits.requestedLayers.size(); ++i)
      layerTab[i] = const_cast<char*>(viewerTraits.requestedLayers[i].c_str());
    instanceCreateInfo.ppEnabledLayerNames = layerTab;
  }

  VK_CHECK_LOG_THROW(vkCreateInstance(&instanceCreateInfo, nullptr, &instance), "Cannot create instance");

  if (layerTab!=nullptr)
    delete[] layerTab;

  if (viewerTraits.useValidation)
    setupDebugging(viewerTraits.debugReportFlags, viewerTraits.debugReportCallback);

  // collect all available physical devices
  uint32_t deviceCount = 0;
  VK_CHECK_LOG_THROW(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "Cannot enumerate physical devices");
  std::vector<VkPhysicalDevice> phDevices(deviceCount);
  VK_CHECK_LOG_THROW(vkEnumeratePhysicalDevices(instance, &deviceCount, phDevices.data()), "Cannot enumerate physical devices " << deviceCount );

  for (auto it : phDevices)
    physicalDevices.push_back(std::make_shared<PhysicalDevice>(it));

  // collect information about extensions
  uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
  extensionProperties.resize(extensionCount);
  vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensionProperties.data());
}

Viewer::~Viewer()
{
  cleanup();
}

void Viewer::cleanup()
{
  if (instance != VK_NULL_HANDLE)
  {
    for( auto t : pumexThreads )
      t->cleanup();
    pumexThreads.clear();
    for( auto s : surfaces )
      s->cleanup();
    surfaces.clear();
    for ( auto d : devices )
      d->cleanup();
    devices.clear();
    physicalDevices.clear();
    if (viewerTraits.useValidation)
      cleanupDebugging();
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

std::shared_ptr<pumex::Device> Viewer::addDevice(unsigned int physicalDeviceIndex, const std::vector<pumex::QueueTraits>& requestedQueues, const std::vector<const char*>& requestedExtensions)
{
  CHECK_LOG_THROW(physicalDeviceIndex >= physicalDevices.size(), "Could not create device. Index is too high : " << physicalDeviceIndex);

  std::shared_ptr<pumex::Device> device = std::make_shared<pumex::Device>(shared_from_this(), physicalDevices[physicalDeviceIndex], requestedQueues, requestedExtensions);
  // FIXME - co gdy nie ma kolejek ?
  devices.push_back(device);
  return device;
}

std::shared_ptr<pumex::Surface> Viewer::addSurface(std::shared_ptr<pumex::Window> window, std::shared_ptr<pumex::Device> device, const pumex::SurfaceTraits& surfaceTraits, std::shared_ptr<pumex::SurfaceThread> surfaceThread)
{
  std::shared_ptr<pumex::Surface> surface = window->createSurface(shared_from_this(), device, surfaceTraits);
  surfaceThread->setup(surface);
  surface->setSurfaceThread(surfaceThread);
  surfaces.push_back(surface);
  addThread(surfaceThread.get());
  return surface;
}

void Viewer::addThread(pumex::Thread* thread)
{
  pumexThreads.push_back(thread);
}

void Viewer::run()
{
  ThreadJoiner joiner;
  for (auto t : pumexThreads)
    joiner.addThread(t);
#if defined(_WIN32)
  WindowWin32::checkWindowMessages();
#endif
}

std::string Viewer::getFullFilePath(const std::string& shortFileName) const
{
  struct stat buf;
  for ( const auto& d : defaultDirectories )
  {
#if defined(_WIN32)
    std::string fullFilePath( d + "\\" + shortFileName );
#else
    std::string fullDirectory(d + "/" + shortFileName);
#endif
    if (stat(fullFilePath.c_str(), &buf) == 0)
      return fullFilePath;
  }
  return std::string();
}

void Viewer::setupDebugging(VkDebugReportFlagsEXT flags, VkDebugReportCallbackEXT callBack)
{
  pfnCreateDebugReportCallback  = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
  pfnDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
  pfnDebugReportMessage         = reinterpret_cast<PFN_vkDebugReportMessageEXT>(vkGetInstanceProcAddr(instance, "vkDebugReportMessageEXT"));

  VkDebugReportCallbackCreateInfoEXT dbgCreateInfo = {};
    dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)messageCallback;
    dbgCreateInfo.flags = flags;
  VK_CHECK_LOG_THROW( pfnCreateDebugReportCallback(instance, &dbgCreateInfo, nullptr, (callBack != nullptr) ? &callBack : &msgCallback), "Cannot create debug report callback");
}

void Viewer::cleanupDebugging()
{
  if (msgCallback != VK_NULL_HANDLE)
  {
    pfnDestroyDebugReportCallback(instance, msgCallback, nullptr);
    msgCallback = VK_NULL_HANDLE;
  }
}

VkBool32 pumex::messageCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void* pUserData)
{
  std::string prefix("");

  if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
    LOG_ERROR << "ERROR: [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg << std::endl;
  if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
    LOG_WARNING << "WARNING: [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg << std::endl;
  if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
    LOG_INFO << "PERFORMANCE: [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg << std::endl;
  if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
    LOG_INFO << "INFO: [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg << std::endl;
  if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
    LOG_JUNK << "DEBUG: [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg << std::endl;
  FLUSH_LOG;
  return VK_FALSE;
}
