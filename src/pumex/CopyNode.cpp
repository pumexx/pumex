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

#include <pumex/CopyNode.h>
#include <pumex/NodeVisitor.h>
#include <pumex/utils/Log.h>

using namespace pumex;

void CopyNode::accept(NodeVisitor& visitor)
{
  if (visitor.getMask() && mask)
  {
    visitor.push(this);
    visitor.apply(*this);
    visitor.pop();
  }
}

ImageCopyRegion::ImageCopyRegion(const ImageSubresourceRange& ir, const glm::ivec3& off0, const glm::ivec3& off1)
  : imageRange{ ir }, offset0{ off0 }, offset1{ off1 }
{
}

ImageCopyData::ImageCopyData(const std::string& name, VkImageLayout il, const std::vector<ImageCopyRegion>& regs)
  : imageName{ name }, layout{ il }, regions( regs )
{
  CHECK_LOG_THROW(name.empty(), "ImageCopyData : Name was not defined");
  CHECK_LOG_THROW(regs.empty(), "ImageCopyData : No regions to copy");
}

ImageCopyData::ImageCopyData(std::shared_ptr<MemoryImage> mi, VkImageLayout il, const std::vector<ImageCopyRegion>& regs)
  : memoryImage{ mi }, layout{ il }, regions(regs)
{
  CHECK_LOG_THROW(mi==nullptr,  "ImageCopyData : Memory image was not defined");
  CHECK_LOG_THROW(regs.empty(), "ImageCopyData : No regions to copy");
}
