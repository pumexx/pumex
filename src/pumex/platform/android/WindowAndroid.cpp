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

#include <pumex/platform/android/WindowAndroid.h>
#include <cstring>
#include <android/input.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <pumex/Viewer.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>

using namespace pumex;

std::unordered_map<int32_t, InputEvent::Key>     WindowAndroid::androidKeycodes;
std::unordered_map<android_app*, WindowAndroid*> WindowAndroid::registeredWindows;
android_app*                                     WindowAndroid::androidApp = nullptr;

void AndroidHandleAppCmd(android_app *app, int32_t cmd) 
{
  WindowAndroid* pumexWindow = WindowAndroid::getWindow(app);
  if(pumexWindow)
    pumexWindow->handleAppCmd(cmd);
}

int32_t AndroidHandleInputEvent(struct android_app* app, AInputEvent* event)
{
  WindowAndroid* pumexWindow = WindowAndroid::getWindow(app);
  if(pumexWindow)
    return pumexWindow->handleInputEvent(event);
  return 0;
}

WindowAndroid::WindowAndroid(const WindowTraits& windowTraits)
{
  // WindowAndroid ignores WindowTraits and may only have one window from android_app object
  // Wonder how to organize application with more than one NativeActivity...
  registerWindow(getAndroidApp(), this);
  window = getAndroidApp()->window;
  CHECK_LOG_THROW(window==nullptr, "Android window not defined" );
	
  if(androidKeycodes.empty())
    fillAndroidKeycodes();

  width  = newWidth  = ANativeWindow_getWidth(window);
  height = newHeight = ANativeWindow_getHeight(window);
}

WindowAndroid::~WindowAndroid()
{
  unregisterWindow(getAndroidApp());
}

std::shared_ptr<Surface> WindowAndroid::createSurface(std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits)
{
  // return existing surface when it was already created
  if (!surface.expired())
    return surface.lock();
  auto viewer = device->viewer.lock();
	
  VkSurfaceKHR vkSurface;
  VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo{};
    surfaceCreateInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.window = window;
  VK_CHECK_LOG_THROW(vkCreateAndroidSurfaceKHR(viewer->getInstance(), &surfaceCreateInfo, nullptr, &vkSurface), "Could not create surface");

  std::shared_ptr<Surface> result = std::make_shared<Surface>(device, shared_from_this(), vkSurface, surfaceTraits);
  viewer->addSurface(result);

  surface = result;
  return result;
}

int32_t WindowAndroid::handleInputEvent(AInputEvent* event)
{
  auto timeNow      = HPClock::now();
  int32_t inputType = AInputEvent_getType(event);
  if ( inputType == AINPUT_EVENT_TYPE_MOTION) 
  {
    int32_t motionAction = AMotionEvent_getAction(event);
	switch( motionAction )
	{
    case AMOTION_EVENT_ACTION_DOWN:
	{
      // for now touching screen simulates left mouse click
      float mx = AMotionEvent_getX(event, 0);
      float my = AMotionEvent_getY(event, 0);
      normalizeMouseCoordinates(mx, my);
      pushInputEvent( InputEvent(timeNow, InputEvent::MOUSE_KEY_PRESSED, InputEvent::LEFT, mx, my) );
	  break;
	}
    case AMOTION_EVENT_ACTION_UP:
	{
      // for now touching screen simulates left mouse click
      float mx = AMotionEvent_getX(event, 0);
      float my = AMotionEvent_getY(event, 0);
      normalizeMouseCoordinates(mx, my);
      pushInputEvent( InputEvent(timeNow, InputEvent::MOUSE_KEY_PRESSED, InputEvent::LEFT, mx, my) );
	  break;
	}
    case AMOTION_EVENT_ACTION_MOVE:
	{
      float mx = AMotionEvent_getX(event, 0);
      float my = AMotionEvent_getY(event, 0);
      normalizeMouseCoordinates(mx,my);
      pushInputEvent( InputEvent(timeNow, InputEvent::MOUSE_MOVE, InputEvent::BUTTON_UNDEFINED, mx, my) );
	  break;
	}
//    case AMOTION_EVENT_ACTION_CANCEL:
//	  break;
//    case AMOTION_EVENT_ACTION_BUTTON_PRESS:
//	  break;
//    case AMOTION_EVENT_ACTION_BUTTON_RELEASE:
//	  break;
	}
    return 1;
  }
  else if( inputType == AINPUT_EVENT_TYPE_KEY )
  {
    InputEvent::Key key = androidKeyCodeToPumex(AKeyEvent_getKeyCode(event));
    int32_t keyAction   = AKeyEvent_getAction(event);
    switch(keyAction)
    {
    case AKEY_EVENT_ACTION_DOWN:
      pushInputEvent( InputEvent( timeNow, InputEvent::KEYBOARD_KEY_PRESSED, key ) );
      break;
    case AKEY_EVENT_ACTION_UP:
      pushInputEvent( InputEvent( timeNow, InputEvent::KEYBOARD_KEY_RELEASED, key ) );
      break;
    default:
      break;
    }
	return 1;
  }
  return 0;
}

