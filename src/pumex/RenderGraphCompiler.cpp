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
  createPipelineBarriers(renderGraph, executable);

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
        [&visitedTransitions](const ResourceTransition& transition) { return visitedTransitions.find(transition.id()) == end(visitedTransitions); });
      if (notVisitedInputCount == 0)
      {
        // operation is performed - add it to partial ordering
        partialOrdering.push_back(operation);
        doneOperations.insert(operation);
        // mark output resources as existing
        auto outTransitions = renderGraph.getOperationIO(operation.get().name, opeAllOutputs);
        for (auto outTransition : outTransitions)
          visitedTransitions.insert(outTransition.get().id());
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
  std::map<uint32_t, float>    transitionCost;
  for (const auto& transition : renderGraph.transitions)
  {
    auto it = transitionCost.find(transition.id());
    if (it != end(transitionCost))
      continue;
    ImageSize     is = transition.operation().attachmentSize;
    OperationType ot = transition.operation().operationType;
    float totalCost = 0.0001f;
    auto others = renderGraph.getTransitionIO(transition.id(), opeAllInputsOutputs);
    for (auto other : others)
    {
      float cost = 0.0f;
      if (transition.operation().operationType != ot)
        cost += 0.1f;
      if (transition.operation().attachmentSize != is)
        cost += 0.1f;
      totalCost = std::max(cost, totalCost);
    }
    transitionCost.insert( { transition.id(), totalCost } );
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
      auto inTransitions = renderGraph.getTransitionIO(outTransition.get().id(), opeAllInputs);
      float transCost = transitionCost[outTransition.get().id()];
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
      float transCost = transitionCost[inputTransition.get().id()];
      auto outputTransitions = renderGraph.getTransitionIO(inputTransition.get().id(), opeAllOutputs);
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
          renderSubPass->entries.insert({ transition.get().entryName(), transition.get().id() });
        
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
          computePass->entries.insert({ transition.get().entryName(), transition.get().id() });

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
  std::map<uint32_t, RenderGraphImageInfo> imageInfo;
  for( auto& op : partialOrdering)
  {
    // operations are ordered. Create vector with all sorted image transitions ( input transitions before output transitions )
    auto opTransitions  = renderGraph.getOperationIO(op.get().name, opeAllAttachmentInputs | opeImageInput);
    auto outTransitions = renderGraph.getOperationIO(op.get().name, opeAllAttachmentOutputs | opeImageOutput);
    std::copy(begin(outTransitions), end(outTransitions), std::back_inserter(opTransitions));
    for (auto transition : opTransitions)
    {
      auto it = imageInfo.find(transition.get().id());
      if (it == end(imageInfo)) // if image is not in the imageInfo already - add it to vector, save its initial layout, guess layout before graph
        imageInfo.insert({ transition.get().id(), RenderGraphImageInfo(
          transition.get().entry().resourceDefinition.attachment,
          transition.get().externalMemoryObjectName(),
          getAttachmentUsage(transition.get().entry().layout),
          transition.get().externalMemoryObjectName().empty() ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
          transition.get().entry().layout,
          transition.get().entry().layout,
          transition.get().entryName() == SWAPCHAIN_NAME ) });
      else // accumulate image usage
      {
        it->second.imageUsage |= getAttachmentUsage(transition.get().entry().layout);
        it->second.finalLayout = transition.get().entry().layout;
      }
    }
  }

  // Image may be reused by next transition when :
  // - AttachmentDefinition is the same for both images
  // - it is not a swapchain image
  // - it is not external memory object ( manually provided by user during graph construction )
  // - all previous operations using reused image are directly reachable from operations that generate new image
  std::vector<std::pair<uint32_t, uint32_t>> potentialAliases;
  for (const auto& followingImage : imageInfo)
  {
    // cannot alias with a swapchain
    if( followingImage.second.isSwapchainImage )
      continue;
    // cannot alias external image
    if ( !followingImage.second.externalMemoryImageName.empty() )
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
      // cannot alias itself
      if (followingImage.first == precedingImage.first)
        continue;
      // cannot alias with a swapchain
      if (precedingImage.second.isSwapchainImage )
        continue;
      // cannot alias external image
      if (!precedingImage.second.externalMemoryImageName.empty())
        continue;
      // cannot alias when attachment is different
      if ( followingImage.second.attachmentDefinition != precedingImage.second.attachmentDefinition)
        continue;
      // if all transitions are reachable from attachmentNext
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
      bool init = true;
      for (auto& p : longestPath)
      {
        imageAliases.insert({ p, target });
        // update image usage on target
        imageInfo[target].imageUsage = imageInfo[target].imageUsage | imageInfo[p].imageUsage;
        // if it's first attachment aliasing target, then set initial layout of the target to first attachment's value
        if (init)
          imageInfo[target].initialLayout = imageInfo[p].initialLayout;
        init = false;
      }
    }
    else break; // there are no more aliases
  }
  // add null aliases for all transitions that have no alias ( including buffers - buffers cannot be aliased, but maybe one day, who knows... )
  for (auto& transition : renderGraph.transitions )
  {
    auto it = imageAliases.find(transition.id());
    if (it == end(imageAliases))
      imageAliases.insert({ transition.id(), transition.id() });
  }

  // Attachment will be created only when it aliases itself. Other attachments only alias existing ones
  executable->memoryObjectAliases = imageAliases;
  std::copy_if(begin(imageInfo), end(imageInfo), std::inserter(executable->imageInfo, end(executable->imageInfo)), [&imageAliases](const std::pair<uint32_t, RenderGraphImageInfo>& p0)
    { return std::count_if(begin(imageAliases), end(imageAliases), [&p0](const std::pair<uint32_t, uint32_t>& p1)
      { return p0.first == p1.second && p1.first != p1.second; }) == 0; });

  for (const auto& image : executable->imageInfo)
  {
    if (!image.second.externalMemoryImageName.empty()) // set only the internal images. External images should be set already
      continue;
    ImageTraits imageTraits(image.second.attachmentDefinition.format, image.second.attachmentDefinition.attachmentSize, image.second.imageUsage, false, image.second.layoutOutside, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE);
    SwapChainImageBehaviour scib = (image.second.isSwapchainImage) ? swForEachImage : swOnce;
    VkImageAspectFlags aspectMask = getAspectMask(image.second.attachmentDefinition.attachmentType);
    auto imageIt = executable->memoryImages.insert({ image.first, std::make_shared<MemoryImage>(imageTraits, executable->frameBufferAllocator, aspectMask, pbPerSurface, scib, false, false) }).first;

    ImageSubresourceRange imageRange(aspectMask, 0, image.second.attachmentDefinition.attachmentSize.samples, 0, image.second.attachmentDefinition.attachmentSize.arrayLayers);
    VkImageViewType imageViewType = (image.second.attachmentDefinition.attachmentSize.arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    executable->memoryImageViews.insert({ image.first, std::make_shared<ImageView>(imageIt->second, imageRange, imageViewType) });
  }
}

void DefaultRenderGraphCompiler::buildFrameBuffersAndRenderPasses(const RenderGraph& renderGraph, const std::vector<std::reference_wrapper<const RenderOperation>>& partialOrdering, std::shared_ptr<RenderGraphExecutable> executable)
{
  // find all render passes
  std::vector<std::shared_ptr<RenderPass>> renderPasses;
  for (int j = 0; j<executable->commands.size(); ++j)
  {
    for (uint32_t i = 0; i<executable->commands[j].size(); ++i)
    {
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

  // build framebuffers
  for (auto& renderPass : renderPasses)
  {
    ImageSize frameBufferSize = renderPass->subPasses[0].lock()->operation.attachmentSize;
    std::map<uint32_t, RenderGraphImageInfo>          frameBufferImageInfo;
    std::map<uint32_t, std::shared_ptr<MemoryImage>>  frameBufferMemoryImages;
    std::map<uint32_t, std::shared_ptr<ImageView>>    frameBufferImageViews;
    for (auto sb : renderPass->subPasses)
    {
      auto subpass = sb.lock();
      for (const auto& entry : subpass->entries)
      {
        // check if such transition exists
        auto aliasIt = executable->memoryObjectAliases.find(entry.second);
        if (aliasIt == end(executable->memoryObjectAliases))
          continue;
        // if it's not attachment entry - it should not be in a framebuffer
        if ((subpass->operation.entries[entry.first].entryType & opeAllAttachments) == 0)
          continue;
        // check if it was not added already
        uint32_t transitionID = aliasIt->second;
        if (frameBufferImageInfo.find(transitionID) != end(frameBufferImageInfo))
          continue;
        // find imageInfo
        auto iiit = executable->imageInfo.find(transitionID);
        if (iiit == end(executable->imageInfo))
          continue;
        frameBufferImageInfo.insert({ transitionID, iiit->second });

        auto miit = executable->memoryImages.find(transitionID);
        CHECK_LOG_THROW(miit == end(executable->memoryImages), "Not all memory images have been supplied");
        frameBufferMemoryImages.insert({ transitionID, miit->second });

        auto vit = executable->memoryImageViews.find(transitionID);
        CHECK_LOG_THROW(vit == end(executable->memoryImageViews), "FrameBuffer::FrameBuffer() : not all memory image views have been supplied");
        frameBufferImageViews.insert({ transitionID, vit->second });

      }
    }
    auto frameBuffer = std::make_shared<FrameBuffer>(frameBufferSize, renderPass, frameBufferImageInfo, frameBufferMemoryImages, frameBufferImageViews);
    executable->frameBuffers.push_back(frameBuffer);

    // build attachments, clear values and image layouts
    std::map<uint32_t,uint32_t>        attachmentOrder = frameBuffer->getAttachmentOrder();
    std::vector<AttachmentDescription> attachments     = frameBuffer->getAttachmentDescription();
    std::vector<VkClearValue>          clearValues(attachments.size(), makeColorClearValue(glm::vec4(0.0f)));
    std::vector<char>                  clearValuesInitialized(attachments.size(), false);

    // find all information about attachments and clear values
    for (auto& sb : renderPass->subPasses)
    {
      auto subPass   = sb.lock();
      // fill attachment information with render subpass specifics ( initial layout, final layout, load op, clear values )
      auto transitions = renderGraph.getOperationIO(subPass->operation.name, opeAllAttachments);
      for (auto& transition : transitions)
      {
        auto attachmentID = executable->memoryObjectAliases.at(transition.get().id());
        uint32_t attIndex = attachmentOrder.at(attachmentID);

        if (attachments[attIndex].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED)
          attachments[attIndex].initialLayout  = transition.get().entry().layout;
        attachments[attIndex].finalLayout      = transition.get().entry().layout;
        AttachmentType at                      = transition.get().entry().resourceDefinition.attachment.attachmentType;
        bool colorDepthAttachment              = (at == atColor) || (at == atDepth) || (at == atDepthStencil);
        bool stencilAttachment                 = (at == atDepthStencil) || (at == atStencil);
        bool stencilDepthAttachment            = (at == atDepth) || (at == atDepthStencil) || (at == atStencil);
        
        if (attachments[attIndex].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
          attachments[attIndex].loadOp = colorDepthAttachment ? static_cast<VkAttachmentLoadOp>(transition.get().entry().loadOp.loadType) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        if (attachments[attIndex].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
          attachments[attIndex].stencilLoadOp = stencilAttachment ? static_cast<VkAttachmentLoadOp>(transition.get().entry().loadOp.loadType) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (!clearValuesInitialized[attIndex])
        {
          if (stencilDepthAttachment)
            clearValues[attIndex] = makeDepthStencilClearValue(transition.get().entry().loadOp.clearColor.x, transition.get().entry().loadOp.clearColor.y);
          else
            clearValues[attIndex] = makeColorClearValue(transition.get().entry().loadOp.clearColor);
          clearValuesInitialized[attIndex] = true;
        }
      }
    }

    // build subpass definitions
    for (auto& sb : renderPass->subPasses)
    {
      auto subPass = sb.lock();
      VkPipelineBindPoint              bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      std::vector<AttachmentReference> ia;
      std::vector<AttachmentReference> oa;
      std::vector<AttachmentReference> ra;
      AttachmentReference              dsa;
      std::vector<uint32_t>            pa;

      auto inputAttachments   = renderGraph.getOperationIO(subPass->operation.name, opeAttachmentInput);
      auto outputAttachments  = renderGraph.getOperationIO(subPass->operation.name, opeAttachmentOutput);
      auto resolveAttachments = renderGraph.getOperationIO(subPass->operation.name, opeAttachmentResolveOutput);
      auto depthAttachments   = renderGraph.getOperationIO(subPass->operation.name, opeAttachmentDepthInput | opeAttachmentDepthOutput);

      for (auto& inputAttachment : inputAttachments)
        ia.push_back({ attachmentOrder.at(executable->memoryObjectAliases.at(inputAttachment.get().id())), inputAttachment.get().entry().layout });
      
      for (auto& outputAttachment : outputAttachments)
      {
        oa.push_back({ attachmentOrder.at(executable->memoryObjectAliases.at(outputAttachment.get().id())), outputAttachment.get().entry().layout });

        if (!resolveAttachments.empty())
        {
          auto it = std::find_if(begin(resolveAttachments), end(resolveAttachments), [&outputAttachment](const ResourceTransition& rt) -> bool { return rt.entry().resolveSourceEntryName == outputAttachment.get().entryName(); });
          if (it != end(resolveAttachments))
            ra.push_back({ attachmentOrder.at(executable->memoryObjectAliases.at(it->get().id())), it->get().entry().layout });
          else
            ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
        }
        else
          ra.push_back({ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
      }
      if (!depthAttachments.empty())
        dsa = { attachmentOrder.at(executable->memoryObjectAliases.at(depthAttachments[0].get().id())), depthAttachments[0].get().entry().layout };
      else
        dsa = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };

      SubpassDescription subPassDescription(VK_PIPELINE_BIND_POINT_GRAPHICS, ia, oa, ra, dsa, pa, 0, subPass->operation.multiViewMask);

      // OK, so we have a subpass definition - the one thing that's missing is information about preserved attachments ( in a subpass ) and attachments that must be saved ( in a render pass )
      auto inTransitions  = renderGraph.getOperationIO(subPass->operation.name, opeAllAttachmentInputs);
      auto outTransitions = renderGraph.getOperationIO(subPass->operation.name, opeAllAttachmentOutputs);
      auto thisOperation = std::find_if(begin(partialOrdering), end(partialOrdering), [&subPass](const RenderOperation& op) { return op.name == subPass->operation.name; });
      bool lastSubpass    = ((subPass->subpassIndex + 1) == renderPass->subPasses.size());

      // check which resources are used in a subpass
      std::set<uint32_t> attachmentsUsedInSubpass;
      for (auto& inTransition : inTransitions)
        attachmentsUsedInSubpass.insert(inTransition.get().id());
      for (auto& outTransition : outTransitions)
        attachmentsUsedInSubpass.insert(outTransition.get().id());
      // for each unused attachment : it must be preserved when
      // - it is swapchain image
      // - it was used before
      // - it is used later in a subpass or outside
      for (auto transition : renderGraph.transitions)
      {
        if (transition.entry().resourceDefinition.metaType != rmtImage)
          continue;
        // skip transition if it's not used in a framebuffer at all
        auto aoit = attachmentOrder.find(executable->memoryObjectAliases.at(transition.id()));
        if (aoit == end(attachmentOrder))
          continue;
        uint32_t attIndex = aoit->second;

        auto resOutTransitions = renderGraph.getTransitionIO(transition.id(), opeAllAttachmentOutputs);
        auto resInTransitions  = renderGraph.getTransitionIO(transition.id(), opeAllAttachmentInputs);

        bool usedLater = false;
        for (auto& inTransition : resInTransitions)
        {
          if (std::find_if(thisOperation + 1, end(partialOrdering), [&inTransition](const RenderOperation& op) { return op.name == inTransition.get().operationName(); })  != end(partialOrdering))
          {
            usedLater = true;
            break;
          }
        }
        bool usedBefore = false;
        for (auto& outTransition : resOutTransitions)
        {
          if (std::find_if(begin(partialOrdering), thisOperation, [&outTransition](const RenderOperation& op) { return op.name == outTransition.get().operationName(); }) != end(partialOrdering))
          {
            usedBefore = true;
            break;
          }
        }
        bool isSwapchain  = transition.entryName() == SWAPCHAIN_NAME;
        bool usedNow      = attachmentsUsedInSubpass.find(transition.id()) != end(attachmentsUsedInSubpass) || attachmentsUsedInSubpass.find(executable->memoryObjectAliases.at(transition.id())) != end(attachmentsUsedInSubpass);
        bool preserve     = usedBefore && !usedNow && (usedLater || isSwapchain);
        bool save         = lastSubpass && (usedLater || isSwapchain);

        if (preserve)
          subPassDescription.preserveAttachments.push_back(attIndex);
        if (save)
        {
          AttachmentType at = transition.entry().resourceDefinition.attachment.attachmentType;
          bool colorDepthAttachment = (at == atColor) || (at == atDepth) || (at == atDepthStencil);
          bool stencilAttachment    = (at == atDepthStencil) || (at == atStencil);

          if (colorDepthAttachment) attachments[attIndex].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
          if (stencilAttachment)    attachments[attIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
      }
      subPass->setSubpassDescription(subPassDescription);
    }
    renderPass->setRenderPassData(frameBuffer, attachments, clearValues);
  }
}

void DefaultRenderGraphCompiler::createPipelineBarriers(const RenderGraph& renderGraph, std::shared_ptr<RenderGraphExecutable> executable)
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
    if (visitedTransitions.find(transition.id()) != end(visitedTransitions))
      continue;
    visitedTransitions.insert(transition.id());
    auto generatingTransitions     = renderGraph.getTransitionIO(transition.id(), opeAllOutputs);
    if (generatingTransitions.empty())
      continue;

    auto generatingOperationNumber = operationNumber[generatingTransitions[0].get().operationName()];
    auto generatingQueueNumber     = queueNumber[generatingTransitions[0].get().operationName()];

    // sort consuming transitions according to operation index, operations from current queue will be first in sorted vector
    auto consumingTransitions = renderGraph.getTransitionIO(transition.id(), opeAllInputs);
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
      if( ((generatingTransitions[0].get().entry().entryType & opeAllAttachmentOutputs) != 0) && ((consumingTransition.get().entry().entryType & opeAllAttachmentInputs) != 0) )
        createSubpassDependency(renderGraph, generatingTransitions[0].get(), commandMap[generatingTransitions[0].get().operationName()], consumingTransition, commandMap[consumingTransition.get().operationName()], queueNumber[generatingTransitions[0].get().operationName()], queueNumber[consumingTransition.get().operationName()], executable);
      else
        createPipelineBarrier(renderGraph, generatingTransitions[0].get(), commandMap[generatingTransitions[0].get().operationName()], consumingTransition, commandMap[consumingTransition.get().operationName()], queueNumber[generatingTransitions[0].get().operationName()], queueNumber[consumingTransition.get().operationName()], executable);
    }
  }
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
  auto memoryObject = executable->getMemoryObject(generatingTransition.id());
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
