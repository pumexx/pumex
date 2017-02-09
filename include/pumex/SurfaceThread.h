#pragma once
#include <memory>
#include <pumex/HPClock.h>

namespace pumex
{
  class Surface;

  // Each surface has a thread that renders its content
  // Thing to implement in a future : many threads per surface to implement VR
  class PUMEX_EXPORT SurfaceThread
  {
  public:
    explicit SurfaceThread();
    SurfaceThread(const SurfaceThread&)            = delete;
    SurfaceThread& operator=(const SurfaceThread&) = delete;
    virtual ~SurfaceThread()
    {}

    virtual void setup( std::shared_ptr<pumex::Surface> surface );
    void startFrame();
    void endFrame();
    virtual void cleanup();


    std::weak_ptr<pumex::Surface>                  surface;
    pumex::HPClock::time_point currentTime;
    pumex::HPClock::duration   timeSinceLastFrame;
    pumex::HPClock::duration   timeSinceStart;
    double timeSinceStartInSeconds;
    double lastFrameInSeconds;
  };
}
