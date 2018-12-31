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
#include <cctype>
#include <pumex/utils/Log.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Device.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/RenderGraphCompiler.h>
#include <pumex/TimeStatistics.h>
#include <pumex/InputEvent.h>
#include <pumex/Asset.h>
#include <pumex/Image.h>
#include <pumex/AssetLoaderAssimp.h>
#include <pumex/TextureLoaderGli.h>
#include <pumex/Version.h>
#if defined(PUMEX_BUILD_TEXTURE_LOADERS)
  #include <pumex/TextureLoaderPNG.h>
  #include <pumex/TextureLoaderJPEG.h>
#endif
#if defined(VK_USE_PLATFORM_WIN32_KHR)
  #include <pumex/platform/win32/WindowWin32.h>
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
  #include <pumex/platform/linux/WindowXcb.h>
  #include <X11/Xlib.h>
  #include <unistd.h>
#endif

using namespace pumex;

ViewerTraits::ViewerTraits(const std::string& aName, const std::vector<std::string>& rie, const std::vector<std::string>& rdl, uint32_t ups)
  : applicationName{ aName }, requestedInstanceExtensions{ rie }, requestedDebugLayers{ rdl }, updatesPerSecond{ ups }
{
}

const uint32_t MAX_PATH_LENGTH = 256;

