#pragma once
#include <memory>
#include <vector>
#include <gli/texture.hpp>
#include <vulkan/vulkan.h>
#include <pumex/RenderPass.h>
#include <pumex/Export.h>
#include <pumex/Pipeline.h>

namespace pumex
{

class Surface;
class Image;

struct PUMEX_EXPORT FrameBufferImageDefinition
{
  enum Type { SwapChain, Depth, Color };
  FrameBufferImageDefinition(Type type, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectMask, VkSampleCountFlagBits samples, const gli::swizzles& swizzles = gli::swizzles(gli::swizzle::SWIZZLE_RED, gli::swizzle::SWIZZLE_GREEN, gli::swizzle::SWIZZLE_BLUE, gli::swizzle::SWIZZLE_ALPHA));
  Type                  type;
  VkFormat              format;
  VkImageUsageFlags     usage;
  VkImageAspectFlags    aspectMask;
  VkSampleCountFlagBits samples;
  gli::swizzles         swizzles;  
};

class PUMEX_EXPORT FrameBufferImages
{
public:
  FrameBufferImages()                                    = delete;
  explicit FrameBufferImages(const std::vector<FrameBufferImageDefinition>& fbid);
  FrameBufferImages(const FrameBufferImages&)            = delete;
  FrameBufferImages& operator=(const FrameBufferImages&) = delete;
  virtual ~FrameBufferImages();

  void validate(std::shared_ptr<Surface> surface);
  void reset(std::shared_ptr<Surface> surface);
  Image* getImage(std::shared_ptr<Surface> surface, uint32_t imageIndex);
  FrameBufferImageDefinition getSwapChainDefinition();


  std::vector<FrameBufferImageDefinition> imageDefinitions;
protected:
  struct PerSurfaceData
  {
    PerSurfaceData(std::shared_ptr<Surface> s, uint32_t imCount)
      : surface{ s }
    {
      frameBufferImages.resize(imCount);
    }
    std::shared_ptr<Surface>            surface;
    std::vector<std::shared_ptr<Image>> frameBufferImages;
    bool                                dirty = true;
  };
  std::unordered_map<VkSurfaceKHR, PerSurfaceData> perSurfaceData;

};

// FIXME : as for now the extent of a frame buffer is the same as the extent of a surface.
// These two extents should be independent

class PUMEX_EXPORT FrameBuffer
{
public:
  FrameBuffer()                              = delete;
  explicit FrameBuffer( std::shared_ptr<RenderPass> renderPass, std::shared_ptr<FrameBufferImages> frameBufferImages );
  FrameBuffer(const FrameBuffer&)            = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;
  virtual ~FrameBuffer();

  void reset(std::shared_ptr<Surface> surface);
  void validate(std::shared_ptr<Surface> surface, const std::vector<std::unique_ptr<Image>>& swapChainImages = std::vector<std::unique_ptr<Image>>());
  VkFramebuffer getFrameBuffer(std::shared_ptr<Surface> surface, uint32_t fbIndex);

  std::weak_ptr<RenderPass>         renderPass;
  std::weak_ptr<FrameBufferImages>  frameBufferImages;
protected:
  struct PerSurfaceData
  {
    PerSurfaceData(std::shared_ptr<Surface> s, uint32_t fbCount)
      : surface{ s }
    {
      frameBuffers.resize(fbCount, VK_NULL_HANDLE);
    }
    std::shared_ptr<Surface>            surface;
    std::vector<VkFramebuffer>          frameBuffers;
    bool                                dirty = true;
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