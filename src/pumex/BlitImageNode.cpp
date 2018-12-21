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

#include <pumex/BlitImageNode.h>
#include <pumex/RenderGraphExecution.h>
#include <pumex/utils/Log.h>

using namespace pumex;

BlitImageNode::BlitImageNode(const ImageCopyData& src, const ImageCopyData& dst, VkFilter f)
  : srcImage{ src }, dstImage{ dst }, filter{ f }
{
  CHECK_LOG_THROW(srcImage.regions.size() != dstImage.regions.size(), "BlitImageNode : number of regions must be equal");
}

void BlitImageNode::validate(const RenderContext& renderContext)
{
  MemoryImage* srcImg = nullptr;
  if (!srcImage.imageName.empty())
    srcImg = renderContext.renderGraphExecutable->getMemoryImage(renderContext.renderOperation->name, srcImage.imageName).get();
  else
    srcImg = srcImage.memoryImage.get();
  CHECK_LOG_THROW(srcImg == nullptr, "BlitImageNode::cmdCopy : srcImage not defined");
  srcImg->validate(renderContext);
  MemoryImage* dstImg = nullptr;
  if (!dstImage.imageName.empty())
    dstImg = renderContext.renderGraphExecutable->getMemoryImage(renderContext.renderOperation->name, dstImage.imageName).get();
  else
    dstImg = dstImage.memoryImage.get();
  CHECK_LOG_THROW(dstImg == nullptr, "BlitImageNode::cmdCopy : dstImage not defined");
  dstImg->validate(renderContext);
}

void BlitImageNode::cmdCopy(const RenderContext& renderContext, CommandBuffer* commandBuffer)
{
  MemoryImage* srcImg = nullptr;
  if (!srcImage.imageName.empty())
    srcImg = renderContext.renderGraphExecutable->getMemoryImage(renderContext.renderOperation->name, srcImage.imageName).get();
  else
    srcImg = srcImage.memoryImage.get();
  CHECK_LOG_THROW(srcImg == nullptr, "BlitImageNode::cmdCopy : srcImage not defined");
  MemoryImage* dstImg = nullptr;
  if (!dstImage.imageName.empty())
    dstImg = renderContext.renderGraphExecutable->getMemoryImage(renderContext.renderOperation->name, dstImage.imageName).get();
  else
    dstImg = dstImage.memoryImage.get();
  CHECK_LOG_THROW(dstImg == nullptr, "BlitImageNode::cmdCopy : dstImage not defined");

  std::vector<VkImageBlit> imageBlits;
  for (uint32_t i = 0; i < srcImage.regions.size(); ++i)
  {
    VkImageBlit imageBlit;
    imageBlit.srcSubresource  = srcImage.regions[i].imageRange.getSubresourceLayers();
    imageBlit.srcOffsets[0].x = srcImage.regions[i].offset0.x;
    imageBlit.srcOffsets[0].y = srcImage.regions[i].offset0.y;
    imageBlit.srcOffsets[0].z = srcImage.regions[i].offset0.z;
    imageBlit.srcOffsets[1].x = srcImage.regions[i].offset1.x;
    imageBlit.srcOffsets[1].y = srcImage.regions[i].offset1.y;
    imageBlit.srcOffsets[1].z = srcImage.regions[i].offset1.z;
    imageBlit.dstSubresource  = dstImage.regions[i].imageRange.getSubresourceLayers();
    imageBlit.dstOffsets[0].x = dstImage.regions[i].offset0.x;
    imageBlit.dstOffsets[0].y = dstImage.regions[i].offset0.y;
    imageBlit.dstOffsets[0].z = dstImage.regions[i].offset0.z;
    imageBlit.dstOffsets[1].x = dstImage.regions[i].offset1.x;
    imageBlit.dstOffsets[1].y = dstImage.regions[i].offset1.y;
    imageBlit.dstOffsets[1].z = dstImage.regions[i].offset1.z;
    imageBlits.push_back(imageBlit);

    // FIXME : previous layout is unknown : use VK_IMAGE_LAYOUT_UNDEFINED
    commandBuffer->setImageLayout(*(dstImg->getImage(renderContext)), dstImage.regions[i].imageRange.aspectMask, VK_IMAGE_LAYOUT_UNDEFINED, dstImage.layout, dstImage.regions[i].imageRange.getSubresource());
  }
  commandBuffer->cmdBlitImage(*(srcImg->getImage(renderContext)), srcImage.layout, *(dstImg->getImage(renderContext)), dstImage.layout, imageBlits, filter);
}




