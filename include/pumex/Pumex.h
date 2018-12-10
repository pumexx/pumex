//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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
#include <vulkan/vulkan.h>
#include <pumex/Version.h>
#include <pumex/utils/Log.h>
#include <pumex/HPClock.h>
#include <pumex/Viewer.h>
#include <pumex/InputEvent.h>
#include <pumex/StandardHandlers.h>
#include <pumex/PhysicalDevice.h>
#include <pumex/Device.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/RenderGraph.h>
#include <pumex/Node.h>
#include <pumex/NodeVisitor.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Image.h>
#include <pumex/Resource.h>
#include <pumex/Descriptor.h>
#include <pumex/MemoryImage.h>
#include <pumex/Sampler.h>
#include <pumex/CombinedImageSampler.h>
#include <pumex/SampledImage.h>
#include <pumex/StorageImage.h>
#include <pumex/InputAttachment.h>
#include <pumex/UniformBuffer.h>
#include <pumex/StorageBuffer.h>
#include <pumex/Pipeline.h>
#include <pumex/RenderPass.h>
#include <pumex/FrameBuffer.h>
#include <pumex/Command.h>
#include <pumex/Query.h>
#include <pumex/Asset.h>
#include <pumex/AssetBuffer.h>
#include <pumex/AssetNode.h>
#include <pumex/AssetBufferNode.h>
#include <pumex/MaterialSet.h>
#include <pumex/DispatchNode.h>
#include <pumex/Text.h>
#include <pumex/Camera.h>
#include <pumex/Kinematic.h>
