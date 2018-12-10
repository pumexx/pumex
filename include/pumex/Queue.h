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
#include <limits>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>

namespace pumex
{

// struct that represents queues that must be provided by Vulkan implementation during initialization

enum QueueAssignment { qaShared = 0, qaExclusive = 1 };

class PUMEX_EXPORT QueueTraits
{
public:
  QueueTraits(VkQueueFlags mustHave, VkQueueFlags mustNotHave, float priority, QueueAssignment assignment);

  VkQueueFlags    mustHave    = 0;
  VkQueueFlags    mustNotHave = 0;
  float           priority;
  QueueAssignment assignment  = qaShared; // whether queue must be exclusive for the render graph or may be shared by few render graphs
};

inline bool operator==(const QueueTraits& lhs, const QueueTraits& rhs)
{
  return (lhs.mustHave == rhs.mustHave) && (lhs.mustNotHave == rhs.mustNotHave) && (lhs.priority == rhs.priority) && (lhs.assignment == rhs.assignment);
}

inline bool operator!=(const QueueTraits& lhs, const QueueTraits& rhs)
{
  return (lhs.mustHave != rhs.mustHave) || (lhs.mustNotHave != rhs.mustNotHave) || (lhs.priority != rhs.priority) || (lhs.assignment != rhs.assignment);
}

// internal class that stores infromation about one reserved queue
class Queue
{
public:
  Queue()                        = delete;
  Queue(const QueueTraits& queueTraits, uint32_t familyIndex, uint32_t index, VkQueue queue);
  Queue(const Queue&)            = delete;
  Queue& operator=(const Queue&) = delete;
  Queue(Queue&&)                 = delete;
  Queue& operator=(Queue&&)      = delete;

  QueueTraits traits;
  uint32_t    familyIndex = std::numeric_limits<uint32_t>::max();
  uint32_t    index       = std::numeric_limits<uint32_t>::max();
  bool        available   = true;
  VkQueue     queue       = VK_NULL_HANDLE;
};

}