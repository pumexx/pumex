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
#include <pumex/utils/Log.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Device.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/RenderWorkflow.h>
#include <pumex/Version.h>
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

ViewerTraits::ViewerTraits(const std::string& aName, const std::vector<std::string>& rie, const std::vector<std::string>& rdl, uint32_t ups)
  : applicationName{ aName }, requestedInstanceExtensions{ rie }, requestedDebugLayers{ rdl }, updatesPerSecond{ ups }
{
}

const uint32_t MAX_PATH_LENGTH = 256;

Viewer::Viewer(const ViewerTraits& vt)
  : viewerTraits{ vt }, 
  opStartUpdateGraph            { updateGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  opEndUpdateGraph              { updateGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  opRenderGraphStart            { renderGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  opRenderGraphEventRenderStart { renderGraph, [=](tbb::flow::continue_msg) { onEventRenderStart(); } },
  opRenderGraphFinish           { renderGraph, [=](tbb::flow::continue_msg) { onEventRenderFinish(); } }
{
  viewerStartTime     = HPClock::now();
  for(uint32_t i=0; i<3;++i)
    updateStartTimes[i] = viewerStartTime;
  renderStartTime     = viewerStartTime;
  lastRenderDuration  = viewerStartTime - renderStartTime;
  lastUpdateDuration  = viewerStartTime - updateStartTimes[0];

  // register basic directories - directories listed in PUMEX_DATA_DIR environment variable, separated by colon or semicolon
  const char* dataDirVariable = std::getenv("PUMEX_DATA_DIR");
  if (dataDirVariable != nullptr)
  {
    const char* currentPos = dataDirVariable;
    do
    {
      const char *beginPos = currentPos;
      while (*currentPos != ';' && *currentPos != ':' && *currentPos != 0)
        currentPos++;
      std::error_code ec;
      filesystem::path currentPath(std::string(beginPos, currentPos));
      addDefaultDirectory(currentPath);
    } while (*currentPos++ != 0);
  }

  // register basic directories - current directory
  char strCurrentPath[MAX_PATH_LENGTH];
  if (getcwd(strCurrentPath, MAX_PATH_LENGTH))
  {
    filesystem::path currentDir(strCurrentPath);
    addDefaultDirectory(currentDir);
    addDefaultDirectory(currentDir / filesystem::path("data") );
    addDefaultDirectory(currentDir / filesystem::path("../data"));
    addDefaultDirectory(currentDir / filesystem::path("../../data"));
  }
  // register basic directories - executable directory and data directory
  char strExecPath[MAX_PATH_LENGTH];
  filesystem::path execDir;
#if defined(_WIN32)
  GetModuleFileNameA(NULL, strExecPath, MAX_PATH_LENGTH);
  execDir = strExecPath;
#else
  {
    char id[MAX_PATH_LENGTH];
    sprintf(id, "/proc/%d/exe", getpid());
    ssize_t size = readlink(id, strExecPath, MAX_PATH_LENGTH-1);
    if ((size < 0) || (size >= MAX_PATH_LENGTH))
      size = 0;
    strExecPath[size] = '\0';
  }
  execDir = strExecPath;
#endif
  addDefaultDirectory(execDir);
  addDefaultDirectory(execDir / filesystem::path("data"));
  // for files INSTALLED on Windows 
#if defined(_WIN32)
  addDefaultDirectory(execDir / filesystem::path("../share/pumex"));
#else
  // for files INSTALLED on Linux
  addDefaultDirectory(filesystem::path("/usr/share/pumex"));
  addDefaultDirectory(filesystem::path("/usr/local/share/pumex"));
#endif  

  //// list all existing default directories
  //LOG_INFO << "Default directories :" << std::endl;
  //for(auto& d : defaultDirectories)
  //  LOG_INFO << d << std::endl;
  
  // create vulkan instance with required extensions
  enabledInstanceExtensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );
#if defined(_WIN32)
  enabledInstanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
  enabledInstanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
  XInitThreads();
#elif defined(__ANDROID__)
  enabledInstanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
  if (viewerTraits.useDebugLayers())
    enabledInstanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

  // add instance extensions requested by user
  for( const auto& extension : viewerTraits.requestedInstanceExtensions )
    enabledInstanceExtensions.push_back(extension.c_str());

  VkApplicationInfo applicationInfo{};
    applicationInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName   = viewerTraits.applicationName.c_str();
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName        = "pumex";
    applicationInfo.engineVersion      = 10000 * PUMEX_VERSION_MAJOR + 100 * PUMEX_VERSION_MINOR + PUMEX_VERSION_PATCH;
    applicationInfo.apiVersion         = VK_API_VERSION_1_0;

  VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo        = &applicationInfo;
    instanceCreateInfo.enabledExtensionCount   = static_cast<uint32_t>(enabledInstanceExtensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = enabledInstanceExtensions.data();

  if (viewerTraits.useDebugLayers())
  {
    for (const auto& layer : viewerTraits.requestedDebugLayers)
      enabledDebugLayers.push_back(layer.c_str());
    instanceCreateInfo.enabledLayerCount   = static_cast<uint32_t>(enabledDebugLayers.size());
    instanceCreateInfo.ppEnabledLayerNames = enabledDebugLayers.data();
  }

  VK_CHECK_LOG_THROW(vkCreateInstance(&instanceCreateInfo, nullptr, &instance), "Cannot create instance");

  if (viewerTraits.useDebugLayers())
    setupDebugging(viewerTraits.debugReportFlags, viewerTraits.debugReportCallback);

  // collect all available physical devices
  uint32_t deviceCount = 0;
  VK_CHECK_LOG_THROW(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "Cannot enumerate physical devices");
  std::vector<VkPhysicalDevice> phDevices(deviceCount);
  VK_CHECK_LOG_THROW(vkEnumeratePhysicalDevices(instance, &deviceCount, phDevices.data()), "Cannot enumerate physical devices " << deviceCount );

  for (auto phDev : phDevices)
    physicalDevices.push_back(std::make_shared<PhysicalDevice>(phDev, this));

  // collect information about all implemented extensions
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

  bool renderContinueRun = true;
  bool updateContinueRun = true;
  std::exception_ptr exceptionCaught;

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
      try
      {
        frameNumber++;
        renderContinueRun = !terminating();
        if (renderContinueRun)
        {
          opRenderGraphStart.try_put(tbb::flow::continue_msg());
          renderGraph.wait_for_all();
        }
      }
      catch (...)
      {
        exceptionCaught = std::current_exception();
        renderContinueRun = false;
        updateConditionVariable.notify_one();
      }
      if (!renderContinueRun || !updateContinueRun)
      {
        for (auto& d : devices)
          vkDeviceWaitIdle(d.second->device);
        break;
      }

      auto renderEndTime = HPClock::now();
      lastRenderDuration = renderEndTime - renderStartTime;
    }
  }
  );
  while (true)
  {
    {
      std::unique_lock<std::mutex> lck(updateMutex);
      updateConditionVariable.wait(lck, [&] { return renderStartTime > updateStartTimes[updateIndex] || !renderContinueRun; });
      if (!renderContinueRun)
        break;
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

#if defined(_WIN32)
    updateContinueRun = WindowWin32::checkWindowMessages();
#elif defined (__linux__)
    updateContinueRun = WindowXcb::checkWindowMessages();
#endif

    if (updateContinueRun)
    {
      try
      {
        opStartUpdateGraph.try_put(tbb::flow::continue_msg());
        updateGraph.wait_for_all();
      }
      catch (...)
      {
        exceptionCaught = std::current_exception();
        updateContinueRun = false;
      }
    }
    auto realUpdateEndTime = HPClock::now();
    lastUpdateDuration = realUpdateEndTime - realUpdateStartTime;
    if (!renderContinueRun || !updateContinueRun)
      break;
  }
  renderThread.join();
  if (exceptionCaught)
    std::rethrow_exception(exceptionCaught);
}

void Viewer::cleanup()
{
  eventRenderStart  = nullptr;
  eventRenderFinish = nullptr;
  updateGraph.reset();
  renderGraph.reset();
  if (instance != VK_NULL_HANDLE)
  {
    if (isRealized())
    {
      for (auto& s : surfaces)
        s.second->cleanup();
    }
    surfaces.clear();
    windows.clear();
    if (isRealized())
    {
      for (auto& d : devices)
        d.second->cleanup();
    }
    devices.clear();
    physicalDevices.clear();
    if (viewerTraits.useDebugLayers())
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
  for (auto& d : devices)
    d.second->resetRequestedQueues();
  for (auto& s : surfaces)
  {
    auto device = s.second->device.lock();
    for (auto& qt : s.second->renderWorkflow->getQueueTraits())
      device->addRequestedQueue(qt);
  }
  for (auto& d : devices)
    d.second->realize();
  for (auto& s : surfaces)
    s.second->realize();

  realized = true;
}

void Viewer::setTerminate()
{
  viewerTerminate = true;
}

std::shared_ptr<Device> Viewer::addDevice(unsigned int physicalDeviceIndex, const std::vector<std::string>& requestedExtensions)
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
  if (it == end(devices))
    return nullptr;
  return it->second.get();
}

Surface* Viewer::getSurface(uint32_t id)
{
  auto it = surfaces.find(id);
  if (it == end(surfaces))
    return nullptr;
  return it->second.get();
}

void Viewer::addDefaultDirectory(const filesystem::path & directory) 
{
  std::error_code ec;
  filesystem::path canonicalPath = filesystem::canonical(directory, ec);
  // skip directory if it's not a canonnical path
  if(ec.value() != 0)
    return;
//  CHECK_LOG_RETURN_VOID(ec.value() != 0, "Viewer::addDefaultDirectory() : Cannot create cannonical path from " << directory << " Error message : " << ec.message());
  // skip directory if it already exists in defaultDirectories
  if (std::find(begin(defaultDirectories), end(defaultDirectories), canonicalPath) != end(defaultDirectories))
    return;
  // skip it if it's not a directory ( it may not even exist on disk )
  if (!filesystem::is_directory(canonicalPath))
    return;
  CHECK_LOG_RETURN_VOID(!canonicalPath.is_absolute(), "Viewer::addDefaultDirectory() : Default directory must be absolute : " << directory);
  defaultDirectories.push_back(canonicalPath);
}

filesystem::path Viewer::getAbsoluteFilePath(const filesystem::path& relativeFilePath) const
{
  for (auto directory : defaultDirectories)
  {
    filesystem::path targetPath = directory / relativeFilePath;
    if (filesystem::exists(targetPath))
      return targetPath;
  }
  return filesystem::path();
}

bool Viewer::instanceExtensionImplemented(const char* extensionName) const
{
  for (const auto& e : extensionProperties)
    if (!std::strcmp(extensionName, e.extensionName))
      return true;
  return false;
}

bool Viewer::instanceExtensionEnabled(const char* extensionName) const
{
  for (const auto& e : enabledInstanceExtensions)
    if (!std::strcmp(extensionName, e))
      return true;
  return false;
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

  opSurfaceBeginFrame.clear();
  opSurfaceEventRenderStart.clear();
  opSurfaceValidateWorkflow.clear();
  opSurfaceValidateSecondaryNodes.clear();
  opSurfaceBarrier0.clear(); 
  opSurfaceValidateSecondaryDescriptors.clear(); 
  opSurfaceSecondaryCommandBuffers.clear(); 
  opSurfaceDrawFrame.clear(); 
  opSurfaceEndFrame.clear();

  opSurfaceValidatePrimaryNodes.clear();
  opSurfaceValidatePrimaryDescriptors.clear();
  opSurfacePrimaryBuffers.clear();

  std::vector<Surface*> surfacePointers;
  for (auto& surf : surfaces)
  {
    Surface* surface = surf.second.get();
    surfacePointers.emplace_back(surface);
    opSurfaceBeginFrame.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->beginFrame();
    });
    opSurfaceEventRenderStart.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->onEventSurfaceRenderStart();
    });
    opSurfaceValidateWorkflow.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->validateWorkflow();
    });
    {
      auto jit = opSurfaceValidatePrimaryNodes.find(surface);
      if (jit == end(opSurfaceValidatePrimaryNodes))
        jit = opSurfaceValidatePrimaryNodes.insert({ surface, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>() }).first;
      for (uint32_t i = 0; i < surface->queues.size(); ++i)
      {
        jit->second.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
        {
          surface->validatePrimaryNodes(i);
        });
      }
    }
    {
      auto jit = opSurfaceValidatePrimaryDescriptors.find(surface);
      if (jit == end(opSurfaceValidatePrimaryDescriptors))
        jit = opSurfaceValidatePrimaryDescriptors.insert({ surface, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>() }).first;
      for (uint32_t i = 0; i < surface->queues.size(); ++i)
      {
        jit->second.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
        {
          surface->validatePrimaryDescriptors(i);
        });
      }
    }
    {
      auto jit = opSurfacePrimaryBuffers.find(surface);
      if (jit == end(opSurfacePrimaryBuffers))
        jit = opSurfacePrimaryBuffers.insert({ surface, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>() }).first;
      for (uint32_t i = 0; i < surface->queues.size(); ++i)
      {
        jit->second.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
        {
          surface->buildPrimaryCommandBuffer(i);
        });
      }
    }
    opSurfaceValidateSecondaryNodes.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->validateSecondaryNodes();
    });
    opSurfaceBarrier0.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      doNothing();
    });
    opSurfaceValidateSecondaryDescriptors.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->validateSecondaryDescriptors();
    });
    opSurfaceSecondaryCommandBuffers.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->setCommandBufferIndices();
      surface->buildSecondaryCommandBuffers();
    });
    opSurfaceDrawFrame.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->draw();
    });
    opSurfaceEndFrame.emplace_back(renderGraph, [=](tbb::flow::continue_msg)
    {
      surface->endFrame();
      surface->onEventSurfaceRenderFinish();
    });
  }

  tbb::flow::make_edge(opRenderGraphStart, opRenderGraphEventRenderStart);
  for (uint32_t i = 0; i < surfacePointers.size(); ++i)
  {
    tbb::flow::make_edge(opRenderGraphStart, opSurfaceBeginFrame[i]);
    tbb::flow::make_edge(opRenderGraphStart, opSurfaceEventRenderStart[i]);
    
    tbb::flow::make_edge(opSurfaceBeginFrame[i], opSurfaceValidateWorkflow[i]);
    tbb::flow::make_edge(opSurfaceEventRenderStart[i], opSurfaceValidateWorkflow[i]);
    tbb::flow::make_edge(opRenderGraphEventRenderStart, opSurfaceValidateWorkflow[i]);

    tbb::flow::make_edge(opSurfaceValidateWorkflow[i], opSurfaceValidateSecondaryNodes[i]);
    tbb::flow::make_edge(opSurfaceValidateSecondaryNodes[i], opSurfaceBarrier0[i]);
    tbb::flow::make_edge(opSurfaceBarrier0[i], opSurfaceValidateSecondaryDescriptors[i]);
    tbb::flow::make_edge(opSurfaceValidateSecondaryDescriptors[i], opSurfaceSecondaryCommandBuffers[i]);

    auto jit0 = opSurfaceValidatePrimaryNodes.find(surfacePointers[i]);
    if (jit0 == end(opSurfaceValidatePrimaryNodes) || jit0->second.size() == 0)
    {
      // no primary command buffer building ? Maybe we should throw an error ?
      tbb::flow::make_edge(opSurfaceValidateWorkflow[i], opSurfaceSecondaryCommandBuffers[i]);
      tbb::flow::make_edge(opSurfaceSecondaryCommandBuffers[i], opSurfaceDrawFrame[i]);
    }
    else
    {
      auto jit1 = opSurfaceValidatePrimaryDescriptors.find(surfacePointers[i]);
      auto jit2 = opSurfacePrimaryBuffers.find(surfacePointers[i]);

      for (uint32_t j = 0; j < jit0->second.size(); ++j)
      {
        tbb::flow::make_edge(opSurfaceValidateWorkflow[i], jit0->second[j]);
        tbb::flow::make_edge(jit0->second[j], opSurfaceBarrier0[i]);
        tbb::flow::make_edge(opSurfaceBarrier0[i], jit1->second[j]);
        tbb::flow::make_edge(jit1->second[j], opSurfaceSecondaryCommandBuffers[i]);
        tbb::flow::make_edge(opSurfaceSecondaryCommandBuffers[i], jit2->second[j]);
        tbb::flow::make_edge(jit2->second[j], opSurfaceDrawFrame[i]);
      }
    }

    tbb::flow::make_edge(opSurfaceDrawFrame[i], opSurfaceEndFrame[i]);
    tbb::flow::make_edge(opSurfaceEndFrame[i], opRenderGraphFinish);
  }
}

// put a breakpoint inside this function if you want to see what code generated layer error
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
