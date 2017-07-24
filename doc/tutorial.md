# Anatomy of a simple application - tutorial

In this document we will walk through creation of simple application based on *pumexviewer*.

The goal of the *pumexviewer* is to load a 3D model provided by command line and render it without textures. *Pumexviewer* creates only one surface and one window for rendering.



## main() function - creating necessary infrastructure classes

We start from parsing the command line. This piece of code will be skipped, and only description of the parameters available in command line will be presented :

```
  -h, --help                        display this help menu
  -d                                enable Vulkan debugging
  -f                                create fullscreen window
  -m[model]                         3D model filename
```
Parameters will be provided in a following variables : 

- -d -  enableDebugging ( bool )
- -f -  useFullScreen ( bool )
- -m - modelFileName ( bool )

First object that we must create is **pumex::Viewer** responsible for creation of Vulkan VkInstance and for collecting information about physical devices. All data required to create **pumex::Viewer** are contained in **pumex::ViewerTraits** structure :

```C++
    const std::vector<std::string> requestDebugLayers = { { "VK_LAYER_LUNARG_standard_validation" } };
    pumex::ViewerTraits viewerTraits{ "pumex viewer", enableDebugging, requestDebugLayers, 60 };
    viewerTraits.debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT;

    std::shared_ptr<pumex::Viewer> viewer = std::make_shared<pumex::Viewer>(viewerTraits);
```

Now we must create a logical device ( VkDevice ). We must define what queues ( VkQueue ) are required for our application and what extensions our application will use :

```C++
    std::vector<pumex::QueueTraits> requestQueues = { pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT , 0, { 0.75f } } };
    std::vector<const char*> requestDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::shared_ptr<pumex::Device> device = viewer->addDevice(0, requestQueues, requestDeviceExtensions);

```

Our new device will create only one queue used for graphics purposes ( VK_QUEUE_GRAPHICS_BIT ). Its priority will be set to 0.75f. Our device must be created with swapchain extension ( VK_KHR_SWAPCHAIN_EXTENSION_NAME ), because we are going to render to a surface that will use images provided by swapchain.

Our next goal after device creation is creation of a window. Creation of windows and surfaces is done separately in Pumex, so that in a future we will be able to connext Pumex created surface to external windows provided by the user ( QT windows for example ) :

```C++
    std::string windowName = "Pumex viewer : ";
    windowName += modelFileName;
    pumex::WindowTraits windowTraits{ 0, 100, 100, 640, 480, useFullScreen ? pumex::WindowTraits::FULLSCREEN : pumex::WindowTraits::WINDOW, windowName };
    std::shared_ptr<pumex::Window> window = pumex::Window::createWindow(windowTraits);
```

We can see that our window will be created on screen number 0, with provided position, size and name. If user requested fullscreen window in command line then the window will be fullscreen ( without OS specific window decorations ).

Next step is a surface creation ( VkSurface ). To create a surface - **pumex::SurfaceTraits** structure must be provided. pumex::SurfaceTraits must have defined following elements : 

- presentation queue ( one from queues defined earlier for a device )
- framebuffer images used by all framebuffers and all render passes during rendering
- main render pass ( the one that renders to swapchain image )

We start by declaring frame buffer images and allocating memory for them :

```C++
    std::vector<pumex::FrameBufferImageDefinition> frameBufferDefinitions =
    {
      { pumex::FrameBufferImageDefinition::SwapChain, VK_FORMAT_B8G8R8A8_UNORM,    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,         VK_IMAGE_ASPECT_COLOR_BIT,                               VK_SAMPLE_COUNT_1_BIT },
      { pumex::FrameBufferImageDefinition::Depth,     VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_SAMPLE_COUNT_1_BIT }
    };

    std::shared_ptr<pumex::DeviceMemoryAllocator> frameBufferAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 16 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
    std::shared_ptr<pumex::FrameBufferImages> frameBufferImages = std::make_shared<pumex::FrameBufferImages>(frameBufferDefinitions, frameBufferAllocator);
```