void WindowAndroid::handleAppCmd(int32_t cmd)
{
  switch (cmd) 
  {
  case APP_CMD_INIT_WINDOW:
    window = getAndroidApp()->window;
    break;
  case APP_CMD_WINDOW_RESIZED:
  {
    width  = newWidth  = ANativeWindow_getWidth(window);
    height = newHeight = ANativeWindow_getHeight(window);
	auto surfaceSh = surface.lock();
    surfaceSh->actions.addAction(std::bind(&Surface::resizeSurface, surfaceSh, newWidth, newHeight));
    break;
  }
  case APP_CMD_START:
    break;
  case APP_CMD_RESUME:
    break;
  case APP_CMD_PAUSE:
    break;
  case APP_CMD_STOP:
    break;
  case APP_CMD_TERM_WINDOW:
  {
    auto viewer = surface.lock()->viewer.lock();
    auto id     = surface.lock()->getID();
    viewer->removeSurface(id);
    break;
  }
  default:
    break;
  }
}

void WindowAndroid::normalizeMouseCoordinates(float& x, float& y) const
{
  // x and y are defined in windows coordinates as oposed to Windows OS
  x = x / width;
  y = y / height;
}

InputEvent::Key WindowAndroid::androidKeyCodeToPumex(int32_t keycode) const
{
  auto it = androidKeycodes.find(keycode);
  if(it != end(androidKeycodes) )
    return it->second;
// Line below is handy for recognizing new keycodes.
//  LOG_ERROR << "Unknown keycode : 0x" << std::hex << (uint32_t)keycode << std::endl;
  return InputEvent::KEY_UNDEFINED;
}

int WindowAndroid::runMain(android_app* app, AndroidMainFunction mainFunction)
{
  CHECK_LOG_THROW(app==nullptr, "android_app is not defined for this application");
  CHECK_LOG_THROW(mainFunction==nullptr, "mainFunction is not defined for this application");
  
  androidApp                   = app;
  androidApp->onAppCmd         = AndroidHandleAppCmd;
  androidApp->onInputEvent     = AndroidHandleInputEvent;
  androidApp->destroyRequested = 0;
  
  // the whole application code happens here
  (*mainFunction)(0, nullptr);
  
  return androidApp->destroyRequested;
}

android_app* WindowAndroid::getAndroidApp()
{
  return androidApp;
}

bool WindowAndroid::checkWindowMessages()
{
  assert(WindowAndroid::getAndroidApp() != nullptr);
  int events;
  android_poll_source *source;
  // Poll all pending events.
  if (ALooper_pollAll(0, NULL, &events, (void **)&source) >= 0) 
  {
    // Process each polled events
    if (source != NULL) 
      source->process(WindowAndroid::getAndroidApp(), source);
  }
  return WindowAndroid::getAndroidApp()->destroyRequested == 0;
}

void WindowAndroid::registerWindow(android_app* app, WindowAndroid* pumexWindow)
{
  registeredWindows.insert({app,pumexWindow});
}

void WindowAndroid::unregisterWindow(android_app* app)
{
  registeredWindows.erase(app);
}

WindowAndroid* WindowAndroid::getWindow(android_app* app)
{
  auto it = registeredWindows.find(app);
  if (it == end(registeredWindows))
    it = registeredWindows.find(nullptr); // You return to your own code after a while and do not recognize it sometimes...
  if (it == end(registeredWindows))
    return nullptr;
  return it->second;
}

void WindowAndroid::fillAndroidKeycodes()
{
  // important keys
  androidKeycodes.insert({AKEYCODE_ESCAPE,     InputEvent::ESCAPE});
  androidKeycodes.insert({AKEYCODE_SPACE,      InputEvent::SPACE});
  androidKeycodes.insert({AKEYCODE_TAB,        InputEvent::TAB});
  androidKeycodes.insert({AKEYCODE_SHIFT_LEFT, InputEvent::SHIFT});

  int32_t i=0;

  // keys F1-F10
  typedef EnumIterator<InputEvent::Key, InputEvent::Key::F1, InputEvent::Key::F10> FunKeyIterator;
  i=AKEYCODE_F1;
  for(InputEvent::Key f : FunKeyIterator())
    androidKeycodes.insert({i++, f});

  // numbers
  typedef EnumIterator<InputEvent::Key, InputEvent::Key::N0, InputEvent::Key::N9> NumKeyIterator;
  i=AKEYCODE_0;
  for(InputEvent::Key f : NumKeyIterator())
    androidKeycodes.insert({i++, f});

  // letters
  typedef EnumIterator<InputEvent::Key, InputEvent::A, InputEvent::Z> LetterKeyIterator;
  i=AKEYCODE_A;
  for(InputEvent::Key f : LetterKeyIterator())
    androidKeycodes.insert({i++, f});
  
}
