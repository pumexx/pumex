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
#include <pumex/RenderGraphCompiler.h>
#include <numeric>
#include <pumex/RenderPass.h>
#include <pumex/FrameBuffer.h>

using namespace pumex;

std::shared_ptr<RenderGraphExecutable> DefaultRenderGraphCompiler::compile(const RenderGraph& renderGraph, const ExternalMemoryObjects& externalMemoryObjects, const std::vector<QueueTraits>& queueTraits, std::shared_ptr<DeviceMemoryAllocator> frameBufferAllocator)
{
  // calculate partial ordering
  auto partialOrdering = calculatePartialOrdering(renderGraph);

  // build the results storage
  auto executable = std::make_shared<RenderGraphExecutable>();
  executable->name                  = renderGraph.name;
  executable->queueTraits           = queueTraits;
  executable->frameBufferAllocator  = frameBufferAllocator;
  executable->setExternalMemoryObjects(renderGraph, externalMemoryObjects);

  // we are scheduling operations according to queue traits and partial ordering
  auto operationSchedule = scheduleOperations(renderGraph, partialOrdering, queueTraits);

  // build render commands and render passes
  buildCommandSequences(renderGraph, operationSchedule, executable);
  
  // build information about all images used in a graph, find aliased attachments
  buildImageInfo(renderGraph, partialOrdering, executable);

  // Build framebuffer for each render pass
  // TODO : specification is not clear what compatible render passes are. Neither are debug layers. One day I will decrease the number of frame buffers
  buildFrameBuffersAndRenderPasses(renderGraph, partialOrdering, executable);

  // build pipeline barriers and subpass dependencies and events ( semaphores ?)
  buildPipelineBarriers(renderGraph, executable);

  return executable;
}

std::vector<std::reference_wrapper<const RenderOperation>> DefaultRenderGraphCompiler::calculatePartialOrdering(const RenderGraph& renderGraph)
{
  std::vector<std::reference_wrapper<const RenderOperation>> partialOrdering;

  std::set<uint32_t> visitedTransitions;
  auto nextOperations = getInitialOperations(renderGraph);
  RenderOperationSet doneOperations;

  while (!nextOperations.empty())
  {
    decltype(nextOperations) nextOperations2;
    for (auto operation : nextOperations)
    {
      // if operation has no inputs, or all inputs are on existingResources then operation may be added to partial ordering
      auto inTransitions = renderGraph.getOperationIO(operation.get().name, opeAllInputs);
      uint32_t notVisitedInputCount = std::count_if(begin(inTransitions), end(inTransitions),
        [&visitedTransitions](const ResourceTransition& transition) { return visitedTransitions.find(transition.tid()) == end(visitedTransitions); });
      if (notVisitedInputCount == 0)
      {
        // operation is performed - add it to partial ordering
        partialOrdering.push_back(operation);
        doneOperations.insert(operation);
        // mark output resources as existing
        auto outTransitions = renderGraph.getOperationIO(operation.get().name, opeAllOutputs);
        for (auto outTransition : outTransitions)
          visitedTransitions.insert(outTransition.get().tid());
        // add next operations to nextOperations2
        auto follow = getNextOperations(renderGraph, operation.get().name);
        std::copy(begin(follow), end(follow), std::inserter(nextOperations2, end(nextOperations2)));
      }
    }
    nextOperations.clear();
    std::copy_if(begin(nextOperations2), end(nextOperations2), std::inserter(nextOperations, end(nextOperations)), [&doneOperations](const RenderOperation& op) { return doneOperations.find(op) == end(doneOperations); });
  }
  return partialOrdering;
}

