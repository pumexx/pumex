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
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Device.h>
#include <pumex/utils/ActionQueue.h>
#include <pumex/RenderGraph.h>

namespace pumex
{

class Viewer;
class Window;
class RenderGraphExecutable;
class CommandPool;
class CommandBuffer;
class FrameBuffer;
class MemoryBuffer;
class MemoryImage;
class ImageView;
class Image;
class Node;
class TimeStatistics;

const uint32_t TSS_STAT_BASIC   = 1;
const uint32_t TSS_STAT_BUFFERS = 2;
const uint32_t TSS_STAT_EVENTS  = 4;

const uint32_t TSS_GROUP_BASIC             = 1;
const uint32_t TSS_GROUP_EVENTS            = 2;
const uint32_t TSS_GROUP_SECONDARY_BUFFERS = 20;
const uint32_t TSS_GROUP_PRIMARY_BUFFERS   = 10;

const uint32_t TSS_CHANNEL_BEGINFRAME                   = 1;
const uint32_t TSS_CHANNEL_EVENTSURFACERENDERSTART      = 2;
const uint32_t TSS_CHANNEL_VALIDATERENDERGRAPH             = 3;
const uint32_t TSS_CHANNEL_VALIDATESECONDARYNODES       = 4;
const uint32_t TSS_CHANNEL_VALIDATESECONDARYDESCRIPTORS = 5;
const uint32_t TSS_CHANNEL_BUILDSECONDARYCOMMANDBUFFERS = 6;
const uint32_t TSS_CHANNEL_DRAW                         = 7;
const uint32_t TSS_CHANNEL_ENDFRAME                     = 8;
const uint32_t TSS_CHANNEL_EVENTSURFACERENDERFINISH     = 9;


// struct representing information required to create a Vulkan surface
struct PUMEX_EXPORT SurfaceTraits
{
  explicit SurfaceTraits(const ResourceDefinition& swapChainDefinition, uint32_t swapChainImageCount, VkColorSpaceKHR swapChainImageColorSpace, VkPresentModeKHR swapchainPresentMode, VkSurfaceTransformFlagBitsKHR preTransform, VkCompositeAlphaFlagBitsKHR compositeAlpha);

  ResourceDefinition                 swapChainDefinition;
  uint32_t                           swapChainImageCount;
  VkColorSpaceKHR                    swapChainImageColorSpace;
  VkPresentModeKHR                   swapchainPresentMode;
  VkSurfaceTransformFlagBitsKHR      preTransform;
  VkCompositeAlphaFlagBitsKHR        compositeAlpha;
  // this variable exists so that WindowQT will not destroy surface on cleanup(), because Vulkan surface is owned by QT
  bool                               destroySurfaceOnCleanup = true;
};

// class representing a Vulkan surface
class PUMEX_EXPORT Surface : public std::enable_shared_from_this<Surface>
{
public:
  Surface()                          = delete;
  explicit Surface(std::shared_ptr<Device> device, std::shared_ptr<Window> window, VkSurfaceKHR surface, const SurfaceTraits& surfaceTraits);
  Surface(const Surface&)            = delete;
  Surface& operator=(const Surface&) = delete;
  Surface(Surface&&)                 = delete;
  Surface& operator=(Surface&&)      = delete;
  virtual ~Surface();

  inline bool                   isRealized() const;
  void                          collectQueueTraits();
  void                          realize();
  void                          cleanup();
  void                          beginFrame();
  void                          validateRenderGraphs();
  void                          setCommandBufferIndices();
  void                          validatePrimaryNodes(uint32_t queueIndex);
  void                          validatePrimaryDescriptors(uint32_t queueIndex);
  void                          buildPrimaryCommandBuffer(uint32_t queueIndex);
  void                          validateSecondaryNodes();
  void                          validateSecondaryDescriptors();
  void                          buildSecondaryCommandBuffers();
  void                          draw();
  void                          endFrame();
  void                          resizeSurface(uint32_t newWidth, uint32_t newHeight);
  inline uint32_t               getImageCount() const;
  inline uint32_t               getImageIndex() const;

  void                          addRenderGraph(const std::string& name, bool active);
  std::vector<uint32_t>         getQueueIndices(const std::string renderGraphName) const;
  uint32_t                      getNumQueues() const;
  Queue*                        getQueue(uint32_t index) const;
  std::shared_ptr<CommandPool>  getCommandPool(uint32_t index) const;

  void                          setID(std::shared_ptr<Viewer> viewer, uint32_t newID);
  inline uint32_t               getID() const;

