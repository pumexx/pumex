//
// Copyright(c) 2017-2018 Paweł Księżopolski ( pumexx )
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
#include <string>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Node.h>
#include <pumex/ResourceRange.h>

namespace pumex
{

// This Node class is predecessor to all classes that copy images and/or buffers

class MemoryImage;
class MemoryBuffer;

class PUMEX_EXPORT CopyNode : public Node
{
public:
  void accept(NodeVisitor& visitor) override;

  virtual void cmdCopy(const RenderContext& renderContext, CommandBuffer* commandBuffer) = 0;
};

struct PUMEX_EXPORT ImageCopyRegion
{
  ImageCopyRegion(const ImageSubresourceRange& imageRange, const glm::ivec3& offset0, const glm::ivec3& offset1);
  ImageSubresourceRange imageRange;
  glm::ivec3 offset0;
  glm::ivec3 offset1;
};

struct PUMEX_EXPORT ImageCopyData
{
  ImageCopyData(const std::string& imageName, VkImageLayout layout, const std::vector<ImageCopyRegion>& regions);
  ImageCopyData(std::shared_ptr<MemoryImage> memoryImage, VkImageLayout layout, const std::vector<ImageCopyRegion>& regions);

  std::string                  imageName;
  std::shared_ptr<MemoryImage> memoryImage;
  VkImageLayout                layout;
  std::vector<ImageCopyRegion> regions;
};

}
