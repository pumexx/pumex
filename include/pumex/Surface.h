//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
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
class RenderPass;
class FrameBuffer;
class FrameBufferImages;
class RenderWorkflow;
class CommandPool;
class CommandBuffer;
class Image;
  
// struct representing information required to create a Vulkan surface
struct PUMEX_EXPORT SurfaceTraits
{
  explicit SurfaceTraits(uint32_t imageCount, VkColorSpaceKHR imageColorSpace, uint32_t imageArrayLayers, VkPresentModeKHR swapchainPresentMode, VkSurfaceTransformFlagBitsKHR preTransform, VkCompositeAlphaFlagBitsKHR compositeAlpha);

  void setRenderWorkflow(std::shared_ptr<RenderWorkflow> renderWorkflow);

  uint32_t                           imageCount;
  VkColorSpaceKHR                    imageColorSpace;
  uint32_t                           imageArrayLayers; // always 1 ( until VR )
  VkPresentModeKHR                   swapchainPresentMode;
  VkSurfaceTransformFlagBitsKHR      preTransform;
  VkCompositeAlphaFlagBitsKHR        compositeAlpha;

  std::shared_ptr<RenderWorkflow>    renderWorkflow;

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

  void            cleanup();
  void            beginFrame();
  void            validateGPUData(bool validateRenderGraphs);
  void            buildPrimaryCommandBuffer();
  void            draw();
  void            endFrame();
  void            resizeSurface();
  inline uint32_t getImageCount() const;
  inline uint32_t getImageIndex() const;

  inline void     setID(uint32_t newID);
  inline uint32_t getID() const;

    
  std::weak_ptr<Viewer>               viewer;
  std::weak_ptr<Window>               window;
  std::weak_ptr<Device>               device;

  VkSurfaceKHR                        surface                      = VK_NULL_HANDLE;
  SurfaceTraits                       surfaceTraits;
  std::shared_ptr<RenderWorkflow>     renderWorkflow;
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

  VkSemaphore                         imageAvailableSemaphore      = VK_NULL_HANDLE;
  VkSemaphore                         renderCompleteSemaphore      = VK_NULL_HANDLE;
  ActionQueue                         actions;

protected:
  uint32_t                            id                           = 0;
  VkSwapchainKHR                      swapChain                    = VK_NULL_HANDLE;
  std::vector<VkFence>                waitFences;
  std::shared_ptr<CommandBuffer>      primaryCommandBuffer;
  std::shared_ptr<CommandBuffer>      presentCommandBuffer;

protected:
  void createSwapChain();
};

void     Surface::setID(uint32_t newID) { id = newID; }
uint32_t Surface::getID() const         { return id; }
uint32_t Surface::getImageCount() const { return surfaceTraits.imageCount; }
uint32_t Surface::getImageIndex() const { return swapChainImageIndex; }


}

