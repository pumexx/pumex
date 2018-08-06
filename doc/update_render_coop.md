# Cooperation of render stage and update stage



Construction of render stage and update stage is inspired by following set of articles :

http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part1/
http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part2/
http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part3/
http://blog.slapware.eu/game-engine/programming/multithreaded-renderloop-part4/

Render stage and update stage work in parallel. Render stage tries to work as fast as possible ( when **immediate** and **multibox** presentation modes are used ), or according to vertical synchronization of  screens ( when **fifo**, or **fifo_relaxed** presentation modes are used). 

In contrast - update stage has constant time rate and is called only when render stage overtakes it on time scale. 

When user sets the update frequency to high values ( e.g. 150 updates per second ) and render stage works using **fifo** presentation mode with 60 Hz vertical sync then for each rendered frame we have 2-3 updates executed. 

When user sets update frequency to low values ( e.g. 10 updates per second ), then one update happens typically every 6th rendered frame. The second case shows that, if we want to have smooth rendering, then we have to interpolate/extrapolate data produced by update state. What tools does Pumex library give use to achieve this ?

## Elements of cooperation

When you learn about instanced rendering - there's one thing that is always hard for all newcomers: there's actually nothing about writing shaders for instanced rendering, except for this one variable that is available in vertex shader : *gl_InstanceIndex* . It is totally up to you, how are you going to utilize this variable.

Similarly - Pumex library does not give you much, when we ask about cooperation mechanisms. All you have is three indices :

- **update index** that you may acquire in update stage using method **Viewer::getUpdateIndex()**
- **render index** that you may acquire in render stage using method **Viewer::getRenderIndex()**
- **previous update index** that you may acquire in update stage using method **Viewer::getPreviousUpdateIndex()**

These three indices have following properties :

- all indices may only have values: 0, 1, or 2 

- when Pumex picks new render index, it must assure that this index:
  - is not equal to update index used currently by update stage
  - is newest updated index
- when Pumex picks new update index, it must assure that this index:
  - is not equal to render index used currently by render stage
  - is not used by previous update. Previous update index will be returned by **Viewer::getPreviousUpdateIndex()**

Rules for proper use of indices :

- for each data you use in both stages : create arrays of 3 elements. Data must be able to perform extrapolation / interpolation in render stage
- use **update index** in **update stage**, when you want to write data to an array
- if you need to know previous state in update stage - use **previous update index**. Data accessed by previous update index **must be treated as read only** ( previous update index may be equal to render index ).
- use **render index** in **render stage** to read data from an array. **Any data must be treated read-only in render stage**.

Besides indices **pumex::Viewer** class gives us few functions describing time :

- Viewer::getUpdateDuration() - returns 1/updateFrequency .
- Viewer::getUpdateTime() - returns current time of update. Used only in update stage.
- Viewer::getRenderTimeDelta() - returns difference between current time point and time point of update used to render data. This difference may be less than 0. Should be used only in render stage.

## Example : calculating camera movement

To show how it works in real life example - we will analyze **pumex::BasicCameraHandler** class ( some parts of the code will be removed or modified for clarity ).

This class has following variable defined :

```
std::array<Kinematic,3>  cameraCenter;
```

**pumex::Kinematic** structure stores information about object position, orientation, linear velocity and angular velocity. Information about velocities will help us extrapolate camera position in render stage.

As we know from tutorial - during update stage method **pumex::BasicCameraHandler::update(Viewer*)** is called. Let's see how it looks. 

First - we acquire updateIndex, previous update index and copy previous data to new one. We also set deltaTime variable to 1/updateFrequency using **Viewer::getUpdateDuration()** method :

```
void BasicCameraHandler::update(Viewer* viewer)
{
  uint32_t updateIndex     = viewer->getUpdateIndex();
  uint32_t prevUpdateIndex = viewer->getPreviousUpdateIndex();

  cameraCenter[updateIndex]   = cameraCenter[prevUpdateIndex];
  float deltaTime             = inSeconds(viewer->getUpdateDuration());

```

Using mouse input we calculate new camera orientation :

```
  if (leftMouseKeyPressed)
  {
    glm::quat qx = glm::angleAxis(5.0f * (lastMousePos.y - currMousePos.y), glm::vec3(1.0, 0.0, 0.0));
    
    glm::quat qz = glm::angleAxis(5.0f * (lastMousePos.x - currMousePos.x), glm::vec3(0.0, 0.0, 1.0));
    
    cameraCenter[updateIndex].orientation = glm::normalize( qz * cameraCenter[updateIndex].orientation * qx );
    
    lastMousePos = currMousePos;
  }
```

And using keyboard input we calculate new position :

```
  float camStep = 8.0f * deltaTime;
  if (moveFast)
    camStep = 24.0f * deltaTime;

  glm::vec3 camForward    = cameraCenter[updateIndex].orientation * glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 groundForward = glm::normalize(camForward - glm::proj(camForward, glm::vec3(0.0f, 0.0f, 1.0f)));
  
  glm::vec3 camRight      = cameraCenter[updateIndex].orientation * glm::vec3(1.0f, 0.0f, 0.0f);
  glm::vec3 groundRight   = glm::normalize(camRight - glm::proj(camRight, glm::vec3(0.0f, 0.0f, 1.0f)));
  
  glm::vec3 groundUp      = glm::vec3(0.0f, 0.0f, 1.0f);

  if (moveForward)
    cameraCenter[updateIndex].position -= groundForward * camStep;
  if (moveBackward)
    cameraCenter[updateIndex].position += groundForward * camStep;
  if (moveLeft)
    cameraCenter[updateIndex].position -= groundRight * camStep;
  if (moveRight)
    cameraCenter[updateIndex].position += groundRight * camStep;
  if (moveUp)
    cameraCenter[updateIndex].position += groundUp * camStep;
  if (moveDown)
    cameraCenter[updateIndex].position -= groundUp * camStep;
}

```

OK, so we now have old camera parameters in *cameraCenter[prevUpdateIndex]* and new camera parameters in *cameraCenter[updateIndex]* . Let's calculate velocities, so that the render stage will have a chance to extrapolate cameraCenter[renderIndex] to exact position :

```
  calculateVelocitiesFromPositionOrientation(cameraCenter[updateIndex], cameraCenter[prevUpdateIndex], deltaTime);
  
}
```

The update part is now over. As we have seen in tutorial, the exact camera view matrix is calculated using **pumex::BasicCameraHandler::getViewMatrix(Surface*)** in a render stage.

At first this method acquires pointer to Viewer, then uses it to obtain current renderIndex. deltaTime variable represents difference between render time and last update time :

```
glm::mat4 BasicCameraHandler::getViewMatrix(Surface* surface)
{
  Viewer* viewer       = surface->viewer.lock().get();
  uint32_t renderIndex = viewer->getRenderIndex();
  float deltaTime      = inSeconds( viewer->getRenderTimeDelta() );
```

Knowing these elements we calculate new camera position using **extrapolate(const Kinematic&)** function.

```
  return glm::inverse(extrapolate(cameraCenter[renderIndex], deltaTime));
}

```

You may experiment with -p and -u parameters in the examples to see how camera works when different update frequencies and different presentation modes are used.