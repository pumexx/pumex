# Description of Pumex classes

It is assumed that person reading this document knows Vulkan specification and its terminology.

The most important Pumex classes may be divided into 3 different categories :

- **basic classes** - equivalents of Vulkan objects ( for example pumex::RenderPass is an equivalent of VkRenderPass from Vulkan )
- **compound classes** - classes that implement some complex functionality and use basic classes to achieve this goal ( for example pumex::Text class that implements text rendering ) 
- **infrastructure classes** that implement Vulkan instances, devices, surfaces and windows.



## Rules for basic classes

All basic Pumex classes have been implemented using the same set of rules :

- one basic Pumex object manages one ( or more ) Vulkan object :
  - **on each logical device**, or 
  - **on each surface**
- objects existing **on each logical device** may be distinguished from others by possesion of internal structure named **PerDeviceData**. These objects store a copy of that structure for each device.
- objects existing **on each surface** may be distinguished from others by possesion of internal structure named **PerSurfaceData**. These objects store a copy of that structure for each surface.
- some classes ( mainly buffers and descriptor sets ) may manage more that one Vulkan object on each device(surface), so that if we have one command buffer for each swapchain image then each command buffer may have use its own Vulkan object and all of these objects are managed by single Pumex object. The number of Vulkan objects stored per device/surface is defined in a class constructor ( as **activeCount** parameter ). During application run, specific Vulkan object may be chosen using **setActiveIndex()** method.
- each class has a method named **validate(Device* )** ( for objects working per device ), or **validate(Surface* )** ( for objects working per surface ). This method is responsible for creation and update of corresponding Vulkan objects on a specified device or surface ( for example: method pumex::UniformBuffer::validate(Device* , ... ) creates a buffer on a specified logical device and sends data to it ).
- to invalidate data in a specific object user must call **setDirty()** method on it. 
- objects that require device memory allocation ( buffers and images ) must get a pointer to **pumex::DeviceMemoryAllocator** class, that manages memory. 
- objects that may be set as a data sources in a descriptor set must inherit from **pumex::DescriptorSetSource** class and must implement one of two methods named **getDescriptorSetValues()**. When content of these classes changed so much, that descriptor sets must be rebuilt then these objects must call **notifyDescriptorSets()** method ( for example: when pumex::StorageBuffer increases its size then corresponding device memory should be also increased. Unfortunately Vulkan buffer must be recreated, because buffers simply cannot have device memory reallocated. The only solution is to destroy buffer and create a new one. Such situation causes undefined behaviour in using descriptor sets that point to this storage buffer - we must inform descriptor sets that they must call vkUpdateDescriptorSets() to update its state ).
- objects that take direct part in command buffer creation must inherit from **pumex::CommandBufferSource**. Command buffer must always use existing objects, so, if one of these objects is recreated, then corresponding command buffers should be rebuilt. To inform them about this user should call **notifyCommandBuffers()** ( for example changes in pumex::GraphicsPipeline mean that command buffer using this pipeline becomes invalid. To inform it about a rebuild necessity, graphics pipeline should call notifyCommandBuffers() ). Classes that are directly used in command buffers : descriptor set, graphics pipeline, compute pipeline, render pass, framebuffer, and generic buffers ( when used as index buffers and vertex buffers ).
- there are three classes created for memory buffers :
  - pumex::UniformBuffer<T> - stores **one object of class T** and creates buffer of type VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT.
  - pumex::StorageBuffer<T> - stores **a vector of objects T** and creates buffer of type VK_BUFFER_USAGE_STORAGE_BUFFER_BIT.
  - pumex::GenericBuffer<T> - stores a pointer to external object T. Type of the buffer is defined in a constructor.
- above mentioned buffers store **the same data on each device**. There is additional set of classes that are able to store **different data on each surface** :
  - pumex::UniformBufferPerSurface<T> - analogous to pumex::UniformBuffer<T>
  - pumex::StorageBufferPerSurface<T> - analogous to pumex::StorageBuffer<T>
  - pumex::GenericBufferPerSurface<T> - analogous to pumex::GenericBuffer<T> 