std::vector<std::vector<std::reference_wrapper<const RenderOperation>>> DefaultRenderGraphCompiler::scheduleOperations(const RenderGraph& renderGraph, const std::vector<std::reference_wrapper<const RenderOperation>>& partialOrdering, const std::vector<QueueTraits>& queueTraits)
{
  // calculate transition cost 
  std::map<uint32_t, float> transitionCost;
  for (const auto& transition : renderGraph.transitions)
  {
    auto it = transitionCost.find(transition.tid());
    if (it != end(transitionCost))
      continue;
    ImageSize     is = transition.operation().attachmentSize;
    OperationType ot = transition.operation().operationType;
    float totalCost = 0.0001f;
    auto others = renderGraph.getTransitionIO(transition.tid(), opeAllInputsOutputs);
    for (auto other : others)
    {
      float cost = 0.0f;
      if (transition.operation().operationType != ot)
        cost += 0.1f;
      if (transition.operation().attachmentSize != is)
        cost += 0.1f;
      totalCost = std::max(cost, totalCost);
    }
    transitionCost.insert( { transition.tid(), totalCost } );
  }

  // calculate operation cost
  std::map<std::string, float> operationCost;
  for (const auto& opit : renderGraph.operations)
  {
    float totalCost = 0.0001f;
    if (opit.second.attachmentSize.type == isSurfaceDependent)
      totalCost += std::max(opit.second.attachmentSize.size.x, opit.second.attachmentSize.size.y) * 0.1f;
    else
      totalCost += 0.01f;
    operationCost.insert( { opit.first, 1.0f } );
  }

  // Scheduling algorithm inspired by "Scheduling Algorithms for Allocating Directed Task Graphs for Multiprocessors" by Yu-Kwong Kwok and Ishfaq Ahmad
  // Calculate b-level
  std::map<std::string, float> bLevel;
  float bLevelMax = 0.0;
  for (auto it = rbegin(partialOrdering); it != rend(partialOrdering); ++it)
  {
    float maxVal = 0.0f;
    auto outTransitions = renderGraph.getOperationIO(it->get().name, opeAllOutputs);
    for (auto outTransition : outTransitions)
    {
      auto inTransitions = renderGraph.getTransitionIO(outTransition.get().tid(), opeAllInputs);
      float transCost = transitionCost[outTransition.get().tid()];
      for (auto inTransition : inTransitions)
        maxVal = std::max(maxVal, transCost + bLevel[inTransition.get().operation().name]);
    }
    float bLevelValue = operationCost[it->get().name] + maxVal;
    bLevel[it->get().name] = bLevelValue;
    bLevelMax = std::max(bLevelMax, bLevelValue);
  }

  std::vector<std::vector<std::reference_wrapper<const RenderOperation>>> results(queueTraits.size());
  std::vector<float>              queueEndTime(queueTraits.size(), 0.0f);
  std::map<std::string, float>    operationEndTime;

  std::deque<std::reference_wrapper<const RenderOperation>> readyList;
  auto sortMethodLambda = [&bLevel](const RenderOperation& lhs, const RenderOperation& rhs)->bool { return bLevel[lhs.name] > bLevel[rhs.name]; };

  auto initialOperations = getInitialOperations(renderGraph);
  std::copy(begin(initialOperations), end(initialOperations), std::back_inserter(readyList));
  std::sort(begin(readyList), end(readyList), sortMethodLambda);

  while (!readyList.empty())
  {
    auto scheduledOperation = readyList.front();
    readyList.pop_front();

    // find minimum execution time taking into account all previous operations ( previous operations are already scheduled )
    float minExecTime = 0.0;
    std::map<std::string, float> predecessors;
    auto inputTransitions = renderGraph.getOperationIO(scheduledOperation.get().name, opeAllInputs);
    for (auto inputTransition : inputTransitions)
    {
      float transCost = transitionCost[inputTransition.get().tid()];
      auto outputTransitions = renderGraph.getTransitionIO(inputTransition.get().tid(), opeAllOutputs);
      for (auto& outputTransition : outputTransitions)
      {
        float fullCost = operationEndTime[outputTransition.get().operationName()] + transCost;
        predecessors.insert({ outputTransition.get().operationName(), fullCost });
        minExecTime = std::min(minExecTime, fullCost);
      }
    }

    std::vector<uint32_t> q(queueTraits.size());
    std::iota(begin(q), end(q), 0);
    // skip queues that are unable to perform the operation
    OperationType opType = scheduledOperation.get().operationType;
    auto it = std::partition(begin(q), end(q), [&queueTraits, opType](uint32_t index) { return (queueTraits[index].mustHave & opType) != 0; });
    CHECK_LOG_THROW(it == begin(q), "No suitable queue for operation : " << scheduledOperation.get().name << ". Check available queue traits.");
    // prefer operations where last performed operation is a predecessor to currently scheduled one ( requires less synchronization between different queues )
    auto it2 = std::partition(begin(q), it, [&results, &predecessors](uint32_t index) {  return (!results[index].empty()) && (predecessors.find(results[index].back().get().name) != end(predecessors)); });
    uint32_t pickedQueue;
    float previousEndTime;
    // if there are queues with predecessors
    if (it2 != begin(q))
    {
      // sort predecessor list - pick the one that finishes last
      std::sort(begin(q), it2, [&queueEndTime](uint32_t lhs, uint32_t rhs) { return queueEndTime[lhs] > queueEndTime[rhs]; });
      pickedQueue = q[0];
      previousEndTime = predecessors.find(results[pickedQueue].back().get().name)->second;
    }
    else
    {
      // if there are no predecessors - we have 3 distinct types of queues
      // - idle queues ( no operation is performed atm )   - pick first available
      // - empty queues ( no operations submitted at all ) - pick first available
      // - queues with operation ongoing at the moment     - pick the one that finishes first
      auto it3 = std::partition(begin(q), it, [&queueEndTime, minExecTime](uint32_t index) { return queueEndTime[index] < minExecTime; });
      if (it3 != begin(q))
      {
        std::sort(begin(q), it3); // if there are some idle or empty queues we have to sort it by index ( std::partition may not preserve original order )
        pickedQueue = q[0];
        previousEndTime = minExecTime;
      }
      else
      {
        std::sort(it2, it, [&queueEndTime](uint32_t lhs, uint32_t rhs) { return queueEndTime[lhs] < queueEndTime[rhs]; }); // sort working queues by endTime
        pickedQueue = q[0];
        previousEndTime = queueEndTime[q[0]];
      }
    }

    // place operation in results
    results[pickedQueue].push_back(scheduledOperation);
    float endTime                                   = previousEndTime + operationCost[scheduledOperation.get().name];
    queueEndTime[pickedQueue]                       = endTime;
    operationEndTime[scheduledOperation.get().name] = endTime;

    auto nextOperations = getNextOperations(renderGraph, scheduledOperation.get().name);
    for (auto nextOperation : nextOperations)
    {
      auto previousOperations = getPreviousOperations(renderGraph, nextOperation.get().name);
      // if all previous operations are already scheduled - sent this operation to schedule queue
      if (std::all_of(begin(previousOperations), end(previousOperations), [&operationEndTime](const RenderOperation& op) { return operationEndTime.find(op.name) != end(operationEndTime); }))
        readyList.push_back(nextOperation);
    }
    std::sort(begin(readyList), end(readyList), sortMethodLambda);
  }
  LOG_INFO << "Operations schedule :";
  for (uint32_t i = 0; i < results.size(); ++i)
  {
    LOG_INFO << "\n";
    LOG_INFO << "Q" << i << " ( +" << queueTraits[i].mustHave << " -" << queueTraits[i].mustNotHave << " p:" << queueTraits[i].priority << "), ";
    for (uint32_t j = 0; j < results[i].size(); ++j)
      LOG_INFO << results[i][j].get().name << ", ";
  }
  LOG_INFO << "\n" << std::endl;
  return results;
}

void DefaultRenderGraphCompiler::buildCommandSequences(const RenderGraph& renderGraph, const std::vector<std::vector<std::reference_wrapper<const RenderOperation>>>& scheduledOperations, std::shared_ptr<RenderGraphExecutable> executable)
{
  for (const auto& schedule : scheduledOperations)
  {
    ImageSize                                   lastOperationSize;
    std::shared_ptr<RenderPass>                 lastRenderPass;
    std::vector<std::shared_ptr<RenderCommand>> commands;
    for (const auto& operation : schedule)
    {
      // we have a new set of operations from bit to it
      switch (operation.get().operationType)
      {
      case opGraphics:
      {
        if (lastOperationSize != operation.get().attachmentSize || lastRenderPass.get() == nullptr)
          lastRenderPass = std::make_shared<RenderPass>();

        std::shared_ptr<RenderSubPass> renderSubPass = std::make_shared<RenderSubPass>();
        renderSubPass->operation = operation;
        auto opTransitions = renderGraph.getOperationIO(operation.get().name, opeAllInputsOutputs);
        for (auto transition : opTransitions)
          renderSubPass->entries.insert({ transition.get().entryName(), transition.get().rteid() });
        
        lastRenderPass->addSubPass(renderSubPass);
        lastRenderPass->multiViewRenderPass = (operation.get().multiViewMask != 0);
        commands.push_back(renderSubPass);
        break;
      }
      case opCompute:
      {
        lastRenderPass = nullptr;

        std::shared_ptr<ComputePass> computePass = std::make_shared<ComputePass>();
        computePass->operation = operation;
        auto opTransitions = renderGraph.getOperationIO(operation.get().name, opeAllInputsOutputs);
        for (auto transition : opTransitions)
          computePass->entries.insert({ transition.get().entryName(), transition.get().rteid() });

        commands.push_back(computePass);
        break;
      }
      default:
        break;
      }
      lastOperationSize = operation.get().attachmentSize;
    }
    executable->commands.push_back(commands);
  }
}

std::vector<uint32_t> recursiveLongestPath(const std::vector<std::pair<uint32_t, uint32_t>>& resourcePairs, const std::set<uint32_t>& doneVertices = std::set<uint32_t>())
{
  std::set<uint32_t> vertices;
  if (doneVertices.empty())
  {
    for (auto& rp : resourcePairs)
      vertices.insert(rp.first);
    for (auto& rp : resourcePairs)
      vertices.erase(rp.second);
  }
  else
  {
    for (auto& rp : resourcePairs)
    {
      if (doneVertices.find(rp.first) == end(doneVertices))
        continue;
      if (doneVertices.find(rp.second) != end(doneVertices))
        continue;
      vertices.insert(rp.second);
    }
  }
  if (vertices.empty())
    return std::vector<uint32_t>();


  std::vector<std::vector<uint32_t>> results;
  for (const auto& x : vertices)
  {
    auto a = doneVertices;
    a.insert(x);
    auto xx = recursiveLongestPath(resourcePairs, a);
    xx.push_back(x);
    results.push_back(xx);
  }
  uint32_t maxSize = 0;
  int maxElt = -1;
  for (uint32_t i = 0; i<results.size(); ++i)
  {
    if (results[i].size() > maxSize)
    {
      maxSize = results[i].size();
      maxElt = i;
    }
  }
  return results[maxElt];
}

