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
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <vulkan/vulkan.h>
#include <tbb/flow_graph.h>
#include <pumex/Export.h>
#include <pumex/HPClock.h>
#include <functional>

#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;

namespace pumex
{

class  PhysicalDevice;
struct QueueTraits;
class  Device;
struct WindowTraits;
class  Window;
struct SurfaceTraits;
class  Surface;
class  TimeStatistics;
struct InputEvent;
class  InputEventHandler;

const uint32_t TSV_STAT_UPDATE                = 1;
const uint32_t TSV_STAT_RENDER                = 2;
const uint32_t TSV_STAT_RENDER_EVENTS         = 4;

const uint32_t TSV_GROUP_UPDATE                = 1;
const uint32_t TSV_GROUP_RENDER                = 2;
const uint32_t TSV_GROUP_RENDER_EVENTS         = 3;

const uint32_t TSV_CHANNEL_INPUTEVENTS         = 1;
const uint32_t TSV_CHANNEL_UPDATE              = 2;
const uint32_t TSV_CHANNEL_RENDER              = 3;
const uint32_t TSV_CHANNEL_FRAME               = 4;
const uint32_t TSV_CHANNEL_EVENT_RENDER_START  = 5;
const uint32_t TSV_CHANNEL_EVENT_RENDER_FINISH = 6;


// struct storing all info required to create or describe the viewer
struct PUMEX_EXPORT ViewerTraits
{
  ViewerTraits(const std::string& applicationName, const std::vector<std::string>& requestedInstanceExtensions, const std::vector<std::string>& requestedDebugLayers, uint32_t updatesPerSecond);

  std::string              applicationName;
  std::vector<std::string> requestedInstanceExtensions;
  std::vector<std::string> requestedDebugLayers;
  uint32_t                 updatesPerSecond = 100;

  VkDebugReportFlagsEXT    debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT; // | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
  // use debugReportCallback if you want to overwrite default messageCallback() logging function
  VkDebugReportCallbackEXT debugReportCallback = nullptr;

  inline bool useDebugLayers() const;
};

// Viewer class stores Vulkan instance and manages devices and surfaces.
// It also takes care of TBB threading, access to files and update and render timing computations
class PUMEX_EXPORT Viewer : public std::enable_shared_from_this<Viewer>
{
public:
  Viewer()                         = delete;
  explicit Viewer(const ViewerTraits& viewerTraits);
  Viewer(const Viewer&)            = delete;
  Viewer& operator=(const Viewer&) = delete;
  Viewer(Viewer&&)                 = delete;
  Viewer& operator=(Viewer&&)      = delete;
  ~Viewer();

  void                       addSurface(std::shared_ptr<Surface> surface);
  void                       removeSurface(uint32_t surfaceID);
  std::vector<uint32_t>      getSurfaceIDs() const;
  Surface*                   getSurface(uint32_t id);
  inline uint32_t            getNumSurfaces() const;

  std::shared_ptr<Device>    addDevice(unsigned int physicalDeviceIndex, const std::vector<std::string>& requestedExtensions);
  std::vector<uint32_t>      getDeviceIDs() const;
  Device*                    getDevice(uint32_t id);
  inline uint32_t            getNumDevices() const;

  inline void                setEventRenderStart(std::function<void(Viewer*)> event);
  inline void                setEventRenderFinish(std::function<void(Viewer*)> event);

  void                       addInputEventHandler(std::shared_ptr<InputEventHandler> eventHandler);
  void                       removeInputEventHandler(std::shared_ptr<InputEventHandler> eventHandler);

  void                       run();
  void                       cleanup();
  inline bool                isRealized() const;
  void                       realize();

  void                       setTerminate();
  inline bool                terminating() const;
  inline VkInstance          getInstance() const;

  inline uint32_t            getUpdateIndex() const;
  inline uint32_t            getPreviousUpdateIndex() const;
  inline uint32_t            getRenderIndex() const;
  inline unsigned long long  getFrameNumber() const;

  inline HPClock::duration   getUpdateDuration() const;      // time of one update ( = 1 / viewerTraits.updatesPerSecond )
  inline HPClock::time_point getApplicationStartTime() const;// get the time point of the application start
  inline HPClock::time_point getUpdateTime() const;          // get the time point of the update
  inline HPClock::duration   getRenderTimeDelta() const;     // get the difference between current render and last update

  void                       addDefaultDirectory( const filesystem::path & directory);
  std::string                getAbsoluteFilePath( const std::string& relativeFilePath ) const;

  bool                       instanceExtensionImplemented(const char* extensionName) const;
  bool                       instanceExtensionEnabled(const char* extensionName) const;

  ViewerTraits                                           viewerTraits;

  tbb::flow::graph                                       updateGraph;
  tbb::flow::continue_node< tbb::flow::continue_msg >    opStartUpdateGraph;
  tbb::flow::continue_node< tbb::flow::continue_msg >    opEndUpdateGraph;

  // Functions declared by instance extensions
  // extension : VK_KHR_get_physical_device_properties2
  PFN_vkGetPhysicalDeviceProperties2                     pfn_vkGetPhysicalDeviceProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2                       pfn_vkGetPhysicalDeviceFeatures2   = nullptr;

