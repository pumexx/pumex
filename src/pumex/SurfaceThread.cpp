#include <pumex/SurfaceThread.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/Viewer.h>
#include <pumex/utils/Log.h>

using namespace pumex;

SurfaceThread::SurfaceThread()
  : Thread(), surface{}
{
}

void SurfaceThread::setup(std::shared_ptr<pumex::Surface> s)
{
  surface            = s;
  currentTime        = pumex::HPClock::now();
  timeSinceStart     = currentTime - currentTime;
  timeSinceLastFrame = timeSinceStart;
}

void SurfaceThread::cleanup()
{
}


void SurfaceThread::run()
{
  while (true)
  {
    auto frameStart = pumex::HPClock::now();
    std::shared_ptr<Surface> surfaceSh = surface.lock();
    if (!surfaceSh)
      break;
    timeSinceLastFrame = frameStart - currentTime;
    timeSinceStart     = frameStart - surfaceSh->viewer.lock()->getStartTime();
    currentTime        = frameStart;
    surfaceSh->actions.performActions();
    if (surfaceSh->viewer.lock()->terminating())
      break;
    surfaceSh->beginFrame();
    draw();
    surfaceSh->endFrame();
    // FIXME - shouldn't be changed to some fence maybe ?
    VK_CHECK_LOG_THROW(vkDeviceWaitIdle(surfaceSh->device.lock()->device), "failed vkDeviceWaitIdle");
    std::this_thread::yield();
  }
}