void DefaultRenderGraphCompiler::buildImageInfo(const RenderGraph& renderGraph, const std::vector<std::reference_wrapper<const RenderOperation>>& partialOrdering, std::shared_ptr<RenderGraphExecutable> executable)
{
  std::map<uint32_t, RenderGraphImageInfo>     imageInfo;
  std::map<std::string, uint32_t>              operationIndices;
  uint32_t operationIndex = 1;
  for( auto& op : partialOrdering )
  {
    operationIndices.insert({ op.get().name, operationIndex });
    // operations are ordered. Create vector with all sorted image transitions ( input transitions before output transitions )
    auto opTransitions  = renderGraph.getOperationIO(op.get().name, opeAllAttachmentInputs | opeImageInput);
    auto outTransitions = renderGraph.getOperationIO(op.get().name, opeAllAttachmentOutputs | opeImageOutput);
    std::copy(begin(outTransitions), end(outTransitions), std::back_inserter(opTransitions));
    for (auto transition : opTransitions)
    {
      auto it = imageInfo.find(transition.get().tid());
      if (it == end(imageInfo)) // if image is not in the imageInfo already - add it to vector, save its initial layout, guess layout before graph
      {
        RenderGraphImageInfo newInfo(
          transition.get().entry().resourceDefinition.attachment,
          transition.get().externalMemoryObjectName(),
          getAttachmentUsage(transition.get().entry().layout) | transition.get().entry().imageUsage,
          transition.get().entry().imageCreate,
          transition.get().entryName() == SWAPCHAIN_NAME,
          transition.get().externalMemoryObjectName().empty() ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL);
        it = imageInfo.insert({ transition.get().tid(), newInfo }).first;
      }
      else // accumulate image usage
      {
        it->second.imageCreate |= transition.get().entry().imageCreate;
        it->second.imageUsage  |= getAttachmentUsage(transition.get().entry().layout) | transition.get().entry().imageUsage;
      }
    }
    operationIndex++;
  }
  // Image may be reused by next transition when :
  // - AttachmentDefinition is the same for both images
  // - it is not a swapchain image
  // - it is not external memory object ( manually provided by user during graph construction )
  // - all previous operations using reused image are directly reachable from operations that generate new image
  std::vector<std::pair<uint32_t, uint32_t>> potentialAliases;
  for (auto& followingImage : imageInfo)
  {
    // image cannot alias :
    // - itself
    // - a swapchain
    // - an external image
    // - when attachment is different on a second image
    if( followingImage.second.isSwapchainImage || !followingImage.second.externalMemoryImageName.empty() )
      continue;
    auto allGeneratingTransitions = renderGraph.getTransitionIO(followingImage.first, opeAllOutputs);

    RenderOperationSet allPreviousOperations;
    for (const auto& generatingTransition : allGeneratingTransitions)
    {
      auto ops = getAllPreviousOperations(renderGraph, generatingTransition.get().operationName());
      std::copy(begin(ops), end(ops), std::inserter(allPreviousOperations, end(allPreviousOperations)));
    }

    for (const auto& precedingImage : imageInfo)
    {
      if (precedingImage.first == followingImage.first || precedingImage.second.isSwapchainImage || !precedingImage.second.externalMemoryImageName.empty() || precedingImage.second.attachmentDefinition != followingImage.second.attachmentDefinition )
        continue;
      // if all transitions are reachable from followingImage
      auto transitions = renderGraph.getTransitionIO(precedingImage.first, opeAllInputsOutputs);
      if (std::all_of(begin(transitions), end(transitions), [&allPreviousOperations](const ResourceTransition& tr) { return allPreviousOperations.find(tr.operation()) != end(allPreviousOperations);  }))
        potentialAliases.push_back({ precedingImage.first, followingImage.first });
    }
  }

  // we can find reuse schema having graph from resource pairs that can be reused. This algorithm should minimize the number of output elements
  std::map<uint32_t, uint32_t> imageAliases;
  while (!potentialAliases.empty())
  {
    auto longestPath = recursiveLongestPath(potentialAliases);

    std::vector<std::pair<uint32_t, uint32_t>> rp;
    std::copy_if(begin(potentialAliases), end(potentialAliases), std::back_inserter(rp),
      [&longestPath](const std::pair<uint32_t, uint32_t>& thisPair) { return std::find(begin(longestPath), end(longestPath), thisPair.first) == end(longestPath) && std::find(begin(longestPath), end(longestPath), thisPair.second) == end(longestPath); });
    potentialAliases = rp;

    if (longestPath.size() > 1)
    {
      auto target = longestPath.back();
      longestPath.pop_back();
      imageAliases.insert({ target, target });
      for (auto& source : longestPath)
      {
        imageAliases.insert({ source, target });
        imageInfo[target].imageUsage = imageInfo[target].imageUsage | imageInfo[source].imageUsage;
      }
    }
    else break; // there are no more aliases
  }
  // add null aliases for all transitions that have no alias ( including buffers - buffers cannot be aliased, but maybe one day, who knows... )
  for (auto& transition : renderGraph.transitions )
  {
    auto it = imageAliases.find(transition.tid());
    if (it == end(imageAliases))
      imageAliases.insert({ transition.tid(), transition.tid() });
  }

  // Attachment will be created only when it aliases itself. Other attachments only alias existing ones
  executable->memoryObjectAliases = imageAliases;
  std::copy_if(begin(imageInfo), end(imageInfo), std::inserter(executable->imageInfo, end(executable->imageInfo)), [&imageAliases](const std::pair<uint32_t, RenderGraphImageInfo>& p0)
    { return std::count_if(begin(imageAliases), end(imageAliases), [&p0](const std::pair<uint32_t, uint32_t>& p1)
      { return p0.first == p1.second && p1.first != p1.second; }) == 0; });
  executable->operationIndices = operationIndices;

  for (const auto& image : executable->imageInfo)
  {
    if (!image.second.externalMemoryImageName.empty()) // set only the internal images. External images should be set already
      continue;
    ImageTraits imageTraits(image.second.attachmentDefinition.format, image.second.attachmentDefinition.attachmentSize, image.second.imageUsage, false, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE);
    SwapChainImageBehaviour scib = (image.second.isSwapchainImage) ? swForEachImage : swOnce;
    VkImageAspectFlags aspectMask = getAspectMask(image.second.attachmentDefinition.attachmentType);
    auto imageIt = executable->memoryImages.insert({ image.first, std::make_shared<MemoryImage>(imageTraits, executable->frameBufferAllocator, aspectMask, pbPerSurface, scib, false, false) }).first;
  }

  // collect layouts and stuff
  std::vector<RenderGraphImageViewInfo> imageViewInfo;
  std::set<uint32_t> usedRteid;
  for (auto& op : partialOrdering)
  {
    auto operationIndex = operationIndices.at(op.get().name);
    auto opTransitions = renderGraph.getOperationIO(op.get().name, opeAllAttachmentInputs | opeImageInput);
    auto outTransitions = renderGraph.getOperationIO(op.get().name, opeAllAttachmentOutputs | opeImageOutput);
    std::copy(begin(outTransitions), end(outTransitions), std::back_inserter(opTransitions));

    for (auto& transition : opTransitions)
    {
      if (usedRteid.find(transition.get().rteid()) != end(usedRteid))
        continue;
      usedRteid.insert(transition.get().rteid());
      auto aliasIt = executable->memoryObjectAliases.find(transition.get().tid());
      if (aliasIt == end(executable->memoryObjectAliases))
        continue;
      uint32_t objectID = aliasIt->second;
      auto iiit = executable->imageInfo.find(objectID);
      if (iiit == end(executable->imageInfo))
        continue;
      RenderGraphImageViewInfo newInfo(transition.get().rteid(), transition.get().tid(), objectID, operationIndex, transition.get().entry().imageRange);

      newInfo.layouts.resize(operationIndex, transition.get().externalMemoryObjectName().empty() ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL);
      newInfo.layouts.push_back(transition.get().entry().layout);
      newInfo.operationParticipants.resize(operationIndex, 0);
      newInfo.operationParticipants.push_back(transition.get().tid());

      imageViewInfo.push_back(newInfo);
    }
    // fill up values for not used transitions
    for (auto& ivInfo : imageViewInfo)
    {
      VkImageLayout lastLayout = ivInfo.layouts.back();
      ivInfo.layouts.resize(operationIndex + 1, lastLayout);
      ivInfo.operationParticipants.resize(operationIndex + 1, 0);
    }
  }
  // we will add additional layout to image info - the same as te last one for all images except for swapchain image - this one gets VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  for (auto& ivInfo : imageViewInfo)
  {
    VkImageLayout lastLayout = executable->imageInfo[ivInfo.oid].isSwapchainImage ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : ivInfo.layouts.back();
    ivInfo.layouts.push_back(lastLayout);
    ivInfo.operationParticipants.push_back(0);
  }
  executable->imageViewInfo = imageViewInfo;

  // collect buffer view info
  std::vector<RenderGraphBufferViewInfo> bufferViewInfo;
  for (auto& op : partialOrdering)
  {
    auto operationIndex = operationIndices.at(op.get().name);
    auto opTransitions = renderGraph.getOperationIO(op.get().name, opeBufferInput);
    auto outTransitions = renderGraph.getOperationIO(op.get().name, opeBufferOutput);
    std::copy(begin(outTransitions), end(outTransitions), std::back_inserter(opTransitions));

    for (auto& transition : opTransitions)
    {
      if (usedRteid.find(transition.get().rteid()) != end(usedRteid))
        continue;
      usedRteid.insert(transition.get().rteid());
      auto aliasIt = executable->memoryObjectAliases.find(transition.get().tid());
      if (aliasIt == end(executable->memoryObjectAliases))
        continue;
      uint32_t objectID = aliasIt->second;
      RenderGraphBufferViewInfo newInfo(transition.get().rteid(), transition.get().tid(), objectID, operationIndex, transition.get().entry().bufferRange);
      bufferViewInfo.push_back(newInfo);
    }
  }
  executable->bufferViewInfo = bufferViewInfo;


  LOG_INFO << "ImageInfo:\n";
  LOG_INFO << "objectID, externalMemoryImageName, attachmentType, format, size type, x, y, z, arrayLayers, mipLevels, samples\n";
  for (const auto& image : executable->imageInfo)
  {
    LOG_INFO << image.first << ", " << (image.second.externalMemoryImageName.empty() ? std::string("<internal>") : image.second.externalMemoryImageName) << ", " << image.second.attachmentDefinition.attachmentType << ", " << image.second.attachmentDefinition.format << ", " << image.second.attachmentDefinition.attachmentSize.type << ", " << image.second.attachmentDefinition.attachmentSize.size.x << ", " << image.second.attachmentDefinition.attachmentSize.size.y << ", " << image.second.attachmentDefinition.attachmentSize.size.z;
    LOG_INFO << ", " << image.second.attachmentDefinition.attachmentSize.arrayLayers << ", " << image.second.attachmentDefinition.attachmentSize.mipLevels << ", " << image.second.attachmentDefinition.attachmentSize.samples << "\n";
  }

  LOG_INFO << "\nImageViewInfo :\n";
  LOG_INFO << "rteid, tid, oid, opidx, imageRange, _before, ";
  for (unsigned int i = 1; i <= operationIndices.size(); ++i)
  {
    auto oit = std::find_if(begin(operationIndices), end(operationIndices), [i](const std::pair<std::string, uint32_t>& p) { return p.second == i; });
    LOG_INFO << oit->first << ", ";
  }
  LOG_INFO << "_after, , _before, ";
  for (unsigned int i = 1; i <= operationIndices.size(); ++i)
  {
    auto oit = std::find_if(begin(operationIndices), end(operationIndices), [i](const std::pair<std::string, uint32_t>& p) { return p.second == i; });
    LOG_INFO << oit->first << ", ";
  }
  LOG_INFO << "_after \n";
  for (auto& ivInfo : executable->imageViewInfo)
  {
    LOG_INFO << ivInfo.rteid << "," << ivInfo.tid << "," << ivInfo.oid << "," << ivInfo.opidx <<",";
    LOG_INFO << ivInfo.imageRange.aspectMask <<"_("<< ivInfo.imageRange.baseMipLevel << "_" << ivInfo.imageRange.levelCount << ")x(" << ivInfo.imageRange.baseArrayLayer<< "_" << ivInfo.imageRange.layerCount << "),";
    for (auto x : ivInfo.layouts)
      LOG_INFO << x << ",";
    LOG_INFO << ",";
    for (auto x : ivInfo.operationParticipants)
      LOG_INFO << x << ",";
    LOG_INFO << "\n";
  }
  LOG_INFO << std::endl;

/*********/
  LOG_INFO << "\nBufferViewInfo :\n";
  LOG_INFO << "rteid, tid, oid, opidx, bufferRange\n";
  for (auto& bfInfo : executable->bufferViewInfo)
    LOG_INFO << bfInfo.rteid << "," << bfInfo.tid << "," << bfInfo.oid << "," << bfInfo.opidx << "," << bfInfo.bufferRange.offset << "_" << bfInfo.bufferRange.range << "\n";
  LOG_INFO << std::endl;

}

