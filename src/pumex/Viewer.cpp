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

#include <pumex/Viewer.h>
#include <algorithm>
#include <map>
#include <pumex/utils/Log.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Device.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/RenderWorkflow.h>
#if defined(_WIN32)
  #include <pumex/platform/win32/WindowWin32.h>
  #include <direct.h>
  #define getcwd _getcwd
#elif defined(__linux__)
  #include <pumex/platform/linux/WindowXcb.h>
  #include <X11/Xlib.h>
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
  renderGraphStart { renderGraph, [=](tbb::flow::continue_msg) { onEventRenderStart(); } },
  renderGraphFinish{ renderGraph, [=](tbb::flow::continue_msg) { onEventRenderFinish(); } }
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
      while (*currentPos != ';' && *currentPos != ':' && *currentPos != 0)
        currentPos++;
      addDefaultDirectory( std::string(begin, currentPos) );
    } while (*currentPos++ != 0);
  }

  // register basic directories - current directory
  char strCurrentPath[MAX_PATH_LENGTH];
  if (getcwd(strCurrentPath, MAX_PATH_LENGTH))
  {
    std::string currentDir(strCurrentPath);
    addDefaultDirectory(currentDir);
    currentDir = currentDir.substr(0, currentDir.find_last_of("/"));
    addDefaultDirectory(currentDir + "/data");
    addDefaultDirectory(currentDir + "../data");
    addDefaultDirectory(currentDir + "../../data");
  }
  // register basic directories - executable directory and data directory
  char strExePath[MAX_PATH_LENGTH];
#if defined(_WIN32)
  GetModuleFileNameA(NULL, strExePath, MAX_PATH_LENGTH);
  std::string exeDir = strExePath;
  exeDir = exeDir.substr(0, exeDir.find_last_of("\\"));
  addDefaultDirectory(exeDir);
  addDefaultDirectory(exeDir + "\\data");
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
    addDefaultDirectory(exeDir);
    addDefaultDirectory(exeDir+"/data");
  }
#endif

  // create vulkan instance with required extensions
  std::vector<const char*> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };
#if defined(_WIN32)
  enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
  enabledExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
  XInitThreads();
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
  if (!isRealized())
    realize();

  std::thread renderThread([&]
  {
    while (true)
    {
      if (!renderGraphValid)
      {
        buildRenderGraph();
        renderGraphValid = true;
      }

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
        frameNumber++;
        continueRun = !terminating();
        if (continueRun)
        {
          renderGraphStart.try_put(tbb::flow::continue_msg());
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
    vkDeviceWaitIdle(d.second->device);
}

void Viewer::cleanup()
{
  updateGraph.reset();
  renderGraph.reset();
  if (instance != VK_NULL_HANDLE)
  {
    if (isRealized())
    {
      for (auto s : surfaces)
        s.second->cleanup();
    }
    surfaces.clear();
    windows.clear();
    if (isRealized())
    {
      for (auto d : devices)
        d.second->cleanup();
    }
    devices.clear();
    physicalDevices.clear();
    if (viewerTraits.useValidation)
      cleanupDebugging();
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

void Viewer::realize()
{
  if (isRealized())
    return;

  // collect queues that are requested by surface workflows
  for (auto d : devices)
    d.second->resetRequestedQueues();
  for (auto s : surfaces)
  {
    auto device = s.second->device.lock();
    for (auto qt : s.second->renderWorkflow->queueTraits)
      device->addRequestedQueue(qt);
  }
  for (auto d : devices)
    d.second->realize();
  for (auto s : surfaces)
    s.second->realize();

  realized = true;
}

void Viewer::setTerminate()
{
  viewerTerminate = true;
}

std::shared_ptr<Device> Viewer::addDevice(unsigned int physicalDeviceIndex, const std::vector<const char*>& requestedExtensions)
{
  CHECK_LOG_THROW(physicalDeviceIndex >= physicalDevices.size(), "Could not create device. Index is too high : " << physicalDeviceIndex);

  std::shared_ptr<Device> device = std::make_shared<Device>(shared_from_this(), physicalDevices[physicalDeviceIndex], requestedExtensions);
  device->setID(nextDeviceID);
  devices.insert({ nextDeviceID++, device });
  return device;
}

std::shared_ptr<Surface> Viewer::addSurface(std::shared_ptr<Window> window, std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits)
{
  std::shared_ptr<Surface> surface = window->createSurface(shared_from_this(), device, surfaceTraits);
  surface->setID(nextSurfaceID);
  surfaces.insert({ nextSurfaceID++, surface });
  windows.push_back(window);
  return surface;
}

Device*  Viewer::getDevice(uint32_t id)
{
  auto it = devices.find(id);
  if (it == devices.end())
    return nullptr;
  return it->second.get();
}

Surface* Viewer::getSurface(uint32_t id)
{
  auto it = surfaces.find(id);
  if (it == surfaces.end())
    return nullptr;
  return it->second.get();
}

std::string Viewer::getFullFilePath(const std::string& shortFileName) const
{
  struct stat buf;
  for ( auto d : defaultDirectories )
  {
#if defined(_WIN32)
    std::replace(d.begin(), d.end(), '/', '\\');
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

void Viewer::buildRenderGraph()
{
  renderGraph.reset();

  std::vector<Surface*> surfacePointers;
  std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>> startSurfaceFrame, drawSurfaceFrame, endSurfaceFrame;
  std::map<Surface*, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>> primaryBuffers;
  for (auto& surf : surfaces)
  {
    Surface* surface = surf.second.get();
    surfacePointers.emplace_back(surface);
    startSurfaceFrame.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->onEventSurfaceRenderStart();
      bool workflowCompiledNow = surface->renderWorkflow->compile();
    });
    auto jit = primaryBuffers.find(surface);
    if(jit == primaryBuffers.end())
      jit = primaryBuffers.insert({ surface, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>() }).first;

    for (uint32_t i = 0; i < surface->queues.size(); ++i)
    {
      jit->second.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
      {
        surface->buildPrimaryCommandBuffer(i);
      });
    }
    drawSurfaceFrame.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->beginFrame();
      surface->draw();
    });
    endSurfaceFrame.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->endFrame();
      surface->onEventSurfaceRenderFinish();
    });
  }

  for (uint32_t i = 0; i < surfacePointers.size(); ++i)
  {
    tbb::flow::make_edge(renderGraphStart, startSurfaceFrame[i]);
    auto jit = primaryBuffers.find(surfacePointers[i]);
    if (jit == primaryBuffers.end() || jit->second.size() == 0)
    {
      // no command buffer building ? Maybe we should throw an error ?
      tbb::flow::make_edge(startSurfaceFrame[i], drawSurfaceFrame[i]);
    }
    else
    {
      for (uint32_t j = 0; j < jit->second.size(); ++j)
      {
        tbb::flow::make_edge(startSurfaceFrame[i], jit->second[j]);
        tbb::flow::make_edge(jit->second[j], drawSurfaceFrame[i]);
      }
    }
    tbb::flow::make_edge(drawSurfaceFrame[i], endSurfaceFrame[i]);
    tbb::flow::make_edge(endSurfaceFrame[i], renderGraphFinish);
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
