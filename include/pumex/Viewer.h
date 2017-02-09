#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/HPClock.h>
#include <tbb/flow_graph.h>

namespace pumex
{
  class  PhysicalDevice;
  struct QueueTraits;
  class  Device;
  struct WindowTraits;
  class  Window;
  struct SurfaceTraits;
  class Surface;
  class Thread;
  class ThreadJoiner;
  class SurfaceThread;

  // struct holding all info required to create the viewer
  struct PUMEX_EXPORT ViewerTraits
  {
    ViewerTraits(const std::string& applicationName, bool useValidation, const std::vector<std::string>& requestedLayers);

    std::string              applicationName;
    bool                     useValidation;
    std::vector<std::string> requestedLayers;

    VkDebugReportFlagsEXT    debugReportFlags    = VK_DEBUG_REPORT_ERROR_BIT_EXT; // | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
    // use debugReportCallback if you want to overwrite default messageCallback() logging function
    VkDebugReportCallbackEXT debugReportCallback = nullptr;
  };

  // Viewer class holds Vulkan instance and manages devices and surfaces
  class PUMEX_EXPORT Viewer : public std::enable_shared_from_this<Viewer>
  {
  public:
    explicit Viewer(const pumex::ViewerTraits& viewerTraits);
    Viewer(const Viewer&)            = delete;
    Viewer& operator=(const Viewer&) = delete;
    ~Viewer();

    std::shared_ptr<pumex::Device> addDevice( unsigned int physicalDeviceIndex, const std::vector<pumex::QueueTraits>& requestedQueues, const std::vector<const char*>& requestedExtensions );
    std::shared_ptr<pumex::Surface> addSurface(std::shared_ptr<pumex::Window> window, std::shared_ptr<pumex::Device> device, const pumex::SurfaceTraits& surfaceTraits, std::shared_ptr<pumex::SurfaceThread> surfaceThread);
    void addThread(pumex::Thread* thread);
    inline pumex::HPClock::time_point getStartTime();

    void run();
    void cleanup();

    inline VkInstance getInstance() const;
    inline void setTerminate();
    inline bool terminating() const;

    std::string getFullFilePath(const std::string& shortFilePath) const; // FIXME - needs transition to <filesystem> ASAP

    ViewerTraits                        viewerTraits;
    VkInstance                          instance             = VK_NULL_HANDLE;
    std::vector<VkExtensionProperties>  extensionProperties;
    bool                                viewerTerminate      = false;
    tbb::flow::graph                    runGraph;
    tbb::flow::broadcast_node< tbb::flow::continue_msg > startGraph;
  protected:
    void setupDebugging(VkDebugReportFlagsEXT flags, VkDebugReportCallbackEXT callBack);
    void cleanupDebugging();

    std::vector<std::string>                            defaultDirectories; // FIXME - needs transition to <filesystem> ASAP
    std::vector<std::shared_ptr<pumex::PhysicalDevice>> physicalDevices;
    std::vector<std::shared_ptr<pumex::Device>>         devices;
    std::vector<std::shared_ptr<pumex::Surface>>        surfaces;
    std::vector<pumex::Thread*>                         pumexThreads;
    pumex::HPClock::time_point      startTime;

    PFN_vkCreateDebugReportCallbackEXT                  pfnCreateDebugReportCallback  = VK_NULL_HANDLE;
    PFN_vkDestroyDebugReportCallbackEXT                 pfnDestroyDebugReportCallback = VK_NULL_HANDLE;
    PFN_vkDebugReportMessageEXT                         pfnDebugReportMessage         = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT                            msgCallback;

  };

  pumex::HPClock::time_point                     Viewer::getStartTime()      { return startTime; }
  VkInstance                                     Viewer::getInstance() const { return instance; }
  void                                           Viewer::setTerminate()      { viewerTerminate = true; };
  bool                                           Viewer::terminating() const { return viewerTerminate; }

  PUMEX_EXPORT VkBool32 messageCallback( VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void* pUserData);

}