- Data for first set of buffer classes are the same for each device. Data for *PerSurface buffers may be different for each surface. Let's suppose that user needs to store different camera parameters ( projection matrix, view matrix ) for each surface. In that case user should choose pumex::UniformBufferPerSurface class to store it as uniform buffer.



There are exceptions to above set of rules. For example :

- pumex::CommandBuffer class exists only on one surface. User is responsible for creating its copies on each surface.
- similarly class pumex::Image exists only on one device/surface. pumex::Image is only a component of pumex::Texture and pumex::FrameBufferImages classes.
- setting a new values to pumex::UniformBuffer and pumex::StorageBuffer classes using set() methods invalidates this class. There's no need for subsequent call for setDirty().



## Basic Pumex classes in detail

### pumex::DeviceMemoryAllocator

Vulkan does not like many memory allocations ( it even defines maximum count of available memory allocations ) and typically in more sophisticated applications there's a need to make a lot of them. To overcome this issue Pumex introduces **pumex::DeviceMemoryAllocator** class. This class allocates required size of memory of particular type ( host visible, or local to a device ) on each available device and provides methods to allocate and deallocate chunks of earlier allocated memory ( suballocations ). Pointer to pumex::DeviceMemoryAllocator must be provided as a constructor parameter in each class that requires memory allocations ( pumex::UniformBuffer, pumex::StorageBuffer, pumex::Image, pumex::FrameBufferImages and many more).

This class also have methods to copy data to host visible device memory - user should use it instead of individual calls to vkMapMemory() and vkUnmapMemory(), because pumex::DeviceMemoryAllocator provides mutual exclusion for this memory chunk.



### pumex::UniformBuffer<T>

Class that stores **single object of class T** on **each device** and sends it to a device as **uniform buffer** ( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ).

Data will be transfered to the GPU through staging buffers, or through allocator that performs vhMapMemory()/memcpy()/vkUnmapMemory(). Transferring data depends on the type of memory used by the allocator ( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT or VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ).

It is user responsibility to ensure that class T meets the criteria of the **std140** alignment. Otherwise data may not be read properly by the shader.

Content of the data may be read/written by the get() and set() methods. set() method causes data invalidation.

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.

Class has setActiveIndex() functionality.

### pumex::UniformBufferPerSurface<T>

Class that stores **different object of class T** on **each surface** and sends it to a device as **uniform buffer** ( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ).

Data will be transfered to the GPU through staging buffers, or through allocator that performs vhMapMemory()/memcpy()/vkUnmapMemory(). Transferring data depends on the type of memory used by the allocator ( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT or VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ).

It is user responsibility to ensure that class T meets the criteria of the **std140** alignment. Otherwise data may not be read properly by the shader.

Content of the data may be read/written by the get(Surface* ) and set(Surface* , T ) methods. There is also set(T) method that sets the same data on every surface. set() methods cause data invalidation.

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class creates Vulkan object on each Vulkan surface. Data on all surfaces may be different.

Class has setActiveIndex() functionality.



### pumex::StorageBuffer<T>

Class that stores **the same vector of objects of class T** on **each device** and sends it to a device as **storage buffer** ( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ).

Data will be transfered to the GPU through staging buffers, or through allocator that performs vhMapMemory()/memcpy()/vkUnmapMemory(). Transferring data depends on the type of memory used by the allocator ( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT or VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ).

It is user responsibility to ensure that class T meets the criteria of the **std430** alignment. Otherwise data may not be read properly by the shader.

Content of the data may be read/written by the get() and set() methods. set() method causes data invalidation.

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.

Class has setActiveIndex() functionality.



### pumex::StorageBufferPerSurface<T>

Class that stores **different vector of objects of class T** on **each surface** and sends it to a device as **storage buffer** ( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ).

