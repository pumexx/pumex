#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <vulkan/vulkan.h>
#include <tbb/flow_graph.h>
#include <pumex/Export.h>
#include <pumex/HPClock.h>

namespace pumex
{
  class  PhysicalDevice;
  struct QueueTraits;
  class  Device;
  struct WindowTraits;
  class  Window;
  struct SurfaceTraits;
  class  Surface;

  // struct holding all info required to create the viewer
  struct PUMEX_EXPORT ViewerTraits
  {
    ViewerTraits(const std::string& applicationName, bool useValidation, const std::vector<std::string>& requestedLayers, uint32_t updatesPerSecond);

    std::string              applicationName;
    bool                     useValidation;
    std::vector<std::string> requestedLayers;

    VkDebugReportFlagsEXT    debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT; // | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
    // use debugReportCallback if you want to overwrite default messageCallback() logging function
    VkDebugReportCallbackEXT debugReportCallback = nullptr;

    uint32_t updatesPerSecond = 100;
  };

  // Viewer class holds Vulkan instance and manages devices and surfaces
  class PUMEX_EXPORT Viewer : public std::enable_shared_from_this<Viewer>
  {
  public:
    explicit Viewer(const pumex::ViewerTraits& viewerTraits);
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;
    ~Viewer();

    std::shared_ptr<pumex::Device>    addDevice(unsigned int physicalDeviceIndex, const std::vector<pumex::QueueTraits>& requestedQueues, const std::vector<const char*>& requestedExtensions);
    std::shared_ptr<pumex::Surface>   addSurface(std::shared_ptr<pumex::Window> window, std::shared_ptr<pumex::Device> device, const pumex::SurfaceTraits& surfaceTraits);
    void run();
    void cleanup();
    void setTerminate();

    inline VkInstance getInstance() const;
    inline bool terminating() const;

    std::string getFullFilePath(const std::string& shortFilePath) const; // FIXME - needs transition to <filesystem> ASAP

    ViewerTraits                        viewerTraits;
    VkInstance                          instance = VK_NULL_HANDLE;
    std::vector<VkExtensionProperties>  extensionProperties;
    bool                                viewerTerminate = false;

    tbb::flow::graph                                    updateGraph;
    tbb::flow::continue_node< tbb::flow::continue_msg > startUpdateGraph;
    tbb::flow::continue_node< tbb::flow::continue_msg > endUpdateGraph;

    tbb::flow::graph                                    renderGraph;
    tbb::flow::continue_node< tbb::flow::continue_msg > startRenderGraph;
    tbb::flow::continue_node< tbb::flow::continue_msg > endRenderGraph;

    inline uint32_t getUpdateIndex() const;
    inline uint32_t getRenderIndex() const;

    inline pumex::HPClock::duration   getUpdateDuration() const;      // time of one update ( = 1 / viewerTraits.updatesPerSecond )
    inline pumex::HPClock::time_point getApplicationStartTime() const;// get the time point of the application start
    inline pumex::HPClock::time_point getUpdateTime() const;          // get the time point of the update
    inline pumex::HPClock::duration   getRenderTimeDelta() const;     // get the difference between current render and last update

    inline void addDefaultDirectory(const std::string& directory);

  protected:
    void setupDebugging(VkDebugReportFlagsEXT flags, VkDebugReportCallbackEXT callBack);
    void cleanupDebugging();

    inline uint32_t getNextRenderSlot() const;
    inline uint32_t getNextUpdateSlot() const;
    inline void doNothing() const;

    std::vector<std::string>                            defaultDirectories; // FIXME - needs transition to <filesystem> ASAP
    std::vector<std::shared_ptr<pumex::PhysicalDevice>> physicalDevices;
    std::vector<std::shared_ptr<pumex::Device>>         devices;
    std::vector<std::shared_ptr<pumex::Surface>>        surfaces;

    pumex::HPClock::time_point                          viewerStartTime;
    pumex::HPClock::time_point                          renderStartTime;
    pumex::HPClock::time_point                          updateStartTimes[3];
    pumex::HPClock::duration                            lastRenderDuration;
    pumex::HPClock::duration                            lastUpdateDuration;

    uint32_t                                            renderIndex = 0;
    uint32_t                                            updateIndex = 1;

    std::mutex                                          updateMutex;
    std::condition_variable                             updateConditionVariable;

    PFN_vkCreateDebugReportCallbackEXT                  pfnCreateDebugReportCallback = VK_NULL_HANDLE;
    PFN_vkDestroyDebugReportCallbackEXT                 pfnDestroyDebugReportCallback = VK_NULL_HANDLE;
    PFN_vkDebugReportMessageEXT                         pfnDebugReportMessage = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT                            msgCallback;

  };

  VkInstance                 Viewer::getInstance() const             { return instance; }
  bool                       Viewer::terminating() const             { return viewerTerminate; }
  uint32_t                   Viewer::getUpdateIndex() const          { return updateIndex; }
  uint32_t                   Viewer::getRenderIndex() const          { return renderIndex; }
  pumex::HPClock::time_point Viewer::getApplicationStartTime() const { return viewerStartTime; }
  pumex::HPClock::duration   Viewer::getUpdateDuration() const       { return (pumex::HPClock::duration(std::chrono::seconds(1))) / viewerTraits.updatesPerSecond; }
  pumex::HPClock::time_point Viewer::getUpdateTime() const           { return updateStartTimes[updateIndex]; }
  pumex::HPClock::duration   Viewer::getRenderTimeDelta() const      { return renderStartTime - updateStartTimes[renderIndex]; }
  void                       Viewer::addDefaultDirectory(const std::string& directory) { defaultDirectories.push_back(directory); }
  void                       Viewer::doNothing() const               {}

  uint32_t   Viewer::getNextUpdateSlot() const
  {
    // pick up the frame not used currently by render nor update
    uint32_t slot;
    for (uint32_t i = 0; i < 3; ++i)
    {
      if (i != renderIndex && i != updateIndex)
        slot = i;
    }
    return slot;
  }
  uint32_t   Viewer::getNextRenderSlot() const
  {
    // pick up the newest frame not used currently by update
    auto value = viewerStartTime;
    auto slot = 0;
    for (uint32_t i = 0; i < 3; ++i)
    {
      if (i == updateIndex)
        continue;
      if (updateStartTimes[i] > value)
      {
        value = updateStartTimes[i];
        slot = i;
      }
    }
    return slot;
  }


  PUMEX_EXPORT VkBool32 messageCallback( VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void* pUserData);

}