  inline void                   setEventSurfaceRenderStart(std::function<void(std::shared_ptr<Surface>)> event);
  inline void                   setEventSurfaceRenderFinish(std::function<void(std::shared_ptr<Surface>)> event);
  inline void                   setEventSurfacePrepareStatistics(std::function<void(Surface*, TimeStatistics*, TimeStatistics*)> event);

  void                          onEventSurfaceRenderStart();
  void                          onEventSurfaceRenderFinish();
  void                          onEventSurfacePrepareStatistics(TimeStatistics* viewerStatistics);


  std::shared_ptr<CommandPool>  getPresentationCommandPool();
  std::shared_ptr<Queue>        getPresentationQueue();

  std::weak_ptr<Viewer>                         viewer;
  std::weak_ptr<Device>                         device;
  std::shared_ptr<Window>                       window;

  VkSurfaceKHR                                  surface                      = VK_NULL_HANDLE;
  SurfaceTraits                                 surfaceTraits;

  VkSurfaceCapabilitiesKHR                      surfaceCapabilities;
  std::vector<VkPresentModeKHR>                 presentModes;
  std::vector<VkSurfaceFormatKHR>               surfaceFormats;
  std::vector<VkBool32>                         supportsPresent;

  VkExtent2D                                    swapChainSize                = VkExtent2D{1,1};
  uint32_t                                      swapChainImageIndex          = 0;
  std::vector<std::shared_ptr<Image>>           swapChainImages;

  ActionQueue                                   actions;
  std::unique_ptr<TimeStatistics>               timeStatistics;

protected:
  uint32_t                                      id                           = 0;
  VkSwapchainKHR                                swapChain                    = VK_NULL_HANDLE;
  bool                                          realized                     = false;
  bool                                          resized                      = false;

  std::vector<QueueTraits>                      queueTraits;
  std::vector<std::shared_ptr<Queue>>           queues;
  std::vector<std::shared_ptr<CommandPool>>     commandPools;
  uint32_t                                      presentationQueueIndex       = -1;

  std::vector<std::tuple<std::string, bool>>    renderGraphData;
  std::map<std::string, std::vector<uint32_t>>  renderGraphQueueIndices;
  std::map<std::string, std::vector<std::shared_ptr<CommandBuffer>>> primaryCommandBuffers;

  std::shared_ptr<CommandBuffer>                presentCommandBuffer;
  std::vector<VkFence>                          waitFences;

  std::vector<Node*>                            secondaryCommandBufferNodes;
  std::vector<VkRenderPass>                     secondaryCommandBufferRenderPasses;
  std::vector<uint32_t>                         secondaryCommandBufferSubPasses;

  VkSemaphore                                   imageAvailableSemaphore      = VK_NULL_HANDLE;
  std::vector<VkSemaphore>                      attachmentsLayoutCompletedSemaphores;
  std::vector<std::vector<VkSemaphore>>         queueSubmissionCompletedSemaphores;
  VkSemaphore                                   renderFinishedSemaphore      = VK_NULL_HANDLE;

  std::function<void(std::shared_ptr<Surface>)> eventSurfaceRenderStart;
  std::function<void(std::shared_ptr<Surface>)> eventSurfaceRenderFinish;
  std::function<void(Surface*, TimeStatistics*, TimeStatistics*)> eventSurfacePrepareStatistics;

  void                                          recreateSwapChain();
public:
  static const std::unordered_map<std::string, VkPresentModeKHR>         nameToPresentationModes;
  static const std::map<VkPresentModeKHR, std::string>                   presentationModeNames;
  static const std::map<VkPresentModeKHR, std::vector<VkPresentModeKHR>> replacementModes;
};

bool                         Surface::isRealized() const                                                               { return realized; }
uint32_t                     Surface::getID() const                                                                    { return id; }
uint32_t                     Surface::getImageCount() const                                                            { return surfaceTraits.swapChainImageCount; }
uint32_t                     Surface::getImageIndex() const                                                            { return swapChainImageIndex; }
void                         Surface::setEventSurfaceRenderStart(std::function<void(std::shared_ptr<Surface>)> event)  { eventSurfaceRenderStart = event; }
void                         Surface::setEventSurfaceRenderFinish(std::function<void(std::shared_ptr<Surface>)> event) { eventSurfaceRenderFinish = event; }
void                         Surface::setEventSurfacePrepareStatistics(std::function<void(Surface*, TimeStatistics*, TimeStatistics*)> event) { eventSurfacePrepareStatistics = event; }


}
