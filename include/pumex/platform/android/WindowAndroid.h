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
#include <unordered_map>
#include <mutex>
#include <pumex/Window.h>

class android_app;
class ANativeWindow;
class AInputEvent;

namespace pumex
{

typedef int (*AndroidMainFunction)( int , char* []);

// class implementing a pumex::Window on Android system
class WindowAndroid : public Window, public std::enable_shared_from_this<WindowAndroid>
{
public:
  explicit WindowAndroid(const WindowTraits& windowTraits);
  WindowAndroid(const WindowAndroid&)            = delete;
  WindowAndroid& operator=(const WindowAndroid&) = delete;
  virtual ~WindowAndroid();

  std::shared_ptr<Surface>    createSurface(std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits) override;
  int32_t                     handleInputEvent(AInputEvent* event);
  void                        handleAppCmd(int32_t cmd);
  void                        normalizeMouseCoordinates( float& x, float& y) const;
  InputEvent::Key             androidKeyCodeToPumex(int32_t keycode) const;
  
  static int                  runMain(android_app* androidApp, AndroidMainFunction* mainFunction);
  static android_app*         getAndroidApp();
  static bool                 checkWindowMessages();
  
  static void                 registerWindow(android_app* app, WindowAndroid* pumexWindow);
  static void                 unregisterWindow(android_app* app);
  static WindowAndroid*       getWindow(android_app* app);

  float lastMouseX = 0.0f;
  float lastMouseY = 0.0f;
  bool  resizeCalled = false;
  HPClock::time_point lastResizeTimePoint;
protected:
  static android_app*         androidApp;
  static std::unordered_map<android_app*, WindowAndroid*> registeredWindows;
  
  ANativeWindow*              window;

  static std::unordered_map<int32_t, InputEvent::Key> androidKeycodes;
  static void fillAndroidKeycodes();
};

}