Data will be transfered to the GPU through staging buffers, or through allocator that performs vhMapMemory()/memcpy()/vkUnmapMemory(). Transferring data depends on the type of memory used by the allocator ( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT or VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ).

It is user responsibility to ensure that class T meets the criteria of the **std430** alignment. Otherwise data may not be read properly by the shader.

Content of the data may be read/written by the get(Surface* ) and set(Surface* , vector<T> ) methods. There is also set() method that sets the same data on every surface. set() methods cause data invalidation.

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class creates Vulkan object on each Vulkan surface. Data on all surfaces may be different.

Class has setActiveIndex() functionality.



### pumex::GenericBuffer<T>

Class that stores **the same data pointed by the pointer to the object of class T** on **each device** and sends it to a device with **buffer type defined in a constructor** .

Data will be transfered to the GPU through staging buffers, or through allocator that performs vhMapMemory()/memcpy()/vkUnmapMemory(). Transferring data depends on the type of memory used by the allocator ( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT or VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ).

It is user responsibility to ensure that class T meets the criteria of the **std140** or **std430** alignment ( it depends on the buffer type ). Otherwise data may not be read properly by the shader.

Content of the data is read/written outside of the class boundary, so **setDirty()** method must be used to inform a buffer, that data needs validation.

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class also inherits from CommandBufferSource, because it may be used as index buffer, or vertex buffer.

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.

Class has setActiveIndex() functionality.



### pumex::GenericBufferPerSurface<T>

Class that stores **different data pointed by the pointer to the object of class T** on **each surface** and sends it to a device with **buffer type defined in a constructor** .

Data will be transfered to the GPU through staging buffers, or through allocator that performs vhMapMemory()/memcpy()/vkUnmapMemory(). Transferring data depends on the type of memory used by the allocator ( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT or VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ).

It is user responsibility to ensure that class T meets the criteria of the **std140** or **std430** alignment ( it depends on the buffer type ). Otherwise data may not be read properly by the shader.

Content of the data is read/written outside of the class boundary, so **setDirty()** method must be used to inform a buffer, that data needs validation.

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class also inherits from CommandBufferSource, because it may be used as index buffer, or vertex buffer.

Class creates Vulkan object on each Vulkan surface. Data on all surfaces may be different.

Class has setActiveIndex() functionality.



### pumex::Image

Class, that is not used directly by the user. It is used by the **pumex::Texture** and **pumex::FrameBufferImages** classes to store image per device or per surface.

Class represents a tandem of **VkImage** and **VkImageView** Vulkan objects.

There are two constructors for this class - the first one  creates a new image and uses memory allocation through allocator. To create an image user must provide pumex::ImageTraits structure, that defines all parameters required to create VkImage and VkImageView. The second one uses provided VkImage and does not allocate memory ( image may be provided by swapchain for example ).



### pumex::Texture

Class represents following Vulkan objects : **VkImage**, **VkImageView** and **VkSampler**. During texture creation user must provide **pumex::TextureTraits** structure that provides most of the VkSampler parameters.

This class is responsible for creating a texture that may be read from disk. Data may be read using object that inherits from **pumex::TextureLoader** class. Currently only one loader is available - the one that reads dds and ktx textures using **GLI** library. pumex::Texture stores gli::texture object internally.

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.

### pumex::DesciptorSet

Class represents **VkDescriptorSet**.

To create a DescriptorSet user is required to provide **pumex::DescriptorSetLayout** class ( that defines required bindings and types of data bound ) and **pumex::DescriptorPool**. The size of the DescriptorPool should take into account number of created surfaces and the number of descriptors created on each surface ( corresponding to activeCount in DescriptorSet constructor ).

Each descriptor set source may be connected to a pumex::DescriptorSet using setSource(binding, source) method. Typical sources include : UniformBuffer, StorageBuffer, InputAttachment, Texture etc.