After that we prepare a main render pass. Our render pass will be simple - just one subpass using both images defined - color and depth ( color image will come from swapchain, depth image will be provided by memory allocator ). No subpass dependencies:

```C++
    std::vector<pumex::AttachmentDefinition> renderPassAttachments =
    {
      { 0, VK_FORMAT_B8G8R8A8_UNORM,    VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0 },
      { 1, VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED,       0 }
    };

    std::vector<pumex::SubpassDefinition> renderPassSubpasses = 
    {
      {
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        {},
        { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
        {},
        { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
        {},
        0
      }
    };
    std::vector<pumex::SubpassDependencyDefinition> renderPassDependencies;

    std::shared_ptr<pumex::RenderPass> renderPass = std::make_shared<pumex::RenderPass>(renderPassAttachments, renderPassSubpasses, renderPassDependencies);
```

Now is the moment to create a **pumex::SurfaceTraits** structure and a surface itself

```C++
    pumex::SurfaceTraits surfaceTraits{ 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, 1, VK_PRESENT_MODE_MAILBOX_KHR, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    surfaceTraits.definePresentationQueue(pumex::QueueTraits{ VK_QUEUE_GRAPHICS_BIT, 0,{ 0.75f } });
    surfaceTraits.setDefaultRenderPass(renderPass);
    surfaceTraits.setFrameBufferImages(frameBufferImages);
    std::shared_ptr<pumex::Surface> surface = viewer->addSurface(window, device, surfaceTraits);
```

Basic **pumex::Surface** parameters required during surface creation are :

- imageCount - quantity of images athat swapchain will provide
- color space of these images
- don't remeber :)
- swapchainPresentMode - a method of presenting image by swapchain ( fifo, mailbox, etc. )
- image transformation
- how the alpha will be treated

Presentation queue for a surface must be taken from the queues defined earlier for a device.

pumex::Surface::addSurface() method associates new surface, device and a window into one mechanism allowing us to render in Pumex library.

At that stage we declare a helper class called **ViewerApplicationData**. This class will contain all basic and compound objects used by Pumex to render an image ( buffers, images, descriptor sets, pipelines, pumex::AssetBuffer, etc. ). It will also contain all data required to perform update phase ( information about the state of rendered objects, camera position etc. ).

```C++
    std::shared_ptr<ViewerApplicationData> applicationData = std::make_shared<ViewerApplicationData>(viewer, modelFileName);
    applicationData->defaultRenderPass = renderPass;
    applicationData->setup();
```

setup() method creates all above mentioned objects in **ViewerApplicationData**. But after creation these objects exist only on CPU side. There are two kinds of such objects : these that need one time setup on GPU side and these that must be updated before each draw. Objects that need one time setup on GPU side be called after surface creation :

```C++
applicationData->surfaceSetup(surface);
```

**ViewerApplicationData** class and its all elements ( methods and objects ) will be described in detail in later part of this article. Below its methods will be used to initiate TBB flow graphs.

**pumex::Viewer** contains two TBB flow graphs. These graphs represent task flow for two phases of the application : update graph and render graph. In our example update graph is really simple - it will have to sequentailly run ViewerApplicationData::processInput() that processes messages from mouse and keyboard, and then ViewerApplicationData::update() will run to set a new position for a camera and if models have any animations - animate the model :

```C++
    tbb::flow::continue_node< tbb::flow::continue_msg > update(viewer->updateGraph, [=](tbb::flow::continue_msg)
    {
      applicationData->processInput(surface);
      applicationData->update(pumex::inSeconds(viewer->getUpdateTime() - viewer->getApplicationStartTime()), pumex::inSeconds(viewer->getUpdateDuration()));
    });

    tbb::flow::make_edge(viewer->startUpdateGraph, update);
    tbb::flow::make_edge(update, viewer->endUpdateGraph);
```

**pumex::Viewer** posseses two tbb::flow::continue_node objects that indicate start and end of the update graph ( pumex::Viewer::startUpdateGraph and  pumex::Viewer::endUpdateGraph ).

