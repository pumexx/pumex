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
#include <pumex/Export.h>
#include <pumex/RenderGraph.h>
#include <pumex/RenderGraphExecution.h>

namespace pumex
{

class PUMEX_EXPORT RenderGraphCompiler
{
public:
  virtual std::shared_ptr<RenderGraphExecutable> compile(const RenderGraph& renderGraph, const ExternalMemoryObjects& externalMemoryObjects, const std::vector<QueueTraits>& queueTraits, std::shared_ptr<DeviceMemoryAllocator> frameBufferAllocator) = 0;
};

class PUMEX_EXPORT DefaultRenderGraphCompiler : public RenderGraphCompiler
{
public:
  std::shared_ptr<RenderGraphExecutable> compile(const RenderGraph& renderGraph, const ExternalMemoryObjects& externalMemoryObjects, const std::vector<QueueTraits>& queueTraits, std::shared_ptr<DeviceMemoryAllocator> frameBufferAllocator) override;
private:
  std::vector<std::reference_wrapper<const RenderOperation>>              calculatePartialOrdering(const RenderGraph& renderGraph);
  std::vector<std::vector<std::reference_wrapper<const RenderOperation>>> scheduleOperations(const RenderGraph& renderGraph, const std::vector<std::reference_wrapper<const RenderOperation>>& partialOrdering, const std::vector<QueueTraits>& queueTraits);
  void                                                                    buildCommandSequences(const RenderGraph& renderGraph, const std::vector<std::vector<std::reference_wrapper<const RenderOperation>>>& scheduledOperations, std::shared_ptr<RenderGraphExecutable> executable);
  void                                                                    buildImageInfo(const RenderGraph& renderGraph, const std::vector<std::reference_wrapper<const RenderOperation>>& partialOrdering, std::shared_ptr<RenderGraphExecutable> executable);
  void                                                                    buildFrameBuffersAndRenderPasses(const RenderGraph& renderGraph, const std::vector<std::reference_wrapper<const RenderOperation>>& partialOrdering, std::shared_ptr<RenderGraphExecutable> executable);
  void                                                                    buildPipelineBarriers(const RenderGraph& renderGraph, std::shared_ptr<RenderGraphExecutable> executable);
  void                                                                    createSubpassDependency(const RenderGraph& renderGraph, const ResourceTransition& generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, const ResourceTransition& consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex, std::shared_ptr<RenderGraphExecutable> executable);
  void                                                                    createPipelineBarrier(const RenderGraph& renderGraph, const ResourceTransition& generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, const ResourceTransition& consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex, std::shared_ptr<RenderGraphExecutable> executable);
};

PUMEX_EXPORT VkImageAspectFlags getAspectMask(AttachmentType at);
PUMEX_EXPORT VkImageUsageFlags  getAttachmentUsage(VkImageLayout imageLayout);
PUMEX_EXPORT void               getPipelineStageMasks(const ResourceTransition& generatingTransition, const ResourceTransition& consumingTransition, VkPipelineStageFlags& srcStageMask, VkPipelineStageFlags& dstStageMask);
PUMEX_EXPORT void               getAccessMasks(const ResourceTransition& generatingTransition, const ResourceTransition& consumingTransition, VkAccessFlags& srcAccessMask, VkAccessFlags& dstAccessMask);

}
