#include <pumex/SurfaceThread.h>
#include <pumex/Window.h>
#include <pumex/Surface.h>
#include <pumex/Viewer.h>
#include <pumex/utils/Log.h>

using namespace pumex;

SurfaceThread::SurfaceThread()
  : surface{}
{
}

void SurfaceThread::setup(std::shared_ptr<pumex::Surface> s)
{
  surface                 = s;
  currentTime             = pumex::HPClock::now();
  timeSinceStart          = currentTime - currentTime;
  timeSinceLastFrame      = timeSinceStart;
  timeSinceStartInSeconds = 0.0;
  lastFrameInSeconds      = 0.0;

}

void SurfaceThread::cleanup()
{
}

void SurfaceThread::startFrame()
{
  auto frameStart = pumex::HPClock::now();
  std::shared_ptr<Surface> surfaceSh = surface.lock();
  if (!surfaceSh)
    return;
  timeSinceLastFrame = frameStart - currentTime;
  timeSinceStart = frameStart - surfaceSh->viewer.lock()->getStartTime();
  timeSinceStartInSeconds = std::chrono::duration<double, std::ratio<1, 1>>(timeSinceStart).count();
  lastFrameInSeconds = std::chrono::duration<double, std::ratio<1, 1>>(timeSinceLastFrame).count();
  currentTime = frameStart;
  surfaceSh->actions.performActions();
  if (surfaceSh->viewer.lock()->terminating())
    return;
  surfaceSh->beginFrame();
}

void SurfaceThread::endFrame()
{
  std::shared_ptr<Surface> surfaceSh = surface.lock();
  if (!surfaceSh)
    return;
  surfaceSh->endFrame();
}