We declare the render graph similarly. In our case it also will be very sequential graph :

```C++
    tbb::flow::continue_node< tbb::flow::continue_msg > prepareBuffers(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->prepareModelForRendering(); });
    tbb::flow::continue_node< tbb::flow::continue_msg > startSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->prepareCameraForRendering(surface); surface->beginFrame(); });
    tbb::flow::continue_node< tbb::flow::continue_msg > drawSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { applicationData->draw(surface); });
    tbb::flow::continue_node< tbb::flow::continue_msg > endSurfaceFrame(viewer->renderGraph, [=](tbb::flow::continue_msg) { surface->endFrame(); });

    tbb::flow::make_edge(viewer->startRenderGraph, prepareBuffers);
    tbb::flow::make_edge(prepareBuffers, startSurfaceFrame);
    tbb::flow::make_edge(startSurfaceFrame, drawSurfaceFrame);
    tbb::flow::make_edge(drawSurfaceFrame, endSurfaceFrame);
    tbb::flow::make_edge(endSurfaceFrame, viewer->endRenderGraph);

```

Render part consist of :

- sending data about model to buffers
- preparing camera data for specific surface ( only one surface in our example ) and sendin it to uniform buffer
- starting new frame with pumex::Surface::beginFrame() - obtaining new image from swapchain
- running ViewerApplication::draw() method that fills command buffer ( if necessary ) and sends it to presentation queue
- finishing new frame with pumex::Surface::endFrame() - sending ready image to presentation

All object to run an application are declared - we may start main render loop :

```C++
    viewer->run();
```

When main render loop is finished it is good idea to remove all surfaces and devices using method :

```C++
viewer->cleanup();
```



## Basic and compound Pumex objects

Currently Pumes does not posses its won Application object that is present in so many frameworks. It's because - firstly - too much formalism makes experimenting harder. And secondly - not all Vulkan specification has been implemented in Vulkan and therefore - future changes may completely change the idea of single Application object. On the other side - in each Pumex example such object exists and there is a need to have some central object, that stores all data and methods required to implement different rendering algorithms. That's why in this example there exists class named **ViewerApplicationData**, that represents  such need.

Data stored by this class may be categorized in a following way :