Class inherits from CommandBufferSource, because it may be used directly in a command buffer.

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.

Class has setActiveIndex() functionality.



### pumex::GraphicsPipeline

Class that represents **VkPipeline** created using function vkCreateGraphicsPipelines().

To create a class user is required to provide: **pumex::PipelineCache**, **pumex::PipelineLayout**, **pumex::RenderPass** ans **subpass index**.

Class stores complete state of the graphics pipeline ( vertex input, assembly, tesselation, rasterization, blend, depth, stencil, viewport, scissor, multisample, shader stages ). For each type of state ( except for shader stages ) class provides default values.

Class inherits from CommandBufferSource, because it may be used directly in a command buffer.

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.



### pumex::ComputePipeline

Class that represents **VkPipeline** created using function vkCreateComputePipelines().

Class stores complete state of the compute pipeline ( which means only compute shader ).

Class inherits from CommandBufferSource, because it may be used directly in a command buffer.

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.



### pumex::RenderPass

Class that represents **VkRenderPass** on each device.

Each render pass consists of the following elements ( declared in a constructor )

- vector of all attachments used in a render pas ( **pumex::AttachmentDefinition** )
- vector of all subpasses. In each subpass there is a definition which attachments are declared as input, output, depth, resolve and preserved ( **pumex::SubpassDefinition** )
- vector of all subpass dependencies ( pumex::SubpassDependencyDefinition )

Class creates Vulkan object on each Vulkan device. Data on all devices is identical.



### pumex::FrameBufferImages

FrameBufferImages is a class that declares and stores all images used by one or more framebuffers in rendering process. 

FrameBufferImages  requires pumex::DeviceMemoryAllocator object that will be allocating memory of images.

Class creates Vulkan object on each Vulkan surface. Data on all surfaces may be different.



### pumex::FrameBuffer

FrameBuffer uses a subset of images from FrameBufferImages to make its work done ( more than one framebuffer may use FrameBufferImages object ).

User needs to deliver pumex::FrameBufferImages  and pumex::RenderPass to create a pumex::FrameBuffer object.

Class inherits from CommandBufferSource, because it may be used directly in a command buffer.

Class creates Vulkan object on each Vulkan surface. 



### pumex::InputAttachment

Class is responsible for providing one of framebuffer images as a source for DescriptorSet . 

Class inherits from the DescriptorSetSource, so it may be associated with the descriptor set using  pumex::DescriptorSet::setSource() method.

Class creates Vulkan object on each Vulkan surface. 



### pumex::QueryPool

Class stores a group of VkQuery objects.

Class creates Vulkan object on each Vulkan surface. 



## Compound Pumex classes in detail

Compound Pumex classes are classes that use basic objects ( like buffers or textures ) to perform some complex functionality like text rendering, or 3D model rendering. There are few such classes defined at the moment in Pumex library :

### pumex::AssetBuffer

Here is the brief overview of this class. In the future I will write some more detailed article about this class.

Main purpose of this class is to render 3D assets providing methods for normal and instanced rendering.

Asset is a single 3D object read from a single file and consisting of:

- a collection of 3D meshes ( geometries ). Each geometry has an attribute called **render mask**, that distinguishes different aspects of rendering in an asset( for example : normal geometry, transparent geometry, light cones, special effects, collision volumes etc ). It is user responsibility to mark each geometry with specific render mask, because asset read from file has all render masks set to 1 by default. Each group of geometries with the same render mask should have vertices defined in the same  manner. Such vertex definition is called vertex semantic and it may contain : position, normals, colors, tangents, bitangents and texture coordinates.
- a set of materials. Material consists of a a set of texture names and a set of named properties. Each property has vec4 type. Each texture name is described by the type of the texture also called a texture semantic ( for example : albedo texture, normal texture, etc. ). Individual textures should be read by the user.
- a skeleton - a set of bones organized into a tree hierarchy. Simplest skeleton consist of only one bone.
- a set of skeletal animations