void DefaultRenderGraphCompiler::buildFrameBuffersAndRenderPasses(const RenderGraph& renderGraph, const std::vector<std::reference_wrapper<const RenderOperation>>& partialOrdering, std::shared_ptr<RenderGraphExecutable> executable)
{
  // build all image views - for render subpasses and compute subpasses, additionally create all render passes
  std::vector<std::shared_ptr<RenderPass>> renderPasses;
  for (int j = 0; j<executable->commands.size(); ++j)
  {
    for (uint32_t i = 0; i<executable->commands[j].size(); ++i)
    {
      auto renderCommand = executable->commands[j][i];
      for (const auto& entry : renderCommand->entries)
      {
        auto transition = renderGraph.getTransition(entry.second);
        // check if such transition exists
        auto aliasIt = executable->memoryObjectAliases.find(transition.get().tid());
        if (aliasIt == end(executable->memoryObjectAliases))
          continue;
        uint32_t objectID = aliasIt->second;
        
        const auto& opEntry = transition.get().entry();
        // for images and attachments - create imageViews
        if ((opEntry.entryType & (opeAllAttachments | opeAllImages)) != 0)
        {
          auto miit = executable->memoryImages.find(objectID);
          CHECK_LOG_THROW(miit == end(executable->memoryImages), "Not all memory images have been supplied");
          
          VkImageViewType imageViewType;
          if (transition.get().entry().imageViewType != VK_IMAGE_VIEW_TYPE_MAX_ENUM)
            imageViewType = transition.get().entry().imageViewType;
          else
            imageViewType = (opEntry.resourceDefinition.attachment.attachmentSize.arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
          renderCommand->imageViews.insert({ entry.second, std::make_shared<ImageView>(miit->second, opEntry.imageRange, imageViewType) });
        }
        else if (opEntry.bufferFormat != VK_FORMAT_UNDEFINED)// for buffers - add buffer views, but only if buffer format was defined, Only texel buffers use buffer views
        {
          auto miit = executable->memoryBuffers.find(objectID);
          CHECK_LOG_THROW(miit == end(executable->memoryBuffers), "Not all memory buffers have been supplied");

          renderCommand->bufferViews.insert({ entry.second, std::make_shared<BufferView>(miit->second, opEntry.bufferRange, opEntry.bufferFormat) });
        }
      }

      if (executable->commands[j][i]->commandType != RenderCommand::ctRenderSubPass)
        continue;
      auto subpass = std::dynamic_pointer_cast<RenderSubPass>(executable->commands[j][i]);
      if (subpass == nullptr || subpass->renderPass == nullptr)
        continue;
      auto rpit = std::find(begin(renderPasses), end(renderPasses), subpass->renderPass);
      if (rpit == end(renderPasses))
        renderPasses.push_back(subpass->renderPass);
    }
  }

  // build framebuffers for each render pass
  for (auto& renderPass : renderPasses)
  {
    ImageSize frameBufferSize = renderPass->subPasses[0].lock()->operation.attachmentSize;
    // ImageView, RenderGraphImageInfo
    std::vector<std::shared_ptr<ImageView>> frameBufferImageViews;
    std::vector<RenderGraphImageInfo>       frameBufferImageInfo;
    std::vector<AttachmentDescription>      frameBufferAttachments;

    for (auto sb : renderPass->subPasses)
    {
      auto subpass = sb.lock();
      for (const auto& entry : subpass->entries)
      {
        auto transition = renderGraph.getTransition(entry.second);
        // if it's not attachment entry - it should not be in a framebuffer
        if ((transition.get().entry().entryType  & opeAllAttachments) == 0)
          continue;
        // find imageInfo
        auto aliasIt = executable->memoryObjectAliases.find(transition.get().tid());
        if (aliasIt == end(executable->memoryObjectAliases))
          continue;
        uint32_t objectID = aliasIt->second;

        auto iiit = executable->imageInfo.find(objectID);
        CHECK_LOG_THROW(iiit == end(executable->imageInfo), "FrameBuffer::FrameBuffer() : not all memory images have been supplied : " << subpass->operation.name << "->" << entry.first);

        auto vit = subpass->imageViews.find(entry.second);
        CHECK_LOG_THROW(vit == end(subpass->imageViews), "FrameBuffer::FrameBuffer() : not all memory image views have been supplied : " << subpass->operation.name << "->"<< entry.first);
        frameBufferImageViews.push_back(vit->second);
        frameBufferImageInfo.push_back(iiit->second);
        frameBufferAttachments.push_back(AttachmentDescription(
          entry.second,
          iiit->second.attachmentDefinition.format,
          makeSamples(iiit->second.attachmentDefinition.attachmentSize),
          VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          VK_ATTACHMENT_STORE_OP_DONT_CARE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          VK_ATTACHMENT_STORE_OP_DONT_CARE,
          VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_UNDEFINED,
          0
        ));
      }
    }
    auto frameBuffer = std::make_shared<FrameBuffer>(frameBufferSize, renderPass, frameBufferImageViews, frameBufferImageInfo);
    executable->frameBuffers.push_back(frameBuffer);

    // build attachments, clear values and image layouts
    std::vector<char>                  initialLayoutsInitialized(frameBufferAttachments.size(), false);
    std::vector<VkClearValue>          clearValues(frameBufferAttachments.size(), makeColorClearValue(glm::vec4(0.0f)));
    std::vector<char>                  clearValuesInitialized(frameBufferAttachments.size(), false);

    // find all information about attachments and clear values
    for (auto& sb : renderPass->subPasses)
    {
      auto subPass            = sb.lock();
      bool lastSubpass        = ((subPass->subpassIndex + 1) == renderPass->subPasses.size());
      auto transitions        = renderGraph.getOperationIO(subPass->operation.name, opeAllAttachments | opeAllImages);
      auto resolveTransitions = renderGraph.getOperationIO(subPass->operation.name, opeAttachmentResolveOutput);

      std::vector<AttachmentReference> ia, oa, ra;
      AttachmentReference              dsa{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
      std::set<uint32_t>               attachmentUsed;

      // fill attachment information with render subpass specifics ( initial layout, final layout, load op, clear values )
      for (const auto& transition : transitions)
      {
        auto attit = std::find_if(begin(frameBufferAttachments), end(frameBufferAttachments), [&transition](const AttachmentDescription& ad) { return transition.get().rteid() == ad.imageDefinitionIndex; });
        if (attit == end(frameBufferAttachments))
          continue;
        uint32_t attIndex = std::distance(begin(frameBufferAttachments), attit);
        attachmentUsed.insert(attIndex);
        uint32_t objectID = executable->memoryObjectAliases.at(transition.get().tid());

        if (!initialLayoutsInitialized[attIndex])
        {
          frameBufferAttachments[attIndex].initialLayout = executable->getImageLayout(subPass->operation.name, objectID, transition.get().entry().imageRange, -1);
          initialLayoutsInitialized[attIndex] = true;
        }
        frameBufferAttachments[attIndex].finalLayout = executable->getImageLayout(subPass->operation.name, objectID, transition.get().entry().imageRange, 0);

        AttachmentType at                      = transition.get().entry().resourceDefinition.attachment.attachmentType;
        bool colorDepthAttachment              = (at == atColor) || (at == atDepth) || (at == atDepthStencil);
        bool stencilAttachment                 = (at == atDepthStencil) || (at == atStencil);
        bool stencilDepthAttachment            = (at == atDepth) || (at == atDepthStencil) || (at == atStencil);
        if (frameBufferAttachments[attIndex].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
          frameBufferAttachments[attIndex].loadOp = colorDepthAttachment ? static_cast<VkAttachmentLoadOp>(transition.get().entry().loadOp.loadType) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        if (frameBufferAttachments[attIndex].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
          frameBufferAttachments[attIndex].stencilLoadOp = stencilAttachment ? static_cast<VkAttachmentLoadOp>(transition.get().entry().loadOp.loadType) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (!clearValuesInitialized[attIndex])
        {
          if (stencilDepthAttachment)
            clearValues[attIndex] = makeDepthStencilClearValue(transition.get().entry().loadOp.clearColor.x, transition.get().entry().loadOp.clearColor.y);
          else
            clearValues[attIndex] = makeColorClearValue(transition.get().entry().loadOp.clearColor);
          clearValuesInitialized[attIndex] = true;
        }
        auto opParticipants            = executable->getOperationParticipants(objectID, transition.get().entry().imageRange);
        uint32_t operationIndex        = executable->operationIndices.at(subPass->operation.name);
        bool usedLater                 = std::find(begin(opParticipants) + operationIndex + 1, end(opParticipants), transition.get().tid()) != end(opParticipants);
        bool isSwapchain               = transition.get().entryName() == SWAPCHAIN_NAME;
        bool needSave                  = ( transition.get().entry().entryType & opeAllOutputs ) != 0 && transition.get().entry().storeAttachment;
        if (needSave || usedLater || isSwapchain)
        {
          if (colorDepthAttachment) frameBufferAttachments[attIndex].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
          if (stencilAttachment)    frameBufferAttachments[attIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        if(transition.get().entry().entryType == opeAttachmentInput)
          ia.push_back({ attIndex, transition.get().entry().layout });
        if (transition.get().entry().entryType == opeAttachmentOutput)
        {
          oa.push_back({ attIndex, transition.get().entry().layout });
          auto it = std::find_if(begin(resolveTransitions), end(resolveTransitions), [&transition](const ResourceTransition& rt) -> bool { return rt.entry().resolveSourceEntryName == transition.get().entryName(); });
          if (it != end(resolveTransitions))
          {
            auto attit = std::find_if(begin(frameBufferAttachments), end(frameBufferAttachments), [&it](const AttachmentDescription& ad) { return it->get().rteid() == ad.imageDefinitionIndex; });
            if (attit != end(frameBufferAttachments))
              ra.push_back({ static_cast<uint32_t>(std::distance(begin(frameBufferAttachments), attit)), it->get().entry().layout });
            else
              ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
          }
          else
            ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
        }
        if ( ( transition.get().entry().entryType & (opeAttachmentDepthInput | opeAttachmentDepthOutput) ) != 0)
          dsa = { attIndex, transition.get().entry().layout };
      }

      // check all available attachments if they need to be preserved
      uint32_t operationIndex = executable->operationIndices.at(subPass->operation.name);
      std::vector<uint32_t> pa;
      for (uint32_t attIndex=0; attIndex<frameBufferAttachments.size(); ++attIndex)
      {
        // if attachment was used in this subpass, then it does not need to be preserved
        if (attachmentUsed.find(attIndex) != end(attachmentUsed))
          continue;
        // find all transitions using this attachment
        auto transition     = renderGraph.getTransition(frameBufferAttachments[attIndex].imageDefinitionIndex);
        uint32_t objectID   = executable->memoryObjectAliases.at(transition.get().tid());
        auto opParticipants = executable->getOperationParticipants(objectID, transition.get().entry().imageRange);
        bool usedBefore     = std::find(begin(opParticipants), begin(opParticipants) + operationIndex, transition.get().tid()) != (begin(opParticipants) + operationIndex);
        bool usedLater      = std::find(begin(opParticipants) + operationIndex + 1, end(opParticipants), transition.get().tid()) != end(opParticipants);
        bool isSwapchain    = transition.get().entryName() == SWAPCHAIN_NAME;
        if(usedBefore && (usedLater || isSwapchain))
          pa.push_back(attIndex);
      }
      subPass->setSubpassDescription(SubpassDescription(VK_PIPELINE_BIND_POINT_GRAPHICS, ia, oa, ra, dsa, pa, 0, subPass->operation.multiViewMask));
    }
    renderPass->setRenderPassData(frameBuffer, frameBufferAttachments, clearValues);
  }

  LOG_INFO << "Render passes and frame buffers :";
  for (auto& renderPass : renderPasses)
  {
    LOG_INFO << "\nSubpasses, ";
    for (auto sb : renderPass->subPasses)
    {
      auto subpass = sb.lock();
      LOG_INFO << subpass->operation.name << ", ";
    }
    LOG_INFO << "\n";
    auto fbs = renderPass->frameBuffer->getFrameBufferSize();
    LOG_INFO << "FrameBuffer size, " << fbs.type << ", "<< fbs.size.x << ", " << fbs.size.y << ", " << fbs.size.z << ", " << fbs.arrayLayers << ", " << fbs.mipLevels << ", " << fbs.samples << "\n";
    LOG_INFO << "externalMemoryImageName, attachmentType, format, size type, x, y, z, arrayLayers, mipLevels, samples\n";
    for (const auto& image : renderPass->frameBuffer->getImageInfo())
    {
      LOG_INFO << (image.externalMemoryImageName.empty() ? std::string("<internal>") : image.externalMemoryImageName) << ", " << image.attachmentDefinition.attachmentType << ", " << image.attachmentDefinition.format << ", " << image.attachmentDefinition.attachmentSize.type << ", " << image.attachmentDefinition.attachmentSize.size.x << ", " << image.attachmentDefinition.attachmentSize.size.y << ", " << image.attachmentDefinition.attachmentSize.size.z << ", ";
      LOG_INFO << image.attachmentDefinition.attachmentSize.arrayLayers << ", " << image.attachmentDefinition.attachmentSize.mipLevels << ", " << image.attachmentDefinition.attachmentSize.samples << "\n";
    }
  }
  LOG_INFO << std::endl;
}

void DefaultRenderGraphCompiler::buildPipelineBarriers(const RenderGraph& renderGraph, std::shared_ptr<RenderGraphExecutable> executable)
{
  std::map<std::string, int> queueNumber;
  std::map<std::string, int> operationNumber;
  std::map<std::string, std::shared_ptr<RenderCommand>> commandMap;
  int queueIndex = 0;
  for (auto& commandSequence : executable->commands)
  {
    int operationIndex = 0;
    for (auto& command : commandSequence)
    {
      queueNumber[command->operation.name]      = queueIndex;
      operationNumber[command->operation.name]  = operationIndex;
      commandMap[command->operation.name]       = command;
      operationIndex++;
    }
    queueIndex++;
  }

  std::set<uint32_t> visitedTransitions;
  for (const auto& transition : renderGraph.transitions)
  {
    // we are dealing with all transitions with the same tid at once
    if (visitedTransitions.find(transition.tid()) != end(visitedTransitions))
      continue;
    visitedTransitions.insert(transition.tid());

    auto generatingTransitions     = renderGraph.getTransitionIO(transition.tid(), opeAllOutputs);
    if (generatingTransitions.empty())
      continue;

    auto generatingOperationNumber = operationNumber[generatingTransitions[0].get().operationName()];
    auto generatingQueueNumber     = queueNumber[generatingTransitions[0].get().operationName()];

    auto consumingTransitions = renderGraph.getTransitionIO(transition.tid(), opeAllInputs);
    if (consumingTransitions.empty())
      continue;
    // sort consuming transitions according to operation index, operations from current queue will be first in sorted vector
    // place transitions that are in the same queue first
    auto pos = std::partition(begin(consumingTransitions), end(consumingTransitions), [&queueNumber, &generatingQueueNumber](const ResourceTransition& lhs)
    {
      return queueNumber[lhs.operationName()] == generatingQueueNumber;
    });
    // sort transitions from the same queue according to order of operations
    std::sort(begin(consumingTransitions), pos, [&operationNumber](const ResourceTransition& lhs, const ResourceTransition& rhs)
    {
      return operationNumber[lhs.operationName()] < operationNumber[rhs.operationName()];
    });
    // sort transitions from other queues
    std::sort(pos, end(consumingTransitions), [&queueNumber, &operationNumber](const ResourceTransition& lhs, const ResourceTransition& rhs)
    {
      if (queueNumber[lhs.operationName()] == queueNumber[rhs.operationName()])
        return operationNumber[lhs.operationName()] < operationNumber[rhs.operationName()];
      return queueNumber[lhs.operationName()] < queueNumber[rhs.operationName()];
    });

    // for now we will create a barrier/subpass dependency for each transition. It should be later optimized ( some barriers are not necessary )
    for (auto& consumingTransition : consumingTransitions)
    {
      for (auto& generatingTransition : generatingTransitions)
      {
        // check if there is an intersection between ranges
        if ((generatingTransition.get().entry().entryType & opeAllBuffers) != 0)
        {
          // if this is buffer - check buffer range
          auto& rangeG = generatingTransition.get().entry().bufferRange;
          auto& rangeC = consumingTransition.get().entry().bufferRange;
          if (!rangeG.contains(rangeC) && !rangeC.contains(rangeG))
            continue;
        }
        else
        {
          // this is image - check image range
          auto& rangeG = generatingTransition.get().entry().imageRange;
          auto& rangeC = consumingTransition.get().entry().imageRange;
          if (!rangeG.contains(rangeC) && !rangeC.contains(rangeG))
            continue;
        }

        if (((generatingTransition.get().entry().entryType & opeAllAttachmentOutputs) != 0) && ((consumingTransition.get().entry().entryType & opeAllAttachmentInputs) != 0))
          createSubpassDependency(renderGraph, generatingTransition.get(), commandMap[generatingTransition.get().operationName()], consumingTransition, commandMap[consumingTransition.get().operationName()], queueNumber[generatingTransition.get().operationName()], queueNumber[consumingTransition.get().operationName()], executable);
        else
          createPipelineBarrier(renderGraph, generatingTransition.get(), commandMap[generatingTransition.get().operationName()], consumingTransition, commandMap[consumingTransition.get().operationName()], queueNumber[generatingTransition.get().operationName()], queueNumber[consumingTransition.get().operationName()], executable);
      }
    }
  }

  LOG_INFO << "Pipeline barriers :\n";
  for( auto& comSeq : executable->commands )
  { 
    for (auto& comm : comSeq)
    {
      if (comm->barriersBeforeOp.empty())
        continue;
      LOG_INFO << "Operation: " << comm->operation.name << "\n";
      for (auto& barg : comm->barriersBeforeOp)
      {
        LOG_INFO << "Barrier Group: " << barg.first.srcStageMask << ", " << barg.first.dstStageMask << ", " << barg.first.dependencyFlags << "\n";
        for (auto& bar : barg.second)
        {
          switch (bar.objectType)
          {
          case MemoryObject::moBuffer:
            LOG_INFO << "Buffer barrier: (" << bar.bufferRange.offset << "_" << bar.bufferRange.range << "), ";
            LOG_INFO << bar.srcAccessMask << ", " << bar.dstAccessMask << ", " << bar.srcQueueIndex << ", " << bar.dstQueueIndex << "\n";
            break;
          case MemoryObject::moImage:
            LOG_INFO << "Image barrier: " << bar.imageRange.aspectMask << "_(" << bar.imageRange.baseMipLevel << "_" << bar.imageRange.levelCount << ")x(" << bar.imageRange.baseArrayLayer << "_" << bar.imageRange.layerCount << "), ";
            LOG_INFO<< bar.srcAccessMask << ", " << bar.dstAccessMask << ", " << bar.srcQueueIndex << ", " << bar.dstQueueIndex << ", " << bar.oldLayout << ", " << bar.newLayout << "\n";
            break;
          }
        }
      }
      LOG_INFO << "\n";
    }
  }

  std::set<RenderPass*> visitedRenderPasses;
  LOG_INFO << "Subpass dependencies :\n";
  for (auto& comSeq : executable->commands)
  {
    for (auto& comm : comSeq)
    {
      auto subpass = comm->asRenderSubPass();
      if ( subpass == nullptr)
        continue;
      auto renderPass = subpass->renderPass.get();
      if (visitedRenderPasses.find(renderPass) != end(visitedRenderPasses))
        continue;
      visitedRenderPasses.insert(renderPass);
      LOG_INFO << "RenderPass : ";
      for (auto& sb : renderPass->subPasses)
        LOG_INFO << sb.lock()->operation.name << ", ";
      LOG_INFO << "\nsrcSubpass, dstSubpass, srcStageMask, dstStageMask, srcAccessMask, dstAccessMask, dependencyFlags\n";
      for (auto& dep : renderPass->dependencies)
        LOG_INFO << dep.srcSubpass << ", " << dep.dstSubpass << ", " << dep.srcStageMask << ", " << dep.dstStageMask << ", " << dep.srcAccessMask << ", " << dep.dstAccessMask << ", " << dep.dependencyFlags << "\n";
    }
  }
  LOG_INFO << std::endl;
}

void DefaultRenderGraphCompiler::createSubpassDependency(const RenderGraph& renderGraph, const ResourceTransition& generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, const ResourceTransition& consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex, std::shared_ptr<RenderGraphExecutable> executable)
{
  VkPipelineStageFlags srcStageMask = 0,  dstStageMask = 0;
  VkAccessFlags        srcAccessMask = 0, dstAccessMask = 0;
  getPipelineStageMasks(generatingTransition, consumingTransition, srcStageMask, dstStageMask);
  getAccessMasks(generatingTransition, consumingTransition, srcAccessMask, dstAccessMask);

  uint32_t             srcSubpassIndex = VK_SUBPASS_EXTERNAL, dstSubpassIndex = VK_SUBPASS_EXTERNAL;
  // try to add subpass dependency to latter command
  if (consumingCommand->commandType == RenderCommand::ctRenderSubPass)
  {
    auto consumingSubpass = std::dynamic_pointer_cast<RenderSubPass>(consumingCommand);
    // if both are render subpasses
    if (generatingCommand->commandType == RenderCommand::ctRenderSubPass)
    {
      auto generatingSubpass = std::dynamic_pointer_cast<RenderSubPass>(generatingCommand);
      // check if it's the same render pass
      if (generatingSubpass->renderPass.get() == consumingSubpass->renderPass.get())
        srcSubpassIndex = generatingSubpass->subpassIndex;
    }
    dstSubpassIndex = consumingSubpass->subpassIndex;

    auto dep = std::find_if(begin(consumingSubpass->renderPass->dependencies), end(consumingSubpass->renderPass->dependencies),
      [srcSubpassIndex, dstSubpassIndex](const SubpassDependencyDescription& sd) -> bool { return sd.srcSubpass == srcSubpassIndex && sd.dstSubpass == dstSubpassIndex; });
    if (dep == end(consumingSubpass->renderPass->dependencies))
      dep = consumingSubpass->renderPass->dependencies.insert(end(consumingSubpass->renderPass->dependencies), SubpassDependencyDescription(srcSubpassIndex, dstSubpassIndex, 0, 0, 0, 0, 0));
    dep->srcStageMask    |= srcStageMask;
    dep->dstStageMask    |= dstStageMask;
    dep->srcAccessMask   |= srcAccessMask;
    dep->dstAccessMask   |= dstAccessMask;
    dep->dependencyFlags |= VK_DEPENDENCY_BY_REGION_BIT;
  }
  else if (generatingCommand->commandType == RenderCommand::ctRenderSubPass) // consumingCommand is not a subpass - let's add it to generating command
  {
    auto generatingSubpass = std::dynamic_pointer_cast<RenderSubPass>(generatingCommand);
    srcSubpassIndex = generatingSubpass->subpassIndex;

    auto dep = std::find_if(begin(generatingSubpass->renderPass->dependencies), end(generatingSubpass->renderPass->dependencies),
      [srcSubpassIndex, dstSubpassIndex](const SubpassDependencyDescription& sd) -> bool { return sd.srcSubpass == srcSubpassIndex && sd.dstSubpass == dstSubpassIndex; });
    if (dep == end(generatingSubpass->renderPass->dependencies))
      dep = generatingSubpass->renderPass->dependencies.insert(end(generatingSubpass->renderPass->dependencies), SubpassDependencyDescription(srcSubpassIndex, dstSubpassIndex, 0, 0, 0, 0, 0));
    dep->srcStageMask    |= srcStageMask;
    dep->dstStageMask    |= dstStageMask;
    dep->srcAccessMask   |= srcAccessMask;
    dep->dstAccessMask   |= dstAccessMask;
    dep->dependencyFlags |= VK_DEPENDENCY_BY_REGION_BIT;
  }
  else // none of the commands are subpasses - add pipeline barrier instead
    createPipelineBarrier(renderGraph, generatingTransition, generatingCommand, consumingTransition, consumingCommand, generatingQueueIndex, consumingQueueIndex, executable);
}

void DefaultRenderGraphCompiler::createPipelineBarrier(const RenderGraph& renderGraph, const ResourceTransition& generatingTransition, std::shared_ptr<RenderCommand> generatingCommand, const ResourceTransition& consumingTransition, std::shared_ptr<RenderCommand> consumingCommand, uint32_t generatingQueueIndex, uint32_t consumingQueueIndex, std::shared_ptr<RenderGraphExecutable> executable)
{
  auto memoryObject = executable->getMemoryObject(generatingTransition.tid());
  if (memoryObject.get() == nullptr)
    return;

  VkPipelineStageFlags srcStageMask = 0,  dstStageMask = 0;
  VkAccessFlags        srcAccessMask = 0, dstAccessMask = 0;
  getPipelineStageMasks(generatingTransition, consumingTransition, srcStageMask, dstStageMask);
  getAccessMasks(generatingTransition, consumingTransition, srcAccessMask, dstAccessMask);

  VkDependencyFlags dependencyFlags = 0; // FIXME

  MemoryObjectBarrierGroup rbg(srcStageMask, dstStageMask, dependencyFlags);
  auto rbgit = consumingCommand->barriersBeforeOp.find(rbg);
  if (rbgit == end(consumingCommand->barriersBeforeOp))
    rbgit = consumingCommand->barriersBeforeOp.insert({ rbg, std::vector<MemoryObjectBarrier>() }).first;
  switch (generatingTransition.entry().resourceDefinition.metaType)
  {
  case rmtBuffer:
  {
    auto bufferRange = generatingTransition.entry().bufferRange;
    rbgit->second.push_back(MemoryObjectBarrier(srcAccessMask, dstAccessMask, generatingQueueIndex, consumingQueueIndex, memoryObject, bufferRange));
    break;
  }
  case rmtImage:
  {
    VkImageLayout oldLayout = generatingTransition.entry().layout;
    VkImageLayout newLayout = consumingTransition.entry().layout;
    auto imageRange         = generatingTransition.entry().imageRange;
    rbgit->second.push_back(MemoryObjectBarrier(srcAccessMask, dstAccessMask, generatingQueueIndex, consumingQueueIndex, memoryObject, oldLayout, newLayout, imageRange));
    break;
  }
  default:
    break;
  }
}

namespace pumex
{

VkImageAspectFlags getAspectMask(AttachmentType at)
{
  switch (at)
  {
  case atColor:
    return VK_IMAGE_ASPECT_COLOR_BIT;
  case atDepth:
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  case atDepthStencil:
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  case atStencil:
    return VK_IMAGE_ASPECT_STENCIL_BIT;
  default:
    return (VkImageAspectFlags)0;
  }
  return (VkImageAspectFlags)0;
}

VkImageUsageFlags  getAttachmentUsage(VkImageLayout il)
{
  switch (il)
  {
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         return VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
  case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:               return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  case VK_IMAGE_LAYOUT_UNDEFINED:
  case VK_IMAGE_LAYOUT_GENERAL:
  case VK_IMAGE_LAYOUT_PREINITIALIZED:
  default:                                               return (VkImageUsageFlags)0;
  }
  return (VkImageUsageFlags)0;
}

void getPipelineStageMasks(const ResourceTransition& generatingTransition, const ResourceTransition& consumingTransition, VkPipelineStageFlags& srcStageMask, VkPipelineStageFlags& dstStageMask)
{
  switch (generatingTransition.entry().entryType)
  {
  case opeAttachmentOutput:
  case opeAttachmentResolveOutput:
  case opeAttachmentDepthOutput:
  case opeImageOutput:
    switch (generatingTransition.entry().layout)
    {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      srcStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; break;
    case VK_IMAGE_LAYOUT_GENERAL:
      srcStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; break;
    default:
      srcStageMask = 0; break;
    }
    break;
  case opeBufferOutput:
    srcStageMask = generatingTransition.entry().pipelineStage;
    break;
  default:
    srcStageMask = 0; break;
  }
  switch (generatingTransition.operation().operationType)
  {
  case opGraphics:
    srcStageMask &= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT | VK_PIPELINE_STAGE_COMMAND_PROCESS_BIT_NVX | VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV | VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV | VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV;
    break;
  case opCompute:
    srcStageMask &= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT | VK_PIPELINE_STAGE_COMMAND_PROCESS_BIT_NVX;
    // missing for now :  | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV    
    break;
  // transfer not implemented yet
  //case opTransfer:
  //  srcStageMask &= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  //  break;
  default: break;
  }


  switch (consumingTransition.entry().entryType)
  {
  case opeAttachmentInput:
  case opeAttachmentDepthInput:
  case opeImageInput:
    switch (consumingTransition.entry().layout)
    {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; break;
    case VK_IMAGE_LAYOUT_GENERAL:
      dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; break;

    default:
      dstStageMask = 0; break;
    }
    break;
  case opeBufferInput:
    dstStageMask = consumingTransition.entry().pipelineStage;
    break;
  default:
    dstStageMask = 0; break;
  }
  switch (consumingTransition.operation().operationType)
  {
  case opGraphics:
    dstStageMask &= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT | VK_PIPELINE_STAGE_COMMAND_PROCESS_BIT_NVX | VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV | VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV | VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV;
    // missing for now : | VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT | VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT 
    break;
  case opCompute:
    dstStageMask &= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT | VK_PIPELINE_STAGE_COMMAND_PROCESS_BIT_NVX;
    // missing for now :  | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV    
    break;
  // transfer not implemented yet
  //case opTransfer:
  //  dstStageMask &= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  //  break;
  default: break;
  }

}

void getAccessMasks(const ResourceTransition& generatingTransition, const ResourceTransition& consumingTransition, VkAccessFlags& srcAccessMask, VkAccessFlags& dstAccessMask)
{
  switch (generatingTransition.entry().entryType)
  {
  case opeAttachmentOutput:
  case opeAttachmentResolveOutput:
  case opeAttachmentDepthOutput:
  case opeImageOutput:
    switch (generatingTransition.entry().layout)
    {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      srcAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT; break;
    case VK_IMAGE_LAYOUT_GENERAL:
      srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; break;
    default:
      srcAccessMask = 0; break;
    }
    break;
  case opeBufferOutput:
    srcAccessMask = generatingTransition.entry().accessFlags;
    break;
  default:
    srcAccessMask = 0; break;
  }

  switch (consumingTransition.entry().entryType)
  {
  case opeAttachmentInput:
  case opeAttachmentDepthInput:
  case opeImageInput:
    switch (consumingTransition.entry().layout)
    {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      dstAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT; break;
    case VK_IMAGE_LAYOUT_GENERAL:
      dstAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
    default:
      dstAccessMask = 0; break;
    }
    break;
  case opeBufferInput:
    dstAccessMask = consumingTransition.entry().accessFlags;
    break;
  default:
    dstAccessMask = 0; break;
  }
}

}
