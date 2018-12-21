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

#include <pumex/RenderContext.h>
#include <pumex/Device.h>
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/FrameBuffer.h>
#include <pumex/RenderPass.h>
#include <pumex/RenderGraph.h>

using namespace pumex;

RenderContext::RenderContext(Surface* s, uint32_t queueNumber)
  : surface { s }, vkSurface{ s->surface }, commandPool{ s->getCommandPool(queueNumber) }, queue{s->getQueue(queueNumber)}, vkQueue{ s->getQueue(queueNumber)->queue },
    device{ s->device.lock().get() }, vkDevice{ device->device }, descriptorPool{ device->getDescriptorPool().get() },
    activeIndex{ s->getImageIndex() }, imageCount{ s->getImageCount() }
{
}

void RenderContext::setRenderOperation(RenderOperation* ro)
{
  renderOperation = ro;
  if (ro != nullptr)
  {
    switch (ro->operationType)
    {
    case opGraphics:
    case opTransfer:
      currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      break;
    case opCompute:
      currentBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
      break;
    }
  }
  else
    currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
}