To render an asset we must first register it as a new type using method pumex::AssetBuffer::registerType(). This method gives us a new type ID. Now we register an asset using newly created typeID using method pumex::AssetBuffer::registerObjectLOD(). After AssetBuffer validation we are able to draw an object calling pumex::AssetBuffer::cmdBindVertexIndexBuffer() and then pumex::AssetBuffer::cmdDraw().

This is the simplest and the slowest method of using AssetBuffer ( because it draws only one object at once ). To use AssetBuffer in instanced rendering we have additionally use object called **pumex::AssetBufferInstancedResults** that stores intermediate results. I invite you to review **pumexcrowd** and **pumexgpucull** examples to see how it's done ( or wait for a promised article ).



### pumex::MaterialSet

AssetBuffer completely avoids subject of materials, because now there exists **pumex::MaterialSet** class that manages all materials on its own, by storing material values in storage buffers accessible in shaders. User has full freedom to define materials whatever he/she likes as long as material structure is **std430** compliant. pumex::MaterialSet also uses additional helper classes that are able to manage textures ( pumex::TextureRegistry class and its descendants).

In examples you may see, how all these classes () are used to render materials :

- **pumexcrowd** example creates **texture array** as texture storage. Texture array may store only textures of the same size.
- **pumexdeferred** example goes one step further storing many textures in **array of textures**. Textures may have any size, but depending on Vulkan driver - their quantity may be limited.
- **pumexgpucull** example does not use textures and its TextureRegistry descendant just ignores it during material creation.
- **pumexviewer** example does not use MaterialSet at all.

As in AssetBuffer case - I will write a detailed article about MaterialSet in a future.



### pumex::Font and pumex::Text

These classes use **freetype2** library to read fonts into Vulkan texture and then use it for fast text rendering.



## Infrastructure classes in detail

Most of the Vulkan programs I've seen so far use only one logical device, one window and one surface. What distinguishes Pumex library from such programs is **the ability to create extended infrastructure potentially consisting  of many devices, windows and surfaces** - all used at once thanks to multithreading. There exists a group of classes to facilitate this goal :



### pumex::Viewer

The most important class in Pumex library. It is responsible for :

- creating a Vulkan instance ( VkInstance )
- collection of information about physical devices ( VkPhysicallDevice ).
- preparation of two independent TBB flow graphs : first one for data update, and second one for data rendering.
- creation of Vulkan debug facilities on demand
- registration of data directories
- storing user created logical devices ( VkDevice ) and surfaces ( VkSurfaceKHR )
- storing shared pointers to user created windows, so that window will not get destroyed before Vulkan surfaces are removed

To create a viewer user needs to provide **pumex::ViewerTraits** structure, that consists of :

- application name
- if viewer has to create Vulkan debug layers and other debug facilities
- what Vulkan debug layers will be created
- what will be update thread frequency ( in updates per second )

After viewer creation user is able to add new logical devices using **pumex::Viewer::addDevice()** method that has following parameters :

- physical device index
- a list of Vulkan queues that the device will use
- a list of extensions used by device

New surfaces may be created using **pumex::Viewer::addSurface()** method with following parameters :

- pointer to the user created window ( that inherits from **pumex::Window** class ).
- pointer to previously created logical device, on which the surface will work
- structure pumex::SurfaceTraits that has all aother parameters required for surface creation

Viewer stores all logical devices, surfaces and windows and is able to release them using **pumex::Viewer::cleanup()** method.

Viewer stores two independent TBB workflow graphs

- first one is responsible for data update with fixed timestep defined in pumex::ViewerTraits structure.
- second one is responsible for data rendering with timestep dependent on values provided in pumex::SurfaceTraits.

To avoid data races between these two graphs Viewer provides two special variables : renderIndex and updateIndex. Value stored in these variables is always 0, 1 or 2, but value of these variables is never the same in the same moment in time. Thanks to this application may create data sets ( for example 3-element arrays ), that will never read/written at once by update and render phases ( no additional synchronization is required). Details of this technique may be obtained from the following articles :