Viewer::Viewer(const ViewerTraits& vt)
  : viewerTraits{ vt },
  opStartUpdateGraph                   { updateGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  opEndUpdateGraph                     { updateGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  opExecutionFlowGraphStart            { executionFlowGraph, [=](tbb::flow::continue_msg) { doNothing(); } },
  opExecutionFlowGraphEventRenderStart { executionFlowGraph, [=](tbb::flow::continue_msg) { onEventRenderStart(); } },
  opExecutionFlowGraphFinish           { executionFlowGraph, [=](tbb::flow::continue_msg) { onEventRenderFinish(); } }
{
  externalMemoryObjects = std::make_shared<ExternalMemoryObjects>();
  renderGraphCompiler = std::dynamic_pointer_cast<RenderGraphCompiler>(std::make_shared<DefaultRenderGraphCompiler>());
  viewerStartTime     = HPClock::now();

  for(uint32_t i=0; i<3;++i)
    updateTimes[i] = viewerStartTime;
  renderStartTime     = viewerStartTime;
  timeStatistics = std::make_unique<TimeStatistics>(32);
  timeStatistics->registerGroup(TSV_GROUP_UPDATE, L"Update operations");
  timeStatistics->registerGroup(TSV_GROUP_RENDER, L"Render operation");
  timeStatistics->registerGroup(TSV_GROUP_RENDER_EVENTS, L"Render events");
  timeStatistics->registerChannel(TSV_CHANNEL_INPUTEVENTS,         TSV_GROUP_UPDATE,        L"Input events",               glm::vec4(0.8f, 0.8f, 0.1f, 0.5f));
  timeStatistics->registerChannel(TSV_CHANNEL_UPDATE,              TSV_GROUP_UPDATE,        L"Full update",                glm::vec4(0.8f, 0.1f, 0.1f, 0.5f));
  timeStatistics->registerChannel(TSV_CHANNEL_RENDER,              TSV_GROUP_RENDER,        L"Full render",                glm::vec4(0.1f, 0.1f, 0.8f, 0.5f));
  timeStatistics->registerChannel(TSV_CHANNEL_FRAME,               TSV_GROUP_RENDER,        L"Frame time",                 glm::vec4(0.5f, 0.5f, 0.5f, 0.5f));
  timeStatistics->registerChannel(TSV_CHANNEL_EVENT_RENDER_START,  TSV_GROUP_RENDER_EVENTS, L"Viewer event render start",  glm::vec4(0.8f, 0.8f, 0.1f, 0.5f));
  timeStatistics->registerChannel(TSV_CHANNEL_EVENT_RENDER_FINISH, TSV_GROUP_RENDER_EVENTS, L"Viewer event render finish", glm::vec4(0.8f, 0.1f, 0.1f, 0.5f));
  timeStatistics->setFlags(TSV_STAT_UPDATE | TSV_STAT_RENDER | TSV_STAT_RENDER_EVENTS);

  // register basic directories - directories listed in PUMEX_DATA_DIR environment variable, separated by colon or semicolon
  const char* dataDirVariable = std::getenv("PUMEX_DATA_DIR");
  if (dataDirVariable != nullptr)
  {
    const char* currentPos = dataDirVariable;
    do
    {
      const char *beginPos = currentPos;
#if defined(_WIN32)
      while (*currentPos != ';' && *currentPos != 0)
        currentPos++;
#else
      while (*currentPos != ';' && *currentPos != ':' && *currentPos != 0)
        currentPos++;
#endif
      std::error_code ec;
      filesystem::path currentPath(std::string(beginPos, currentPos));
      addDefaultDirectory(currentPath);
    } while (*currentPos++ != 0);
  }

  // register basic directories - current directory
  filesystem::path currentDir = filesystem::current_path();
  addDefaultDirectory(currentDir);
  addDefaultDirectory(currentDir / filesystem::path("data") );
  addDefaultDirectory(currentDir / filesystem::path("../data"));
  addDefaultDirectory(currentDir / filesystem::path("../../data"));
  addDefaultDirectory(currentDir / filesystem::path("../../../data"));

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
  if (execDir.has_filename())
    execDir.remove_filename();
  addDefaultDirectory(execDir);
  addDefaultDirectory(execDir / filesystem::path("data"));
  addDefaultDirectory(execDir / filesystem::path("../"));
#if defined(_WIN32)
  // for files INSTALLED on Windows
  addDefaultDirectory(execDir / filesystem::path("../share/pumex"));
#else
  // for files INSTALLED on Linux
  addDefaultDirectory(filesystem::path("/usr/share/pumex"));
  addDefaultDirectory(filesystem::path("/usr/local/share/pumex"));
#endif

////// list all existing default directories
//  LOG_INFO << "Default directories :" << std::endl;
//  for(auto& d : defaultDirectories)
//    LOG_INFO << d << std::endl;

  // collect asset and texture loaders
  auto assimpLoader = std::make_shared<AssetLoaderAssimp>();
  assimpLoader->setImportFlags(assimpLoader->getImportFlags() | aiProcess_CalcTangentSpace);
  assetLoaders.push_back(assimpLoader);

  textureLoaders.push_back(std::make_shared<TextureLoaderGli>());
  // load optional texture loaders
#if defined(PUMEX_BUILD_TEXTURE_LOADERS)
  textureLoaders.push_back(std::make_shared<TextureLoaderPNG>());
  textureLoaders.push_back(std::make_shared<TextureLoaderJPEG>());
#endif
  // create vulkan instance with required extensions
  enabledInstanceExtensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );
#if defined(VK_USE_PLATFORM_WIN32_KHR)
  enabledInstanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
  enabledInstanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
  XInitThreads();
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
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

  loadExtensionFunctions();
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

  std::thread renderThread([&]
  {
    while (true)
    {
      std::lock_guard<std::mutex> renderLock(renderMutex);
      if (!executionFlowGraphValid)
      {
        buildExecutionFlowGraph();
        executionFlowGraphValid = true;
      }

      for (auto& it : surfaces)
        it.second->onEventSurfacePrepareStatistics(timeStatistics.get());

      auto prevRenderStartTime = renderStartTime;
      {
        std::lock_guard<std::mutex> lck(updateMutex);
        renderIndex      = getNextRenderSlot();
        renderStartTime  = HPClock::now();
        updateConditionVariable.notify_one();
      }
      //switch (renderIndex)
      //{
      //case 0:
      //  LOG_INFO << "R:+   " << inSeconds(getRenderTimeDelta()) << std::endl; break;
      //case 1:
      //  LOG_INFO << "R: +  " << inSeconds(getRenderTimeDelta()) << std::endl; break;
      //case 2:
      //  LOG_INFO << "R:  + " << inSeconds(getRenderTimeDelta()) << std::endl; break;
      //}
      try
      {
        frameNumber++;
        renderContinueRun = !terminating();
        if (renderContinueRun)
        {
          opExecutionFlowGraphStart.try_put(tbb::flow::continue_msg());
          executionFlowGraph.wait_for_all();
        }
      }
      catch (...)
      {
        exceptionCaught = std::current_exception();
        renderContinueRun = false;
        updateConditionVariable.notify_one();
      }

      if (timeStatistics->hasFlags(TSV_STAT_RENDER))
      {
        auto renderEndTime = HPClock::now();
        timeStatistics->setValues(TSV_CHANNEL_RENDER, inSeconds(renderStartTime - viewerStartTime), inSeconds(renderEndTime - renderStartTime));
        timeStatistics->setValues(TSV_CHANNEL_FRAME, inSeconds(prevRenderStartTime - viewerStartTime), inSeconds(renderStartTime - prevRenderStartTime));
      }

      if (!renderContinueRun || !updateContinueRun)
      {
        for (auto& d : devices)
          vkDeviceWaitIdle(d.second->device);
        break;
      }
    }
  }
  );
  while (true)
  {
    {
      std::unique_lock<std::mutex> lck(updateMutex);
      updateConditionVariable.wait(lck, [&] { return renderStartTime > updateTimes[updateIndex] || !renderContinueRun; });
      if (!renderContinueRun)
        break;
      prevUpdateIndex          = updateIndex;
      updateIndex              = getNextUpdateSlot();
      updateInProgress         = true;
      updateTimes[updateIndex] = updateTimes[prevUpdateIndex] + getUpdateDuration();
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
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    updateContinueRun = WindowWin32::checkWindowMessages();
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
    updateContinueRun = WindowXcb::checkWindowMessages();
#endif

    if (updateContinueRun)
    {
      try
      {
        HPClock::time_point tickStart;
        if (timeStatistics->hasFlags(TSV_STAT_UPDATE))
          tickStart = HPClock::now();

        handleInputEvents();

        if (timeStatistics->hasFlags(TSV_STAT_UPDATE))
        {
          auto tickEnd = HPClock::now();
          timeStatistics->setValues(TSV_CHANNEL_INPUTEVENTS, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
        }

        if (timeStatistics->hasFlags(TSV_STAT_UPDATE))
          tickStart = HPClock::now();

        opStartUpdateGraph.try_put(tbb::flow::continue_msg());
        updateGraph.wait_for_all();

        if (timeStatistics->hasFlags(TSV_STAT_UPDATE))
        {
          auto tickEnd = HPClock::now();
          timeStatistics->setValues(TSV_CHANNEL_UPDATE, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
        }
        updateInProgress = false;
      }
      catch (...)
      {
        exceptionCaught   = std::current_exception();
        updateInProgress   = false;
        updateContinueRun = false;
      }
    }
    if (!renderContinueRun || !updateContinueRun)
      break;
  }
  renderThread.join();
  if (exceptionCaught)
    std::rethrow_exception(exceptionCaught);
}

void Viewer::cleanup()
{
  inputEventHandlers.clear();
  eventRenderStart  = nullptr;
  eventRenderFinish = nullptr;
  updateGraph.reset();
  executionFlowGraph.reset();
  renderGraphs.clear();
  externalMemoryObjects = nullptr;
  frameBufferAllocator = nullptr;
  if (instance != VK_NULL_HANDLE)
  {
    if (isRealized())
    {
      for (auto& s : surfaces)
        s.second->cleanup();
    }
    surfaces.clear();
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

  // collect queues that are requested by render graphs
  for (auto& d : devices)
    d.second->resetRequestedQueues();
  for (auto& s : surfaces)
    s.second->collectQueueTraits();
  for (auto& d : devices)
    d.second->realize();
  for (auto& s : surfaces)
    s.second->realize();

  renderContinueRun = true;
  updateContinueRun = true;
  realized          = true;
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

void Viewer::addSurface(std::shared_ptr<Surface> surface)
{
  std::lock_guard<std::mutex> renderLock(renderMutex);
  surface->setID(shared_from_this(), nextSurfaceID);
  surfaces.insert({ nextSurfaceID++, surface });
  executionFlowGraphValid = false;
}

void Viewer::removeSurface(uint32_t surfaceID)
{
  std::lock_guard<std::mutex> renderLock(renderMutex);
  auto it = surfaces.find(surfaceID);
  CHECK_LOG_THROW(it == end(surfaces), "Cannot remove surface with id = " << surfaceID);
  bool isMainWindow = it->second->window->isMainWindow();
  // FIXME - what about removing resources allocated for this surface ?
  surfaces.erase(it);
  if(isMainWindow || surfaces.empty())
    setTerminate();
  executionFlowGraphValid = false;
}

std::vector<uint32_t> Viewer::getDeviceIDs() const
{
  std::vector<uint32_t> result;
  for (const auto& dev : devices)
    result.push_back(dev.first);
  return std::move(result);
}

Device*  Viewer::getDevice(uint32_t id)
{
  auto it = devices.find(id);
  if (it == end(devices))
    return nullptr;
  return it->second.get();
}

std::vector<uint32_t> Viewer::getSurfaceIDs() const
{
  std::vector<uint32_t> result;
  for (const auto& surf : surfaces)
    result.push_back(surf.first);
  return std::move(result);
}

Surface* Viewer::getSurface(uint32_t id)
{
  auto it = surfaces.find(id);
  if (it == end(surfaces))
    return nullptr;
  return it->second.get();
}

void Viewer::compileRenderGraph(std::shared_ptr<RenderGraph> renderGraph, const std::vector<QueueTraits>& qt)
{
//  auto tickStart = HPClock::now();
  renderGraph->addMissingResourceTransitions();
  std::shared_ptr<RenderGraphExecutable> executable = renderGraphCompiler->compile(*renderGraph, *externalMemoryObjects, qt, frameBufferAllocator);
  renderGraphs.insert({ renderGraph->name, executable });
  queueTraits.insert({ renderGraph->name, qt });
//  auto tickEnd = HPClock::now();
//  LOG_ERROR << "Compilation of render graph " << renderGraph->name << " took " << 1000.0f * inSeconds(tickEnd - tickStart) << " ms " <<std::endl;
}

std::shared_ptr<RenderGraphExecutable> Viewer::getRenderGraphExecutable(const std::string& name) const
{
  auto it = renderGraphs.find(name);
  if(it == end(renderGraphs))
      return std::shared_ptr<RenderGraphExecutable>();
  return it->second;
}

const std::vector<QueueTraits>& Viewer::getRenderGraphQueueTraits(const std::string& name) const
{
  auto it = queueTraits.find(name);
  CHECK_LOG_THROW(it == end(queueTraits), "Viewer does not have registered queue traits for render graph : " << name);
  return it->second;
}

void Viewer::addInputEventHandler(std::shared_ptr<InputEventHandler> eventHandler)
{
  inputEventHandlers.erase(std::remove_if(begin(inputEventHandlers), end(inputEventHandlers), [&](std::shared_ptr<InputEventHandler> ie) { return ie.get() == eventHandler.get();  }), end(inputEventHandlers));
  inputEventHandlers.push_back(eventHandler);
}

void Viewer::removeInputEventHandler(std::shared_ptr<InputEventHandler> eventHandler)
{
  inputEventHandlers.erase(std::remove_if(begin(inputEventHandlers), end(inputEventHandlers), [&](std::shared_ptr<InputEventHandler> ie) { return ie.get() == eventHandler.get();  }), end(inputEventHandlers));
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

std::string Viewer::getAbsoluteFilePath(const std::string& relativeFilePath) const
{
  // check if our relativeFilePath candidate is not absolute path
  filesystem::path candidate(relativeFilePath);
  if (candidate.is_absolute() && filesystem::exists(candidate))
    return candidate.string();
  for (auto directory : defaultDirectories)
  {
    filesystem::path targetPath = directory / candidate;
    if (filesystem::exists(targetPath))
      return targetPath.string();
  }
  return std::string();
}

std::shared_ptr<Asset> Viewer::loadAsset(const std::string& fileName, bool animationOnly, const std::vector<VertexSemantic>& requiredSemantic) const
{
  auto fullFileName = getAbsoluteFilePath(fileName);
  CHECK_LOG_THROW(fullFileName.empty(), "Cannot find asset file " << fileName);

  // find loader that will load this asset - loader is matched by file extension ( lowercase )
  auto index = fullFileName.find_last_of('.');
  CHECK_LOG_THROW(index == std::string::npos, "File does not have an extension " << fileName);
  auto extension = fullFileName.substr(index + 1);
  std::transform(begin(extension), end(extension), begin(extension), std::tolower);

  for (auto& loader : assetLoaders)
  {
    const auto& exts = loader->getSupportedExtensions();
    if (std::find(begin(exts), end(exts), extension) == end(exts))
      continue;
    return loader->load(fullFileName, animationOnly, requiredSemantic);
  }
  LOG_WARNING << "Cannot find loader for asset " << fileName << std::endl;
  return std::shared_ptr<Asset>();
}

std::shared_ptr<gli::texture> Viewer::loadTexture(const std::string& fileName, bool buildMipMaps) const
{
  auto fullFileName = getAbsoluteFilePath(fileName);
  CHECK_LOG_THROW(fullFileName.empty(), "Cannot find texture file " << fileName);

  // find loader that will load this texture - loader is matched by file extension ( lowercase )
  auto index = fullFileName.find_last_of('.');
  CHECK_LOG_THROW(index == std::string::npos, "File does not have an extension " << fileName);
  auto extension = fullFileName.substr(index + 1);
  std::transform(begin(extension), end(extension), begin(extension), std::tolower);

  for (auto& loader : textureLoaders)
  {
    const auto& exts = loader->getSupportedExtensions();
    if (std::find(begin(exts), end(exts), extension) == end(exts))
      continue;
    return loader->load(fullFileName, buildMipMaps);
  }
  LOG_WARNING << "Cannot find loader for texture " << fileName << std::endl;
  return std::shared_ptr<gli::texture>();
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

void Viewer::loadExtensionFunctions()
{
  // initialize extensions
  if (viewerTraits.useDebugLayers())
  {
    pfn_vkCreateDebugReportCallback  = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT> (vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
    pfn_vkDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
    pfn_vkDebugReportMessage         = reinterpret_cast<PFN_vkDebugReportMessageEXT>        (vkGetInstanceProcAddr(instance, "vkDebugReportMessageEXT"));
  }
  for (const auto& extension : viewerTraits.requestedInstanceExtensions)
  {
    if (!std::strcmp(extension.c_str(), VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
    {
      pfn_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));
      pfn_vkGetPhysicalDeviceFeatures2   = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>  (vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    }
  }
}

void Viewer::setupDebugging(VkDebugReportFlagsEXT flags, VkDebugReportCallbackEXT callBack)
{
  VkDebugReportCallbackCreateInfoEXT dbgCreateInfo{};
    dbgCreateInfo.sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)messageCallback;
    dbgCreateInfo.flags       = flags;
  VK_CHECK_LOG_THROW(pfn_vkCreateDebugReportCallback(instance, &dbgCreateInfo, nullptr, (callBack != nullptr) ? &callBack : &msgCallback), "Cannot create debug report callback");
}

void Viewer::cleanupDebugging()
{
  if (msgCallback != VK_NULL_HANDLE)
  {
    pfn_vkDestroyDebugReportCallback(instance, msgCallback, nullptr);
    msgCallback = VK_NULL_HANDLE;
  }
}

uint32_t   Viewer::getNextUpdateSlot() const
{
  // pick up the frame not used currently by render nor update
  for (uint32_t i = 0; i < 3; ++i)
  {
    if (i != renderIndex && i != updateIndex)
      return i;
  }
  CHECK_LOG_THROW(true, "Not possible");
  return 0;
}
uint32_t   Viewer::getNextRenderSlot() const
{
  // pick up the newest frame not used currently by update
  auto value = viewerStartTime;
  auto slot  = 0;
  for (uint32_t i = 0; i < 3; ++i)
  {
    if (updateInProgress && i == updateIndex)
      continue;
    if (updateTimes[i] > value)
    {
      value = updateTimes[i];
      slot = i;
    }
  }
  return slot;
}

void Viewer::onEventRenderStart()
{
  HPClock::time_point tickStart;
  if (timeStatistics->hasFlags(TSV_STAT_RENDER_EVENTS))
    tickStart = HPClock::now();

  if (eventRenderStart != nullptr)
    eventRenderStart(this);

  if (timeStatistics->hasFlags(TSV_STAT_RENDER_EVENTS))
  {
    auto tickEnd = HPClock::now();
    timeStatistics->setValues(TSV_CHANNEL_EVENT_RENDER_START, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
  }
}
void Viewer::onEventRenderFinish()
{
  HPClock::time_point tickStart;
  if (timeStatistics->hasFlags(TSV_STAT_RENDER_EVENTS))
    tickStart = HPClock::now();

  if (eventRenderFinish != nullptr)
    eventRenderFinish(this);
  if (timeStatistics->hasFlags(TSV_STAT_RENDER_EVENTS))
  {
    auto tickEnd = HPClock::now();
    timeStatistics->setValues(TSV_CHANNEL_EVENT_RENDER_FINISH, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
  }
}

void Viewer::handleInputEvents()
{
  // collect inputEvents from all windows
  std::vector<InputEvent> inputEvents;
  for (auto& s : surfaces)
  {
    std::vector<InputEvent> windowInputEvents = s.second->window->getInputEvents();
    inputEvents.insert(end(inputEvents), begin(windowInputEvents), end(windowInputEvents));
  }
  // sort input events by event time
  std::sort(begin(inputEvents), end(inputEvents), [](const InputEvent& lhs, const InputEvent& rhs) { return lhs.time < rhs.time; });
  // handle inputEvents using inputEventHandlers
  for (const auto& inputEvent : inputEvents)
  {
    for (auto& inputEventHandler : inputEventHandlers)
    {
      if (inputEventHandler->handle(inputEvent, this))
        break;
    }
  }
}

void Viewer::buildExecutionFlowGraph()
{
  executionFlowGraph.reset();

  opSurfaceBeginFrame.clear();
  opSurfaceEventRenderStart.clear();
  opSurfaceValidateRenderGraphs.clear();
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
    opSurfaceBeginFrame.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
        tickStart = HPClock::now();

      surface->beginFrame();

      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_BEGINFRAME, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });
    opSurfaceEventRenderStart.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_EVENTS))
        tickStart = HPClock::now();

      surface->onEventSurfaceRenderStart();

      if (surface->timeStatistics->hasFlags(TSS_STAT_EVENTS))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_EVENTSURFACERENDERSTART, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });
    opSurfaceValidateRenderGraphs.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
        tickStart = HPClock::now();

      surface->validateRenderGraphs();

      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_VALIDATERENDERGRAPH, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });

    {
      auto jit = opSurfaceValidatePrimaryNodes.find(surface);
      if (jit == end(opSurfaceValidatePrimaryNodes))
        jit = opSurfaceValidatePrimaryNodes.insert({ surface, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>() }).first;
      for (uint32_t i = 0; i < surface->getNumQueues(); ++i)
      {
        auto queue = surface->getQueue(i); // FIXME : we have to modify it anyway...
        jit->second.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
        {
          HPClock::time_point tickStart;
          if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
            tickStart = HPClock::now();

          surface->validatePrimaryNodes(i);

          if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
          {
            auto tickEnd = HPClock::now();
            // FIXME - wrong channel number
            surface->timeStatistics->setValues(20 + 10 * i + 0, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
          }
        });
      }
    }
    {
      auto jit = opSurfaceValidatePrimaryDescriptors.find(surface);
      if (jit == end(opSurfaceValidatePrimaryDescriptors))
        jit = opSurfaceValidatePrimaryDescriptors.insert({ surface, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>() }).first;
      for (uint32_t i = 0; i < surface->getNumQueues(); ++i)
      {
        jit->second.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
        {
          HPClock::time_point tickStart;
          if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
            tickStart = HPClock::now();

          surface->validatePrimaryDescriptors(i);

          if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
          {
            auto tickEnd = HPClock::now();
            // FIXME - wrong channel number
            surface->timeStatistics->setValues(20 + 10 * i + 1, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
          }
        });
      }
    }
    {
      auto jit = opSurfacePrimaryBuffers.find(surface);
      if (jit == end(opSurfacePrimaryBuffers))
        jit = opSurfacePrimaryBuffers.insert({ surface, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>() }).first;
      for (uint32_t i = 0; i < surface->getNumQueues(); ++i)
      {
        jit->second.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
        {
          HPClock::time_point tickStart;
          if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
            tickStart = HPClock::now();

          surface->buildPrimaryCommandBuffer(i);

          if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
          {
            auto tickEnd = HPClock::now();
            // FIXME - wrong channel number
            surface->timeStatistics->setValues(20 + 10 * i + 2, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
          }
        });
      }
    }
    opSurfaceValidateSecondaryNodes.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
        tickStart = HPClock::now();

      surface->validateSecondaryNodes();

      if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_VALIDATESECONDARYNODES, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });
    opSurfaceBarrier0.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      doNothing();
    });
    opSurfaceValidateSecondaryDescriptors.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
        tickStart = HPClock::now();

      surface->validateSecondaryDescriptors();

      if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_VALIDATESECONDARYDESCRIPTORS, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });
    opSurfaceSecondaryCommandBuffers.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
        tickStart = HPClock::now();

      surface->setCommandBufferIndices();
      surface->buildSecondaryCommandBuffers();

      if (surface->timeStatistics->hasFlags(TSS_STAT_BUFFERS))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_BUILDSECONDARYCOMMANDBUFFERS, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });
    opSurfaceDrawFrame.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
        tickStart = HPClock::now();

      surface->draw();

      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_DRAW, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });
    opSurfaceEndFrame.emplace_back(executionFlowGraph, [=](tbb::flow::continue_msg)
    {
      HPClock::time_point tickStart;
      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
        tickStart = HPClock::now();

      surface->endFrame();

      if (surface->timeStatistics->hasFlags(TSS_STAT_BASIC))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_ENDFRAME, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }

      if (surface->timeStatistics->hasFlags(TSS_STAT_EVENTS))
        tickStart = HPClock::now();

      surface->onEventSurfaceRenderFinish();

      if (surface->timeStatistics->hasFlags(TSS_STAT_EVENTS))
      {
        auto tickEnd = HPClock::now();
        surface->timeStatistics->setValues(TSS_CHANNEL_EVENTSURFACERENDERFINISH, inSeconds(tickStart - viewerStartTime), inSeconds(tickEnd - tickStart));
      }
    });
  }

  tbb::flow::make_edge(opExecutionFlowGraphStart, opExecutionFlowGraphEventRenderStart);
  for (uint32_t i = 0; i < surfacePointers.size(); ++i)
  {
    tbb::flow::make_edge(opExecutionFlowGraphStart, opSurfaceBeginFrame[i]);
    tbb::flow::make_edge(opExecutionFlowGraphStart, opSurfaceEventRenderStart[i]);

    tbb::flow::make_edge(opSurfaceBeginFrame[i], opSurfaceValidateRenderGraphs[i]);
    tbb::flow::make_edge(opSurfaceEventRenderStart[i], opSurfaceValidateRenderGraphs[i]);
    tbb::flow::make_edge(opExecutionFlowGraphEventRenderStart, opSurfaceValidateRenderGraphs[i]);

    tbb::flow::make_edge(opSurfaceValidateRenderGraphs[i], opSurfaceValidateSecondaryNodes[i]);
    tbb::flow::make_edge(opSurfaceValidateSecondaryNodes[i], opSurfaceBarrier0[i]);
    tbb::flow::make_edge(opSurfaceBarrier0[i], opSurfaceValidateSecondaryDescriptors[i]);
    tbb::flow::make_edge(opSurfaceValidateSecondaryDescriptors[i], opSurfaceSecondaryCommandBuffers[i]);

    auto jit0 = opSurfaceValidatePrimaryNodes.find(surfacePointers[i]);
    if (jit0 == end(opSurfaceValidatePrimaryNodes) || jit0->second.size() == 0)
    {
      // no primary command buffer building ? Maybe we should throw an error ?
      tbb::flow::make_edge(opSurfaceValidateRenderGraphs[i], opSurfaceSecondaryCommandBuffers[i]);
      tbb::flow::make_edge(opSurfaceSecondaryCommandBuffers[i], opSurfaceDrawFrame[i]);
    }
    else
    {
      auto jit1 = opSurfaceValidatePrimaryDescriptors.find(surfacePointers[i]);
      auto jit2 = opSurfacePrimaryBuffers.find(surfacePointers[i]);

      for (uint32_t j = 0; j < jit0->second.size(); ++j)
      {
        tbb::flow::make_edge(opSurfaceValidateRenderGraphs[i], jit0->second[j]);
        tbb::flow::make_edge(jit0->second[j], opSurfaceBarrier0[i]);
        tbb::flow::make_edge(opSurfaceBarrier0[i], jit1->second[j]);
        tbb::flow::make_edge(jit1->second[j], opSurfaceSecondaryCommandBuffers[i]);
        tbb::flow::make_edge(opSurfaceSecondaryCommandBuffers[i], jit2->second[j]);
        tbb::flow::make_edge(jit2->second[j], opSurfaceDrawFrame[i]);
      }
    }

    tbb::flow::make_edge(opSurfaceDrawFrame[i], opSurfaceEndFrame[i]);
    tbb::flow::make_edge(opSurfaceEndFrame[i], opExecutionFlowGraphFinish);
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
