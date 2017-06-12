#include <pumex/Viewer.h>
#include <pumex/utils/Log.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Device.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/Thread.h>
#if defined(_WIN32)
  #include <pumex/platform/win32/WindowWin32.h>
  #include <direct.h>
  #define getcwd _getcwd
#elif defined(__linux__)
  #include <pumex/platform/linux/WindowXcb.h>
  #include <unistd.h>
#endif
#include <sys/stat.h>

using namespace pumex;

ViewerTraits::ViewerTraits(const std::string& aName, bool uv, const std::vector<std::string>& rl, uint32_t ups)
  : applicationName(aName), useValidation{ uv }, requestedLayers(rl), updatesPerSecond(ups)
{
}

const uint32_t MAX_PATH_LENGTH = 256;

Viewer::Viewer(const ViewerTraits& vt)
  : viewerTraits{ vt }, 
  startUpdateGraph { updateGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  endUpdateGraph   { updateGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  startRenderGraph { renderGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  endRenderGraph   { renderGraph, [=](tbb::flow::continue_msg) { doNothing(); } }
{
  viewerStartTime     = HPClock::now();
  for(uint32_t i=0; i<3;++i)
    updateStartTimes[i] = viewerStartTime;
  renderStartTime     = viewerStartTime;
  lastRenderDuration  = viewerStartTime - renderStartTime;
  lastUpdateDuration  = viewerStartTime - updateStartTimes[0];

  // register basic directories - directories listed in PUMEX_DATA_DIR environment variable, spearated by semicolon
  const char* dataDirVariable = getenv("PUMEX_DATA_DIR");
  if (dataDirVariable != nullptr)
  {
    const char* currentPos = dataDirVariable;
    do
    {
      const char *begin = currentPos;
      while (*currentPos != ';' && *currentPos != 0)
        currentPos++;
      defaultDirectories.push_back( std::string(begin, currentPos));
    } while (*currentPos++ != 0);
  }

  // register basic directories - current directory
  char strCurrentPath[MAX_PATH_LENGTH];
  if (getcwd(strCurrentPath, MAX_PATH_LENGTH))
  {
    std::string currentDir(strCurrentPath);
    defaultDirectories.push_back(currentDir);
#if defined(_WIN32)
    defaultDirectories.push_back(currentDir + "\\data");
    defaultDirectories.push_back(currentDir + "\\data\\textures");
    defaultDirectories.push_back(currentDir + "\\..\\data");
    defaultDirectories.push_back(currentDir + "\\..\\data\\textures");
    defaultDirectories.push_back(currentDir + "\\..\\..\\data");
    defaultDirectories.push_back(currentDir + "\\..\\..\\data\\textures");
#else
    defaultDirectories.push_back(currentDir + "/data");
    currentDir = currentDir.substr(0, currentDir.find_last_of("/"));
    defaultDirectories.push_back(currentDir + "/data");
    defaultDirectories.push_back(currentDir + "/data/textures");
    defaultDirectories.push_back(currentDir + "../data");
    defaultDirectories.push_back(currentDir + "../data/textures");
    defaultDirectories.push_back(currentDir + "../../data");
    defaultDirectories.push_back(currentDir + "../../data/textures");
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
    exeDir = exeDir.substr(0, exeDir.find_last_of("/"));
    defaultDirectories.push_back(exeDir);
    defaultDirectories.push_back(exeDir+"/data");
    defaultDirectories.push_back(exeDir+"/data/textures");
}
#endif


  // create vulkan instance with required extensions
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

void Viewer::run()
{
  std::thread renderThread([&]
  {
    while (true)
    {
      {
        std::lock_guard<std::mutex> lck(updateMutex);
        renderIndex      = getNextRenderSlot();
        renderStartTime  = HPClock::now();
        updateConditionVariable.notify_one();
      }
      //switch (renderIndex)
      //{
      //case 0:
      //  LOG_INFO << "R:+  " << inSeconds(getRenderTimeDelta()) << std::endl; break;
      //case 1:
      //  LOG_INFO << "R: + " << inSeconds(getRenderTimeDelta()) << std::endl; break;
      //case 2:
      //  LOG_INFO << "R:  +" << inSeconds(getRenderTimeDelta()) << std::endl; break;
      //}
      bool continueRun = true;
      try
      {
        continueRun = !terminating();
        if (continueRun)
        {
          startRenderGraph.try_put(tbb::flow::continue_msg());
          renderGraph.wait_for_all();
        }
      }
      catch (...)
      {
        continueRun = false;
      }

      auto renderEndTime = HPClock::now();
      lastRenderDuration = renderEndTime - renderStartTime;
      if (!continueRun)
        break;
    }
  }
  );
  while (true)
  {
    {
      std::unique_lock<std::mutex> lck(updateMutex);
      updateConditionVariable.wait(lck, [&] { return renderStartTime > updateStartTimes[updateIndex]; });
      auto prevUpdateIndex = updateIndex;
      updateIndex = getNextUpdateSlot();
      updateStartTimes[updateIndex] = updateStartTimes[prevUpdateIndex] + HPClock::duration(std::chrono::seconds(1)) / viewerTraits.updatesPerSecond;
    }
    //switch (updateIndex)
    //{
    //case 0:
    //  LOG_INFO << "U:*  " << std::endl; break;
    //case 1:
    //  LOG_INFO << "U: * " << std::endl; break;
    //case 2:
    //  LOG_INFO << "U:  *" << std::endl; break;
    //}
    auto realUpdateStartTime = HPClock::now();

    bool continueRun = true;
#if defined(_WIN32)
    continueRun = WindowWin32::checkWindowMessages();
#elif defined (__linux__)
    continueRun = WindowXcb::checkWindowMessages();
#endif

    if (continueRun)
    {
      try
      {
        startUpdateGraph.try_put(tbb::flow::continue_msg());
        updateGraph.wait_for_all();
      }
      catch (...)
      {
        continueRun = false;
      }
    }

    auto realUpdateEndTime = HPClock::now();
    lastUpdateDuration = realUpdateEndTime - realUpdateStartTime;
    if (!continueRun)
      break;
  }
  renderThread.join();
  for (auto& d : devices)
    vkDeviceWaitIdle(d->device);
}

void Viewer::cleanup()
{
  updateGraph.reset();
  renderGraph.reset();
  if (instance != VK_NULL_HANDLE)
  {
    for( auto s : surfaces )
      s->cleanup();
    surfaces.clear();
    for (auto d : devices)
      d->cleanup();
    devices.clear();
    physicalDevices.clear();
    if (viewerTraits.useValidation)
      cleanupDebugging();
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

void Viewer::setTerminate()
{
  viewerTerminate = true;
}


std::shared_ptr<Device> Viewer::addDevice(unsigned int physicalDeviceIndex, const std::vector<QueueTraits>& requestedQueues, const std::vector<const char*>& requestedExtensions)
{
  CHECK_LOG_THROW(physicalDeviceIndex >= physicalDevices.size(), "Could not create device. Index is too high : " << physicalDeviceIndex);
  CHECK_LOG_THROW(requestedQueues.empty(), "Could not create device with no queues");

  std::shared_ptr<Device> device = std::make_shared<Device>(shared_from_this(), physicalDevices[physicalDeviceIndex], requestedQueues, requestedExtensions);
  devices.push_back(device);
  return device;
}

std::shared_ptr<Surface> Viewer::addSurface(std::shared_ptr<Window> window, std::shared_ptr<Device> device, const pumex::SurfaceTraits& surfaceTraits)
{
  std::shared_ptr<Surface> surface = window->createSurface(shared_from_this(), device, surfaceTraits);
  surfaces.push_back(surface);
  return surface;
}

std::string Viewer::getFullFilePath(const std::string& shortFileName) const
{
  struct stat buf;
  for ( const auto& d : defaultDirectories )
  {
#if defined(_WIN32)
    std::string fullFilePath( d + "\\" + shortFileName );
#else
    std::string fullFilePath(d + "/" + shortFileName);
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

  VkDebugReportCallbackCreateInfoEXT dbgCreateInfo{};
    dbgCreateInfo.sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)messageCallback;
    dbgCreateInfo.flags       = flags;
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
  return VK_FALSE;
}
