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
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/Command.h>
#include <pumex/Resource.h>

namespace pumex
{

class Device;
class Surface;
class Node;
class RenderContext;

// A set of classes implementing different Vulkan pipeline elements

// descriptor set layout creation
struct PUMEX_EXPORT DescriptorSetLayoutBinding
{
  DescriptorSetLayoutBinding(uint32_t binding, uint32_t bindingCount, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags);
  uint32_t            binding        = 0;
  uint32_t            bindingCount   = 1;
  VkDescriptorType    descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
  VkShaderStageFlags  stageFlags     = VK_SHADER_STAGE_ALL_GRAPHICS; // VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_ALL_GRAPHICS
};


class PUMEX_EXPORT DescriptorSetLayout
{
public:
  DescriptorSetLayout()                                      = delete;
  explicit DescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& bindings);
  DescriptorSetLayout(const DescriptorSetLayout&)            = delete;
  DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
  virtual ~DescriptorSetLayout();

  void                  validate(const RenderContext& renderContext);
  VkDescriptorSetLayout getHandle(VkDevice device) const;
  VkDescriptorType      getDescriptorType(uint32_t binding) const;
  uint32_t              getDescriptorBindingCount(uint32_t binding) const;

  std::vector<DescriptorSetLayoutBinding> bindings;
protected:
  struct PerDeviceData
  {
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  };
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

class PUMEX_EXPORT DescriptorPool
{
public:
  explicit DescriptorPool(uint32_t poolSize, const std::vector<DescriptorSetLayoutBinding>& bindings);
  DescriptorPool(const DescriptorPool&)            = delete;
  DescriptorPool& operator=(const DescriptorPool&) = delete;
  virtual ~DescriptorPool();

  void             validate(const RenderContext& renderContext);
  VkDescriptorPool getHandle(VkDevice device) const;

  uint32_t poolSize;
  std::vector<DescriptorSetLayoutBinding> bindings;
protected:
  struct PerDeviceData
  {
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  };
  mutable std::mutex                          mutex;
  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
};

class DescriptorSet;
struct DescriptorSetValue;

class PUMEX_EXPORT Descriptor : public std::enable_shared_from_this<Descriptor>
{
public:
  Descriptor(std::shared_ptr<DescriptorSet> owner, std::shared_ptr<Resource> resource, VkDescriptorType descriptorType);
  Descriptor(std::shared_ptr<DescriptorSet> owner, const std::vector<std::shared_ptr<Resource>>& resources, VkDescriptorType descriptorType);
  ~Descriptor();

  void registerInResources();
  void unregisterFromResources();

  void validate(const RenderContext& renderContext);
  void invalidateDescriptorSet();
  void notifyDescriptorSet(const RenderContext& renderContext);
  void getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const;

  std::weak_ptr<DescriptorSet>           owner;
  std::vector<std::shared_ptr<Resource>> resources;
  VkDescriptorType                       descriptorType;
};


class PUMEX_EXPORT DescriptorSet : public CommandBufferSource
{
public:
  DescriptorSet()                                = delete;
  explicit DescriptorSet(std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<DescriptorPool> pool);
  DescriptorSet(const DescriptorSet&)            = delete;
  DescriptorSet& operator=(const DescriptorSet&) = delete;
  virtual ~DescriptorSet();

  void                        validate(const RenderContext& renderContext);
  void                        invalidateOwners();
  void                        notify(const RenderContext& renderContext);
  void                        notify();

  void                        setDescriptor(uint32_t binding, const std::vector<std::shared_ptr<Resource>>& resources, VkDescriptorType descriptorType);
  void                        setDescriptor(uint32_t binding, const std::vector<std::shared_ptr<Resource>>& resources);
  void                        setDescriptor(uint32_t binding, std::shared_ptr<Resource> resource, VkDescriptorType descriptorType);
  void                        setDescriptor(uint32_t binding, std::shared_ptr<Resource> resource);
  void                        resetDescriptor(uint32_t binding);
  std::shared_ptr<Descriptor> getDescriptor(uint32_t binding);

  void                        addNode(std::shared_ptr<Node> node);
  void                        removeNode(std::shared_ptr<Node> node);

  VkDescriptorSet             getHandle(const RenderContext& renderContext) const;

  std::shared_ptr<DescriptorSetLayout> layout;
  std::shared_ptr<DescriptorPool>      pool;
protected:
  struct DescriptorSetInternal
  {
    DescriptorSetInternal()
      : descriptorSet{ VK_NULL_HANDLE }
    {
    }
    VkDescriptorSet descriptorSet;
  };
  typedef PerObjectData<DescriptorSetInternal, uint32_t> DescriptorSetData;

  mutable std::mutex                                        mutex;
  std::unordered_map<uint32_t, DescriptorSetData>           perObjectData;
  std::unordered_map<uint32_t, std::shared_ptr<Descriptor>> descriptors; // descriptor set indirectly owns buffers, images and whatnot
  std::vector<std::weak_ptr<Node>>                          nodeOwners;
  uint32_t                                                  activeCount = 1;
};

}