- variables used by update phase ( UpdateData object for example ) - for example input from mouse and keyboard is stored in these variables
- update phase stores results of its work in one of three RenderData objects ( in our case there is mainly data associated with camera )
- basic Pumex objects used during rendering ( pipelines, descriptor sets, buffers, etc.)
- compound Pumex objects that implement some complex functionality ( asset rendering with lods, material sets, text rendering )
- command buffers for each surface ( only one surface in our case )
- pumex::Viewer pointer
- variables storing time statistics
- additional data ( such as (3D model name)
- slave view matrices for each surface ( no such matrices required for our example )



Methods in ViewerApplicationData :

- **constructor** - provides 3D model name and pointer to pumex::Viewer
- **setup()** - creates all Pumex objects on CPU side
- **surfaceSetup()** - realizes above mentioned objects on a provided surface
- **processInput()** - receives and processes mouse and keyboard input
- **update()** - processes data and places it in one of RenderData structures
- **prepareModelForRendering()** - receives data from newest RenderData structure and fills Vulkan buffers with it ( calculate new animation pose if 3D model has any )
- **prepareCameraForRendering()** - knowing surface it sets camera parameters in uniform buffer ( view matrix, projection matrix, observer position )
- **draw()** - validates all Vulkan objects ( sends it to GPU ), fills command buffer when necessary and sends it to presentation queue.



### ViewerApplicationData::setup() in detail

In a main() function we allocated some memory for frame buffer images. Now we need to allocate **memory for buffers**. These buffers will be updated on every frame, so we will allocate this memory on the host. 1 MB of memory should be sufficient for these buffers :

```C++
    buffersAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT , 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
```

Also we must allocate **memory for vertex and index buffers** - these buffers will not be updated every frame and they should be allocated as close to GPU as possible, to speed up the rendering. The size of the memory allocated for vertices will be 64 MB :

```C++
    verticesAllocator = std::make_shared<pumex::DeviceMemoryAllocator>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 64 * 1024 * 1024, pumex::DeviceMemoryAllocator::FIRST_FIT);
```

Next thing to define is a **vertex semantic** that will be used during 3D asset loading, asset storing in pumex::AssetBuffer, pipeline creation and shader execution. Vertex semantic defines what vertices will look like :

```C++
    std::vector<pumex::VertexSemantic> requiredSemantic           = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 2 }, { pumex::VertexSemantic::BoneWeight, 4 }, { pumex::VertexSemantic::BoneIndex, 4 } };
```

We can see that the single vertex will contain position (3 floats), normal vector (3floats), texture coordinate (2 floats), bone weights ( 4 floats ), and bone indices (4 floats).

Now we create **pumex::AssetBuffer**. Asset buffer uses few storage buffers internally and also stores vertices and indices on GPU. So it must use both earlier created allocators :

```C++
    std::vector<pumex::AssetBufferVertexSemantics> assetSemantics = { { 1, requiredSemantic } };
    assetBuffer    = std::make_shared<pumex::AssetBuffer>(assetSemantics, buffersAllocator, verticesAllocator);
```

Our next task is to load provided 3D asset and to calculate its bounding box. Loaded asset's vertices will be converted to required vertex semantic, because we use only one vertex semantic in this example. Alternatively we could omit providing vertex semantic to a loader and convert asset later by ourselves - this comes handy, when we want to use many graphics pipelines to render an asset ( because asset may contain different rendering aspects : translucent parts, lights, other special effects ).

```C++
    pumex::AssetLoaderAssimp loader;
    std::shared_ptr<pumex::Asset> asset(loader.load(modelName, false, requiredSemantic));
    
    pumex::BoundingBox bbox;
    if (asset->animations.size() > 0)
      bbox = pumex::calculateBoundingBox(asset->skeleton, asset->animations[0], true);
    else
      bbox = pumex::calculateBoundingBox(*asset,1);
```

After asset loading - we need to register an asset as new object type in asset buffer and add a LOD to it. This LOD will be visible between 0 and 10000 meters :

```C++
    modelTypeID = assetBuffer->registerType("object", pumex::AssetTypeDefinition(bbox));
    assetBuffer->registerObjectLOD(modelTypeID, asset, pumex::AssetLodDefinition(0.0f, 10000.0f));
```

When the asset is loaded we must prepare basic Pumex objects from ViewerApplicationData class to render it. First we define agroup of helper objects : a descriptor set layout, a pool of descriptor sets, a pipeline layout containing only one descriptor set and a cache for pipelines :

```C++
    std::vector<pumex::DescriptorSetLayoutBinding> layoutBindings =
    {
      { 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
      { 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
    };
    descriptorSetLayout = std::make_shared<pumex::DescriptorSetLayout>(layoutBindings);

    descriptorPool = std::make_shared<pumex::DescriptorPool>(2, layoutBindings);

    pipelineLayout = std::make_shared<pumex::PipelineLayout>();
    pipelineLayout->descriptorSetLayouts.push_back(descriptorSetLayout);

    pipelineCache = std::make_shared<pumex::PipelineCache>();
```

And now we define a graphics pipeline. **pumex::GraphicsPipeline** objects store all variables required to define such pipeline. All these variables have default values defined, so during pipeline creation we only change these variables whose values are different from default values :

```C++
    pipeline = std::make_shared<pumex::GraphicsPipeline>(pipelineCache, pipelineLayout, defaultRenderPass, 0);
    pipeline->shaderStages =
    {
      { VK_SHADER_STAGE_VERTEX_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/viewer_basic.vert.spv")), "main" },
      { VK_SHADER_STAGE_FRAGMENT_BIT, std::make_shared<pumex::ShaderModule>(viewer->getFullFilePath("shaders/viewer_basic.frag.spv")), "main" }
    };
    pipeline->vertexInput =
    {
      { 0, VK_VERTEX_INPUT_RATE_VERTEX, requiredSemantic }
    };
    pipeline->blendAttachments =
    {
      { VK_FALSE, 0xF }
    };
    pipeline->dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
```

Important thing in pipeline creation is shader loading and setting definition of vertex semantic.

Before we create a descriptor set - we must create uniform buffers that will work as a data source for this descriptor set. There are two such buffers - first one is responsible for camera parameters. There is a helper class that is able to provide such parameters : pumex::Camera. 

```C++
    cameraUbo = std::make_shared<pumex::UniformBufferPerSurface<pumex::Camera>>(buffersAllocator);
```

Camera may have different parameters on each surface and that's why we use a **pumex::UniformBufferPerSurface** class.

Second uniform buffer provides rendered object state - its position and bone matrices in our example ( all that is stored in PositionData object ). We need to calculate reset position for bones and put it into **pumex::UniformBuffer** class - we use pumex::UniformBuffer, because it may have the same data on each surface ( and device ) :

```C++
    std::vector<glm::mat4> globalTransforms = pumex::calculateResetPosition(*asset);
    PositionData modelData;
    std::copy(globalTransforms.begin(), globalTransforms.end(), std::begin(modelData.bones));

    positionUbo = std::make_shared<pumex::UniformBuffer<PositionData>>(modelData, buffersAllocator);
```

Buffers are created - now comes the time for descriptor set. It's important to connect buffers as sources to proper bindings earlier defined in descriptor set layout :

```C++
    descriptorSet = std::make_shared<pumex::DescriptorSet>(descriptorSetLayout, descriptorPool);
    descriptorSet->setSource(0, cameraUbo);
    descriptorSet->setSource(1, positionUbo);
```

Last thing in ViewerApplicationData::setup() function is to reset update data ( camera position, keyboard state etc. ) :

```C++
    updateData.cameraPosition              = glm::vec3(0.0f, 0.0f, 0.0f);
    updateData.cameraGeographicCoordinates = glm::vec2(0.0f, 0.0f);
    updateData.cameraDistance              = 1.0f;
    updateData.leftMouseKeyPressed         = false;
    updateData.rightMouseKeyPressed        = false;
    updateData.moveForward                 = false;
    updateData.moveBackward                = false;
    updateData.moveLeft                    = false;
    updateData.moveRight                   = false;
```



### ViewerApplicationData::surfaceSetup() in detail

Objects created in ViewerApplicationData::setup() exist only on CPU side ( there are no Vulkan objects until now ). After surface has been created we want to create these objects on GPU.

First - we create a single command buffer that will render our object : 

```C++
    pumex::Device*      devicePtr      = surface->device.lock().get();
    pumex::CommandPool* commandPoolPtr = surface->commandPool.get();

    myCmdBuffer[surface.get()] = std::make_shared<pumex::CommandBuffer>(VK_COMMAND_BUFFER_LEVEL_PRIMARY, devicePtr, commandPoolPtr, surface->getImageCount());
```

and then we validate all earlier created objects on GPU :

```C++
    cameraUbo->validate(surface.get());
    positionUbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);

    assetBuffer->validate(devicePtr, commandPoolPtr, surface->presentationQueue);

    descriptorSetLayout->validate(devicePtr);
    descriptorPool->validate(devicePtr);
    pipelineLayout->validate(devicePtr);
    pipelineCache->validate(devicePtr);
    pipeline->validate(devicePtr);
    descriptorSet->validate(surface.get());
```

Now all data is ready to render.



### Methods responsible for drawing in every frame

Drawing methods are called every frame ( and for every surface, but we only use one surface in our example ). They consist of the following phases :

- data produced by update flow graph is extrapolated to obtain current data. This data is global and the same for each surface
- this data is sent to buffers and buffers are validated ( updated for specific surface )
- command buffer is rerecorded ( if necessary ) and sent to presentation queue

Data extrapolation in *pumexviewer* example actually is very limited - only new animation position is calculated ( in ViewerApplicationData::prepareModelForRendering() ) and new camera parameters are obtained ( in ViewerApplicationData::prepareCameraForRendering() ). Both of these methods use data created earlier by update phase ( ViewerApplicationData::processInput() and ViewerApplicationData::update() ).

Data is sent to buffers in ViewerApplicationData::draw() method :

```C++
    cameraUbo->validate(surfacePtr);
    positionUbo->validate(devicePtr, commandPoolPtr, surface->presentationQueue);
```

In the same method command buffer is sent to presentation queue. If objects associated with command buffer have changed ( and CB is dirty ) then command buffer is recorded again :

```C++
    auto& currentCmdBuffer = myCmdBuffer[surfacePtr];
    currentCmdBuffer->setActiveIndex(activeIndex);
    if (currentCmdBuffer->isDirty(activeIndex))
    {
      currentCmdBuffer->cmdBegin();

      std::vector<VkClearValue> clearValues = { pumex::makeColorClearValue(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), pumex::makeDepthStencilClearValue(1.0f, 0) };
      currentCmdBuffer->cmdBeginRenderPass(surfacePtr, defaultRenderPass.get(), surface->frameBuffer.get(), surface->getImageIndex(), pumex::makeVkRect2D(0, 0, renderWidth, renderHeight), clearValues);
      currentCmdBuffer->cmdSetViewport(0, { pumex::makeViewport(0, 0, renderWidth, renderHeight, 0.0f, 1.0f) });
      currentCmdBuffer->cmdSetScissor(0, { pumex::makeVkRect2D(0, 0, renderWidth, renderHeight) });

      currentCmdBuffer->cmdBindPipeline(pipeline.get());
      currentCmdBuffer->cmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePtr, pipelineLayout.get(), 0, descriptorSet.get());
      assetBuffer->cmdBindVertexIndexBuffer(devicePtr, currentCmdBuffer.get(), 1, 0);
      assetBuffer->cmdDrawObject(devicePtr, currentCmdBuffer.get(), 1, modelTypeID, 0, 50.0f);

      currentCmdBuffer->cmdEndRenderPass();
      currentCmdBuffer->cmdEnd();
    }
```

As we see the rendering is very simple. It consists of :

- starting render pass
- setting viewport and scissor ( both were defined as dynamic in graphics pipeline, so it must be done here )
- binding pipeline
- binding descriptor set that works with above mentioned pipeline
- binding vertex and index vertices through asset buffer
- drawing object with proper ID
- ending render pass

And at the end we are sending prepared command buffer to a presentation queue :

```C++
    currentCmdBuffer->queueSubmit(surface->presentationQueue, { surface->imageAvailableSemaphore }, { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }, { surface->renderCompleteSemaphore }, VK_NULL_HANDLE);
```

After performing pumex::Surface::endFrame() application is ready to draw next frame according to render flow graph.

### Vertex and fragment shader

Here we present shaders that work with above defined objects. Firstly our vertex shader must declare version and used extensions :

```GLSL
    #version 450
    #extension GL_ARB_separate_shader_objects : enable
    #extension GL_ARB_shading_language_420pack : enable
```

When model is loaded and registered in an pumex::AssetBuffer its vertices are stored using earlier defined vertex semantic. Now graphics pipeline defines the same vertex semantic as expected one, so that shaders may be used. Vertex shader must define following inputs:

```GLSL
    layout (location = 0) in vec3 inPos;
    layout (location = 1) in vec3 inNormal;
    layout (location = 2) in vec2 inUV;
    layout (location = 3) in vec4 inBoneWeight;
    layout (location = 4) in vec4 inBoneIndex;
```

Graphics pipeline uses two uniform buffers ( camera state and object state ) that must be declared here as well :

```GLSL
    layout (binding = 0) uniform CameraUbo
    {
      mat4 viewMatrix;
      mat4 viewMatrixInverse;
      mat4 projectionMatrix;
      vec4 observerPosition;
    } camera;

    #define MAX_BONES 511
    layout (binding = 1) uniform PositionSbo
    {
      mat4  position;
      mat4  bones[MAX_BONES];
    } object;
```

You may compare above defined GLSL structures with their C++ counterparts. 

pumex::Camera class corresponding to CameraUbo uniform buffer looks like this :

```C++
class Camera
{
  // methods ommited for clarity
  glm::mat4 viewMatrix;
  glm::mat4 viewMatrixInverse;
  glm::mat4 projectionMatrix;
  glm::vec4 observerPosition;
  float     timeSinceStart;  // forgot to declare it in GLSL structure :(
};

```

And PositionData struct corresponding to PositionSbo uniform buffer is declared like this :

```C++
const uint32_t MAX_BONES = 511;

struct PositionData
{
  // methods ommited for clarity
  glm::mat4 position;
  glm::mat4 bones[MAX_BONES];
};
```



Vertex shader now declares additional constants :

```GLSL
    const vec3 lightDirection = vec3(0,0,1);
```

...and now outputs of the vertex shader are declared :

```GLSL
    out gl_PerVertex 
    {
      vec4 gl_Position;   
    };

    layout (location = 0) out vec3 outNormal;
    layout (location = 1) out vec3 outColor;
    layout (location = 2) out vec2 outUV;
    layout (location = 3) out vec3 outViewVec;
    layout (location = 4) out vec3 outLightVec;
```

Vertex shader main() function implements skeletal animation with directional lighting always parallel to camera viewing axis :

```GLSL
    void main() 
    {
    	mat4 boneTransform = object.bones[int(inBoneIndex[0])] * inBoneWeight[0];
    	boneTransform     += object.bones[int(inBoneIndex[1])] * inBoneWeight[1];
    	boneTransform     += object.bones[int(inBoneIndex[2])] * inBoneWeight[2];
    	boneTransform     += object.bones[int(inBoneIndex[3])] * inBoneWeight[3];	
    	mat4 vertexTranslation = object.position * boneTransform;

    	gl_Position = camera.projectionMatrix * camera.viewMatrix * vertexTranslation * vec4(inPos.xyz, 1.0);
    	outNormal   = mat3(inverse(transpose(vertexTranslation))) * inNormal;
    	outColor    = vec3(1.0,1.0,1.0);
    	outUV       = inUV;
	
        vec4 pos    = camera.viewMatrix * vertexTranslation * vec4(inPos.xyz, 1.0);
        outLightVec = normalize ( mat3( camera.viewMatrixInverse ) * lightDirection );
        outViewVec  = -pos.xyz;
}
```

Input variables of fragment shader must correspond to output variables of vertex shader. 

```GLSL
    #version 450

    #extension GL_ARB_separate_shader_objects : enable
    #extension GL_ARB_shading_language_420pack : enable

    layout (location = 0) in vec3 inNormal;
    layout (location = 1) in vec3 inColor;
    layout (location = 2) in vec2 inUV;
    layout (location = 3) in vec3 inViewVec;
    layout (location = 4) in vec3 inLightVec;
```

Output variable must be declared. It will be stored as a result in a swapchain image :

```GLSL
layout (location = 0) out vec4 outFragColor;
```

Fragment shader main() function implements simple Phong shading :

```GLSL
void main() 
{
	vec4 color = vec4(inColor,1);

	vec3 N        = normalize(inNormal);
	vec3 L        = normalize(inLightVec);
	vec3 V        = normalize(inViewVec);
	vec3 R        = reflect(-L, N);
	vec3 ambient  = vec3(0.1,0.1,0.1);
	vec3 diffuse  = max(dot(N, L), 0.0) * vec3(0.9,0.9,0.9);
	vec3 specular = pow(max(dot(R, V), 0.0), 128.0) * vec3(1,1,1);
	outFragColor  = vec4(ambient + diffuse * color.rgb + specular, 1.0);
}
```



