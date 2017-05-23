#pragma once

#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Device.h>
#include <pumex/Pipeline.h>
#include <pumex/utils/ActionQueue.h>

namespace pumex
{

class Viewer;
class Window;
class RenderPass;
class CommandPool;
class CommandBuffer;
class Image;
  
// struct representing information required to create a Vulkan surface
struct PUMEX_EXPORT SurfaceTraits
{
  explicit SurfaceTraits(uint32_t imageCount, VkFormat imageFormat, VkColorSpaceKHR imageColorSpace, uint32_t imageArrayLayers, VkFormat depthFormat, VkPresentModeKHR swapchainPresentMode, VkSurfaceTransformFlagBitsKHR preTransform, VkCompositeAlphaFlagBitsKHR compositeAlpha);
  void setDefaultRenderPass(std::shared_ptr<pumex::RenderPass> renderPass);
  void definePresentationQueue( const pumex::QueueTraits& queueTraits );

  uint32_t                           imageCount;
  VkFormat                           imageFormat;
  VkColorSpaceKHR                    imageColorSpace;
  uint32_t                           imageArrayLayers; // always 1 ( until VR )
  VkFormat                           depthFormat;
  VkPresentModeKHR                   swapchainPresentMode;
  VkSurfaceTransformFlagBitsKHR      preTransform;
  VkCompositeAlphaFlagBitsKHR        compositeAlpha;
  pumex::QueueTraits                 presentationQueueTraits;
  std::shared_ptr<pumex::RenderPass> defaultRenderPass;
};

// class representing a Vulkan surface
class PUMEX_EXPORT Surface
{
public:
  Surface()                          = delete;
  explicit Surface(std::shared_ptr<pumex::Viewer> viewer, std::shared_ptr<pumex::Window> window, std::shared_ptr<pumex::Device> device, VkSurfaceKHR surface, const pumex::SurfaceTraits& surfaceTraits);
  Surface(const Surface&)            = delete;
  Surface& operator=(const Surface&) = delete;
  virtual ~Surface();

  void cleanup();
  void beginFrame();
  void endFrame();
  void resizeSurface(uint32_t newWidth, uint32_t newHeight);
  inline uint32_t getImageCount() const;
  inline uint32_t getImageIndex() const;
  inline VkFramebuffer getCurrentFrameBuffer() const;
    
  std::weak_ptr<pumex::Viewer>    viewer;
  std::weak_ptr<pumex::Window>    window;
  std::weak_ptr<pumex::Device>    device;
  VkSurfaceKHR                    surface                      = VK_NULL_HANDLE;
  pumex::SurfaceTraits            surfaceTraits;
  VkSurfaceCapabilitiesKHR        surfaceCapabilities;
  std::vector<VkPresentModeKHR>   presentModes;
  std::vector<VkSurfaceFormatKHR> surfaceFormats;
  std::vector<VkBool32>           supportsPresent;

  VkQueue                             presentationQueue            = VK_NULL_HANDLE;
  uint32_t                            presentationQueueFamilyIndex = UINT32_MAX;
  uint32_t                            presentationQueueIndex       = UINT32_MAX;
  std::shared_ptr<pumex::CommandPool> commandPool;

  VkExtent2D                          swapChainSize                = VkExtent2D{1,1};
  uint32_t                            swapChainImageIndex          = 0;
  std::vector<std::unique_ptr<Image>> swapChainImages;
  std::vector<std::unique_ptr<Image>> frameBufferImages;

  VkSemaphore                     imageAvailableSemaphore      = VK_NULL_HANDLE;
  VkSemaphore                     renderCompleteSemaphore      = VK_NULL_HANDLE;
  pumex::ActionQueue              actions;
  std::shared_ptr<pumex::RenderPass> defaultRenderPass;
protected:
  VkSwapchainKHR                  swapChain                    = VK_NULL_HANDLE;
  std::vector<VkFramebuffer>      frameBuffers;
  std::vector<VkFence>            waitFences;
  std::vector<std::shared_ptr<pumex::CommandBuffer>> postPresentCmdBuffers;
  std::vector<std::shared_ptr<pumex::CommandBuffer>> prePresentCmdBuffers;

protected:
  void createSwapChain();
};

class PUMEX_EXPORT InputAttachment : public DescriptorSetSource
{
public:
  InputAttachment(uint32_t frameBufferIndex);

  void validate(std::shared_ptr<Surface> surface);
  void getDescriptorSetValues(VkSurfaceKHR surface, std::vector<DescriptorSetValue>& values) const override;
protected:
  uint32_t frameBufferIndex;

  struct PerSurfaceData
  {
    PerSurfaceData(std::shared_ptr<Surface> s)
      : surface{ s }
    {
    }
    std::weak_ptr<Surface> surface;
    bool                   dirty    = true;
  };
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
};

uint32_t Surface::getImageCount() const { return surfaceTraits.imageCount; }
uint32_t Surface::getImageIndex() const { return swapChainImageIndex; }

VkFramebuffer Surface::getCurrentFrameBuffer() const { return frameBuffers[swapChainImageIndex]; }

}

