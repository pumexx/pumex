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
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Device.h>
#include <pumex/utils/ActionQueue.h>

namespace pumex
{

class Viewer;
class Window;
class RenderWorkflow;
class RenderWorkflowCompiler;
class RenderWorkflowResults;
class CommandPool;
class CommandBuffer;
class FrameBuffer;
class MemoryObject;
class ImageView;
class Image;
class Node;
  
// struct representing information required to create a Vulkan surface
struct PUMEX_EXPORT SurfaceTraits
{
  explicit SurfaceTraits(uint32_t imageCount, VkColorSpaceKHR imageColorSpace, uint32_t imageArrayLayers, VkPresentModeKHR swapchainPresentMode, VkSurfaceTransformFlagBitsKHR preTransform, VkCompositeAlphaFlagBitsKHR compositeAlpha);

  uint32_t                           imageCount;
  VkColorSpaceKHR                    imageColorSpace;
  uint32_t                           imageArrayLayers; // always 1 ( until VR )
  VkPresentModeKHR                   swapchainPresentMode;
  VkSurfaceTransformFlagBitsKHR      preTransform;
  VkCompositeAlphaFlagBitsKHR        compositeAlpha;
};

// class representing a Vulkan surface
class PUMEX_EXPORT Surface : public std::enable_shared_from_this<Surface>
{
public:
  Surface()                          = delete;
  explicit Surface(std::shared_ptr<Viewer> viewer, std::shared_ptr<Window> window, std::shared_ptr<Device> device, VkSurfaceKHR surface, const SurfaceTraits& surfaceTraits);
  Surface(const Surface&)            = delete;
  Surface& operator=(const Surface&) = delete;
  Surface(Surface&&)                 = delete;
  Surface& operator=(Surface&&)      = delete;
  virtual ~Surface();

  inline bool                   isRealized() const;
  void                          realize();
  void                          cleanup();
  void                          beginFrame();
  void                          validateWorkflow();
  void                          setCommandBufferIndices();
  void                          validatePrimaryNodes(uint32_t queueNumber);
  void                          validatePrimaryDescriptors(uint32_t queueNumber);
  void                          buildPrimaryCommandBuffer(uint32_t queueNumber);
  void                          validateSecondaryNodes();
  void                          validateSecondaryDescriptors();
  void                          buildSecondaryCommandBuffers();
  void                          draw();
  void                          endFrame();
  void                          resizeSurface(uint32_t newWidth, uint32_t newHeight);
  inline uint32_t               getImageCount() const;
  inline uint32_t               getImageIndex() const;

  void                          setRenderWorkflow(std::shared_ptr<RenderWorkflow> workflow, std::shared_ptr<RenderWorkflowCompiler> compiler);

  inline void                   setID(uint32_t newID);
  inline uint32_t               getID() const;

  std::shared_ptr<MemoryObject> getRegisteredMemoryObject(const std::string name);
  std::shared_ptr<ImageView>    getRegisteredImageView(const std::string name);

  inline void                   setEventSurfaceRenderStart(std::function<void(std::shared_ptr<Surface>)> event);
  inline void                   setEventSurfaceRenderFinish(std::function<void(std::shared_ptr<Surface>)> event);

  inline void                   onEventSurfaceRenderStart();
  inline void                   onEventSurfaceRenderFinish();

  std::shared_ptr<CommandPool>  getPresentationCommandPool();
  std::shared_ptr<Queue>        getPresentationQueue();

  std::weak_ptr<Viewer>                         viewer;
  std::weak_ptr<Window>                         window;
  std::weak_ptr<Device>                         device;

  VkSurfaceKHR                                  surface                      = VK_NULL_HANDLE;
  SurfaceTraits                                 surfaceTraits;

  std::shared_ptr<RenderWorkflow>               renderWorkflow;
  std::shared_ptr<RenderWorkflowCompiler>       renderWorkflowCompiler;
  std::shared_ptr<RenderWorkflowResults>        workflowResults;

  VkSurfaceCapabilitiesKHR                      surfaceCapabilities;
  std::vector<VkPresentModeKHR>                 presentModes;
  std::vector<VkSurfaceFormatKHR>               surfaceFormats;
  std::vector<VkBool32>                         supportsPresent;

  std::vector<std::shared_ptr<Queue>>           queues;
  std::vector<std::shared_ptr<CommandPool>>     commandPools;

  VkExtent2D                                    swapChainSize                = VkExtent2D{1,1};
  uint32_t                                      swapChainImageIndex          = 0;
  std::vector<std::shared_ptr<Image>>           swapChainImages;

  ActionQueue                                   actions;

protected:
  uint32_t                                      id                           = 0;
  VkSwapchainKHR                                swapChain                    = VK_NULL_HANDLE;
  bool                                          realized                     = false;
  bool                                          resized                      = false;

  std::vector<VkFence>                          waitFences;
  std::shared_ptr<CommandBuffer>                prepareCommandBuffer;
  std::vector<std::shared_ptr<CommandBuffer>>   primaryCommandBuffers;
  std::shared_ptr<CommandBuffer>                presentCommandBuffer;

  std::vector<Node*>                            secondaryCommandBufferNodes;
  std::vector<VkRenderPass>                     secondaryCommandBufferRenderPasses;
  std::vector<uint32_t>                         secondaryCommandBufferSubPasses;

  VkSemaphore                                   imageAvailableSemaphore      = VK_NULL_HANDLE;
  std::vector<VkSemaphore>                      frameBufferReadySemaphores;
  std::vector<VkSemaphore>                      renderCompleteSemaphores;
  VkSemaphore                                   renderFinishedSemaphore      = VK_NULL_HANDLE;


  std::function<void(std::shared_ptr<Surface>)> eventSurfaceRenderStart;
  std::function<void(std::shared_ptr<Surface>)> eventSurfaceRenderFinish;

  void                                          createSwapChain();
  bool                                          checkWorkflow();
};

bool                         Surface::isRealized() const                                                               { return realized; }
void                         Surface::setID(uint32_t newID)                                                            { id = newID; }
uint32_t                     Surface::getID() const                                                                    { return id; }
uint32_t                     Surface::getImageCount() const                                                            { return surfaceTraits.imageCount; }
uint32_t                     Surface::getImageIndex() const                                                            { return swapChainImageIndex; }
void                         Surface::setEventSurfaceRenderStart(std::function<void(std::shared_ptr<Surface>)> event)  { eventSurfaceRenderStart = event; }
void                         Surface::setEventSurfaceRenderFinish(std::function<void(std::shared_ptr<Surface>)> event) { eventSurfaceRenderFinish = event; }
void                         Surface::onEventSurfaceRenderStart()                                                      { if (eventSurfaceRenderStart != nullptr)  eventSurfaceRenderStart(shared_from_this()); }
void                         Surface::onEventSurfaceRenderFinish()                                                     { if (eventSurfaceRenderFinish != nullptr)  eventSurfaceRenderFinish(shared_from_this()); }

}

