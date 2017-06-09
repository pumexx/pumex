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
class RenderPass;
class FrameBuffer;
class FrameBufferImages;
class CommandPool;
class CommandBuffer;
class Image;
  
// struct representing information required to create a Vulkan surface
struct PUMEX_EXPORT SurfaceTraits
{
  explicit SurfaceTraits(uint32_t imageCount, VkColorSpaceKHR imageColorSpace, uint32_t imageArrayLayers, VkPresentModeKHR swapchainPresentMode, VkSurfaceTransformFlagBitsKHR preTransform, VkCompositeAlphaFlagBitsKHR compositeAlpha);
  void setDefaultRenderPass(std::shared_ptr<RenderPass> renderPass);
  void setFrameBufferImages(std::shared_ptr<FrameBufferImages> frameBufferImages);
  void definePresentationQueue( const QueueTraits& queueTraits );

  uint32_t                           imageCount;
  VkColorSpaceKHR                    imageColorSpace;
  uint32_t                           imageArrayLayers; // always 1 ( until VR )
  VkPresentModeKHR                   swapchainPresentMode;
  VkSurfaceTransformFlagBitsKHR      preTransform;
  VkCompositeAlphaFlagBitsKHR        compositeAlpha;
  QueueTraits                        presentationQueueTraits;

  std::shared_ptr<RenderPass>        defaultRenderPass;
  std::shared_ptr<FrameBufferImages> frameBufferImages;
};

// class representing a Vulkan surface
class PUMEX_EXPORT Surface : public std::enable_shared_from_this<Surface>
{
public:
  Surface()                          = delete;
  explicit Surface(std::shared_ptr<Viewer> viewer, std::shared_ptr<Window> window, std::shared_ptr<Device> device, VkSurfaceKHR surface, const SurfaceTraits& surfaceTraits);
  Surface(const Surface&)            = delete;
  Surface& operator=(const Surface&) = delete;
  virtual ~Surface();

  void cleanup();
  void beginFrame();
  void endFrame();
  void resizeSurface(uint32_t newWidth, uint32_t newHeight);
  inline uint32_t getImageCount() const;
  inline uint32_t getImageIndex() const;
  VkFramebuffer getCurrentFrameBuffer();
    
  std::weak_ptr<Viewer>               viewer;
  std::weak_ptr<Window>               window;
  std::weak_ptr<Device>               device;

  VkSurfaceKHR                        surface                      = VK_NULL_HANDLE;
  SurfaceTraits                       surfaceTraits;
  VkSurfaceCapabilitiesKHR            surfaceCapabilities;
  std::vector<VkPresentModeKHR>       presentModes;
  std::vector<VkSurfaceFormatKHR>     surfaceFormats;
  std::vector<VkBool32>               supportsPresent;

  VkQueue                             presentationQueue            = VK_NULL_HANDLE;
  uint32_t                            presentationQueueFamilyIndex = UINT32_MAX;
  uint32_t                            presentationQueueIndex       = UINT32_MAX;
  std::shared_ptr<CommandPool>        commandPool;

  VkExtent2D                          swapChainSize                = VkExtent2D{1,1};
  uint32_t                            swapChainImageIndex          = 0;
  std::vector<std::unique_ptr<Image>> swapChainImages;

  std::unique_ptr<FrameBuffer>        frameBuffer;

  VkSemaphore                         imageAvailableSemaphore      = VK_NULL_HANDLE;
  VkSemaphore                         renderCompleteSemaphore      = VK_NULL_HANDLE;
  ActionQueue                         actions;

  std::shared_ptr<RenderPass>         defaultRenderPass;
  std::shared_ptr<FrameBufferImages>  frameBufferImages;
protected:
  VkSwapchainKHR                      swapChain                    = VK_NULL_HANDLE;
  std::vector<VkFence>                waitFences;
  std::vector<std::shared_ptr<CommandBuffer>> prePresentCmdBuffers;

protected:
  void createSwapChain();
};

uint32_t Surface::getImageCount() const { return surfaceTraits.imageCount; }
uint32_t Surface::getImageIndex() const { return swapChainImageIndex; }


}