http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part1/
http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part2/
http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part3/
http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part4/

Another functionality provided by Viewer is time measurment : pumex::Viewer::getUpdateDuration(), pumex::Viewer::getUpdateTime(), pumex::Viewer::getRenderTimeDelta() .

Viewer stores default directories in which files are located, when full file path will not be provided ( methods pumex::Viewer::addDefaultDirectory() and pumex::Viewer::getFullFilePath() ).



### pumex::PhysicalDevice

This class does not take active part in application work. It serves only as a database of information about physical device ( GPU ) :

- device properties
- device features
- device memory properties
- available extensions
- available queue families and its properties



### pumex::Device

Represents Vulkan logical device ( or simply: device ).

When creating logical device :

- a number of queues defined in constructor is reserved for device use. If underlying physical device is not able to deliver required number of queues - the device creation will fail. Queues reserved during device creation may be later used by Surface as presentation queues, or directly depending on user needs.
- physical device is asked if it may deliver required extensions
- extension VK_EXT_DEBUG_MARKER_EXTENSION_NAME is initialized if user requests it

During its work the device creates and manages staging buffers ( buffers used to transfer data to device local memory ).

Device has an ID assigned by the viewer.

Raw pointer to a device, or variable device ( of VkDevice type ) is frequently used as  key to maps and unordered maps in objects storing data in PerDeviceData structures.



### pumex::Window

Represents single application window.

This class is a base class. Its descendants are responsible for windows management on different operating systems :

- **pumex::WindowWin32** class - creates a window on Windows systems
- **pumex::WindowXcb** class - creates a window on Linux system using XCB and X11 library

User must provide pumex::WindowTraits structure to create a window. It contains following parameters: 

- screen number on which window will be created, 
- window position and size, 
- window type 
- window title.

Available window types :

- WINDOW - normal window
- FULLSCREEN - fullscreen window iwthout window decoration
- HALFSCREEN_LEFT and HALFSCREEN_RIGHT - windows taking left or right half of the screen. A pair of such windows may serve as a base to render on VR gogles.

User creates windows itself and then provides it to the Viewer class during surface creation.

According to Vulkan specification a window may only have one surface attached.

Window input events ( mouse and keyboard )  may be collected during update phase.



### pumex::Surface

Represents Vulkan surface ( VkSurfaceKHR ).

Surfaces are created by pumex::Window descendants, because surface creation is OS dependent.

To create a surface user needs to provide **pumex::SurfaceTraits** structure that contains : 

- a number of images that swapchain will create for this surface
- color space used by swapchain images
- number of layers in each image
- method of swapchain presentation ( immediate, FIFO, mailbox, FIFO relaxed, etc. )
- swapchain image transformation
- swapchain alpha use
- definition of a presentation queue ( must be one of queues defined for underlying logical device )
- main render pass
- definition of all images used by framebuffers ( pumex::FrameBufferImages )

Every surface stores information about following elements :

- VkSurfaceKHR handle ( used as a key in maps and unordered maps in most ot the objets with PerSurfaceData structure )
- pumex::Device pointer to a device that this surface is associated with
- presentation queue handle ( VkQueue )
- pointer to pumex::CommandPool object
- swapchain data ( for example : active swapchain image index )
- framebuffer
- main render pass
- pointer to pumex::FrameBufferImages

Surface has an ID assigned by the viewer.

Raw pointer to a surface, or variable surface ( of VkSurfaceKHR type ) is frequently used as  key to maps and unordered maps in objects storing data in PerSurfaceData structures.

There are two important methods in Surface : **pumex::Surface::beginFrame()** and **pumex::Surface::endFrame()**, that must be called before and after each draw to a surface. They are responsible for acquisition of a next image from swapchain and for sending ready image to presentation.



