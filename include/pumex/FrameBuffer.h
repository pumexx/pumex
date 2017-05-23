#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <pumex/RenderPass.h>
#include <pumex/Export.h>
#include <pumex/Pipeline.h>

namespace pumex
{

class Surface;
class Image;

// FIXME : as for now the extent of a frame buffer is the same as the extent of a surface.
// These two extents should be independent

class PUMEX_EXPORT FrameBuffer
{
public:
  FrameBuffer()                              = delete;
  explicit FrameBuffer( std::shared_ptr<RenderPass> renderPass );
  FrameBuffer(const FrameBuffer&)            = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;
  virtual ~FrameBuffer();

  void reset(std::shared_ptr<Surface> surface);
  void validate(std::shared_ptr<Surface> surface, const std::vector<std::unique_ptr<Image>>& swapChainImages = std::vector<std::unique_ptr<Image>>());
  VkFramebuffer getFrameBuffer(std::shared_ptr<Surface> surface, uint32_t fbIndex);
  Image* getImage(std::shared_ptr<Surface> surface, uint32_t imageIndex);

  std::weak_ptr<RenderPass>         renderPass;
protected:
  struct PerSurfaceData
  {
    PerSurfaceData(uint32_t fbCount, uint32_t imCount, VkDevice d)
      : device{ d }
    {
      frameBuffers.resize(fbCount, VK_NULL_HANDLE);
      frameBufferImages.resize(imCount);
    }
    std::vector<VkFramebuffer>          frameBuffers;
    std::vector<std::shared_ptr<Image>> frameBufferImages; // FIXME : cannot use unique_ptr for some reason...
    bool                                dirty = true;
    VkDevice                            device;
  };
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
};

class PUMEX_EXPORT InputAttachment : public DescriptorSetSource
{
public:
  InputAttachment(std::shared_ptr<FrameBuffer> frameBuffer, uint32_t frameBufferIndex);

  void validate(std::shared_ptr<Surface> surface);
  void getDescriptorSetValues(VkSurfaceKHR surface, std::vector<DescriptorSetValue>& values) const override;
protected:
  std::shared_ptr<FrameBuffer> frameBuffer;
  uint32_t frameBufferIndex;

  struct PerSurfaceData
  {
    PerSurfaceData(std::shared_ptr<Surface> s)
      : surface{ s }
    {
    }
    std::weak_ptr<Surface> surface;
    bool                   dirty = true;
  };
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;
};


}