  // extension : VK_EXT_debug_report ( initialized by setting requestedDebugLayers in viewerTraits )
  PFN_vkCreateDebugReportCallbackEXT                     pfn_vkCreateDebugReportCallback      = nullptr;
  PFN_vkDestroyDebugReportCallbackEXT                    pfn_vkDestroyDebugReportCallback     = nullptr;
  PFN_vkDebugReportMessageEXT                            pfn_vkDebugReportMessage             = nullptr;

protected:
  void                       loadExtensionFunctions();
  void                       setupDebugging(VkDebugReportFlagsEXT flags, VkDebugReportCallbackEXT callBack);
  void                       cleanupDebugging();

  uint32_t                   getNextRenderSlot() const;
  uint32_t                   getNextUpdateSlot() const;
  inline void                doNothing() const;

  void                       onEventRenderStart();
  void                       onEventRenderFinish();
  void                       handleInputEvents();

  void                       buildRenderGraph();

  std::vector<filesystem::path>                          defaultDirectories;
  std::vector<std::shared_ptr<PhysicalDevice>>           physicalDevices;
  std::unordered_map<uint32_t, std::shared_ptr<Device>>  devices;
  std::unordered_map<uint32_t, std::shared_ptr<Surface>> surfaces;
  std::function<void(Viewer*)>                           eventRenderStart;
  std::function<void(Viewer*)>                           eventRenderFinish;
  std::vector<std::shared_ptr<InputEventHandler>>        inputEventHandlers;
  bool                                                   realized                           = false;
  bool                                                   renderContinueRun                  = true;
  bool                                                   updateContinueRun                  = true;
  bool                                                   viewerTerminate                    = false;
  std::exception_ptr                                     exceptionCaught;

  VkInstance                                             instance                           = VK_NULL_HANDLE;

  std::vector<const char*>                               enabledInstanceExtensions;
  std::vector<VkExtensionProperties>                     extensionProperties;
  std::vector<const char*>                               enabledDebugLayers;

  uint32_t                                               nextSurfaceID                      = 0;
  uint32_t                                               nextDeviceID                       = 0;
  unsigned long long                                     frameNumber                        = 0;
  HPClock::time_point                                    viewerStartTime;
  HPClock::time_point                                    renderStartTime;
  HPClock::time_point                                    updateTimes[3];
  std::unique_ptr<TimeStatistics>                        timeStatistics;

  uint32_t                                               renderIndex                        = 0;
  uint32_t                                               updateIndex                        = 1;
  uint32_t                                               prevUpdateIndex                    = 0; // accessible only during update. DO NOT USE IN RENDER.
  bool                                                   updateInProgress                   = false;

  mutable std::mutex                                     updateMutex;
  std::condition_variable                                updateConditionVariable;

  VkDebugReportCallbackEXT                               msgCallback;

  tbb::flow::graph                                       renderGraph;
  tbb::flow::continue_node< tbb::flow::continue_msg >    opRenderGraphStart, opRenderGraphEventRenderStart, opRenderGraphFinish;
  std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>> opSurfaceBeginFrame, opSurfaceEventRenderStart, opSurfaceValidateWorkflow, opSurfaceValidateSecondaryNodes, opSurfaceBarrier0, opSurfaceValidateSecondaryDescriptors, opSurfaceSecondaryCommandBuffers, opSurfaceDrawFrame, opSurfaceEndFrame;
  std::map<Surface*, std::vector<tbb::flow::continue_node<tbb::flow::continue_msg>>> opSurfaceValidatePrimaryNodes, opSurfaceValidatePrimaryDescriptors, opSurfacePrimaryBuffers;

  bool                                                   renderGraphValid                   = false;
};

bool                ViewerTraits::useDebugLayers() const    { return !requestedDebugLayers.empty(); }

bool                Viewer::isRealized() const              { return realized; }
uint32_t            Viewer::getNumDevices() const           { return devices.size(); }
uint32_t            Viewer::getNumSurfaces() const          { return surfaces.size(); }
VkInstance          Viewer::getInstance() const             { return instance; }
bool                Viewer::terminating() const             { return viewerTerminate; }
uint32_t            Viewer::getUpdateIndex() const          { return updateIndex; }
uint32_t            Viewer::getPreviousUpdateIndex() const  { return prevUpdateIndex; }
uint32_t            Viewer::getRenderIndex() const          { return renderIndex; }
unsigned long long  Viewer::getFrameNumber() const          { return frameNumber; }
HPClock::time_point Viewer::getApplicationStartTime() const { return viewerStartTime; }
HPClock::duration   Viewer::getUpdateDuration() const       { return (HPClock::duration(std::chrono::seconds(1))) / viewerTraits.updatesPerSecond; }
HPClock::time_point Viewer::getUpdateTime() const           { return updateTimes[updateIndex]; }
HPClock::duration   Viewer::getRenderTimeDelta() const      { return renderStartTime - updateTimes[renderIndex]; }
void                Viewer::doNothing() const               {}
void                Viewer::setEventRenderStart(std::function<void(Viewer*)> event)  { eventRenderStart = event; }
void                Viewer::setEventRenderFinish(std::function<void(Viewer*)> event) { eventRenderFinish = event; }

PUMEX_EXPORT VkBool32 messageCallback( VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void* pUserData);

}


