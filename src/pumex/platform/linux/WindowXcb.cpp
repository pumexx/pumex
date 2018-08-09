//
// Copyright(c) 2017 Paweł Księżopolski ( pumexx )
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

#include <pumex/platform/linux/WindowXcb.h>
#include <cstring>
#include <pumex/Viewer.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>

using namespace pumex;

xcb_connection_t*                             WindowXcb::connection        = nullptr;
const xcb_setup_t*                            WindowXcb::connectionSetup   = nullptr;
std::unordered_map<uint32_t, InputEvent::Key> WindowXcb::xcbKeycodes;
std::unordered_map<xcb_window_t, WindowXcb*>  WindowXcb::registeredWindows;
std::mutex                                    WindowXcb::regMutex;

static const char PUMEX_WINDOW_CLASS_ON_LINUX[] = "pumex_class\0pumex_class";

WindowXcb::WindowXcb(const WindowTraits& windowTraits)
{
  if(xcbKeycodes.empty())
    fillXcbKeycodes();
  int screenNum;
  if (registeredWindows.empty())
  {
    screenNum = windowTraits.screenNum;
    connection = xcb_connect(NULL, &screenNum);
    int errorNum = xcb_connection_has_error(connection);
    CHECK_LOG_THROW(errorNum>0, "Cannot create XCB connection. Error number " << errorNum);

    connectionSetup = xcb_get_setup(connection);
  }
  xcb_screen_iterator_t iter  = xcb_setup_roots_iterator(connectionSetup);
  for (int s = screenNum; s > 0; s--)
    xcb_screen_next(&iter);
  screen = iter.data;

  window = xcb_generate_id(connection);
  uint32_t eventMask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  uint32_t valueList[] = { screen->black_pixel, XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE };

  switch(windowTraits.type)
  {
  case WindowTraits::WINDOW:
    xcb_create_window( connection, XCB_COPY_FROM_PARENT, window, screen->root, windowTraits.x, windowTraits.y, windowTraits.w, windowTraits.h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, eventMask, valueList);
    break;
  case WindowTraits::FULLSCREEN:
    xcb_create_window( connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, screen->width_in_pixels, screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, eventMask, valueList);
    break;
  case WindowTraits::HALFSCREEN_LEFT:
    xcb_create_window( connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, screen->width_in_pixels / 2, screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, eventMask, valueList);
    break;
  case WindowTraits::HALFSCREEN_RIGHT:
    xcb_create_window( connection, XCB_COPY_FROM_PARENT, window, screen->root, screen->width_in_pixels / 2, 0, screen->width_in_pixels/ 2, screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, eventMask, valueList);
    break;
  }
  // Window managers on Linux like to place your windows in random positions. Using window class you can create a rule ( in a window manager ) that says that you want to place your window in exact position
  xcb_change_property( connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, 23, PUMEX_WINDOW_CLASS_ON_LINUX);
  // set window name
  xcb_change_property( connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, windowTraits.windowName.size(),   windowTraits.windowName.c_str());

  xcb_intern_atom_cookie_t wmDeleteCookie    = xcb_intern_atom(connection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
  xcb_intern_atom_cookie_t wmProtocolsCookie = xcb_intern_atom(connection, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
  xcb_intern_atom_reply_t* wmDeleteReply     = xcb_intern_atom_reply(connection, wmDeleteCookie, NULL);
  xcb_intern_atom_reply_t* wmProtocolsReply  = xcb_intern_atom_reply(connection, wmProtocolsCookie, NULL);
  wmDeleteWin = wmDeleteReply->atom;
  wmProtocols = wmProtocolsReply->atom;
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, wmProtocolsReply->atom, 4, 32, 1, &wmDeleteReply->atom);

  registerWindow(window, this);

  switch(windowTraits.type)
  {
  case WindowTraits::WINDOW:
    break;
  case WindowTraits::FULLSCREEN :
  {
    xcb_intern_atom_cookie_t cookieWms = xcb_intern_atom(connection, false, strlen("_NET_WM_STATE"), "_NET_WM_STATE");
    xcb_intern_atom_reply_t* atomWms   = xcb_intern_atom_reply(connection, cookieWms, NULL);

    xcb_intern_atom_cookie_t cookieFs  = xcb_intern_atom(connection, false, strlen("_NET_WM_STATE_FULLSCREEN"), "_NET_WM_STATE_FULLSCREEN");
    xcb_intern_atom_reply_t* atomFs = xcb_intern_atom_reply(connection, cookieFs, NULL);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, atomWms->atom, XCB_ATOM_ATOM, 32, 1, &(atomFs->atom));
    free(atomFs);
    free(atomWms);
    break;
  }
  case WindowTraits::HALFSCREEN_LEFT:
  case WindowTraits::HALFSCREEN_RIGHT:
  {
    xcb_intern_atom_cookie_t cookieWms = xcb_intern_atom(connection, false, strlen("_NET_WM_STATE"), "_NET_WM_STATE");
    xcb_intern_atom_reply_t* atomWms   = xcb_intern_atom_reply(connection, cookieWms, NULL);

    xcb_intern_atom_cookie_t cookieMV  = xcb_intern_atom(connection, false, strlen("_NET_WM_STATE_MAXIMIZED_VERT"), "_NET_WM_STATE_MAXIMIZED_VERT");
    xcb_intern_atom_reply_t* atomMV    = xcb_intern_atom_reply(connection, cookieMV, NULL);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, atomWms->atom, XCB_ATOM_ATOM, 32, 1, &(atomMV->atom));

    // FIXME : I haven't found any reliable method to turn window decorations off :(

    free(atomMV);
    free(atomWms);
    break;
  }
  }

  // collect window size
  xcb_get_geometry_cookie_t cookie;
  xcb_get_geometry_reply_t *reply;
  cookie = xcb_get_geometry(connection, window);
  if ((reply = xcb_get_geometry_reply(connection, cookie, NULL)))
  {
    width  = newWidth  = reply->width;
    height = newHeight = reply->height;
  }
  else
  {
    width  = newWidth  = windowTraits.w;
    height = newHeight = windowTraits.h;
  }
  free(reply);

  xcb_map_window(connection, window);
  xcb_flush(connection);
}

WindowXcb::~WindowXcb()
{
  unregisterWindow(window);
  xcb_destroy_window(connection, window);
  if (registeredWindows.empty())
  {
    xcb_disconnect(connection);
    connection = nullptr;
  }
}

std::shared_ptr<Surface> WindowXcb::createSurface(std::shared_ptr<Viewer> v, std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits)
{
  VkSurfaceKHR vkSurface;
  VkXcbSurfaceCreateInfoKHR surfaceCreateInfo{};
    surfaceCreateInfo.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.connection = connection;
    surfaceCreateInfo.window     = window;
  VK_CHECK_LOG_THROW(vkCreateXcbSurfaceKHR(v->getInstance(), &surfaceCreateInfo, nullptr, &vkSurface), "Could not create surface");

  std::shared_ptr<Surface> result = std::make_shared<Surface>(v, shared_from_this(), device, vkSurface, surfaceTraits);
  // create swapchain
//  result->resizeSurface(width, height);

  viewer = v;
  surface = result;
  swapChainResizable = true;
  return result;
}

void WindowXcb::registerWindow(xcb_window_t windowID, WindowXcb* window)
{
  std::lock_guard<std::mutex> lock(regMutex);
  registeredWindows.insert({windowID,window});
}

void WindowXcb::unregisterWindow(xcb_window_t windowID)
{
  std::lock_guard<std::mutex> lock(regMutex);
  registeredWindows.erase(windowID);
}

WindowXcb* WindowXcb::getWindow(xcb_window_t windowID)
{
  std::lock_guard<std::mutex> lock(regMutex);
  auto it = registeredWindows.find(windowID);
  if (it == end(registeredWindows))
    return nullptr;
  return it->second;
}

bool WindowXcb::checkWindowMessages()
{
  auto timeNow = HPClock::now();
  xcb_generic_event_t *event;
  while ((event = xcb_poll_for_event(connection)))
  {
    switch (event->response_type & ~0x80)
    {
    case XCB_CLIENT_MESSAGE:
    {
      xcb_client_message_event_t* clientMessage = (xcb_client_message_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(clientMessage->window);
      if (clientMessage->data.data32[0] == window->wmDeleteWin)
      {
        window->viewer.lock()->setTerminate();
        return false;
      }
      break;
    }
    case XCB_MOTION_NOTIFY:
    {
      xcb_motion_notify_event_t* motionNotify = (xcb_motion_notify_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(motionNotify->event);
      if(window!=nullptr)
      {
        float mx = motionNotify->event_x;
        float my = motionNotify->event_y;
        window->normalizeMouseCoordinates(mx,my);
        window->lastMouseX = mx;
        window->lastMouseY = my;
        window->pushInputEvent( InputEvent( timeNow, InputEvent::MOUSE_MOVE, InputEvent::BUTTON_UNDEFINED, window->lastMouseX, window->lastMouseY ) );
      }
      break;
    }
    case XCB_BUTTON_PRESS:
    {
      xcb_button_press_event_t* buttonPress = (xcb_button_press_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(buttonPress->event);
      if(window!=nullptr)
      {
        InputEvent::MouseButton button = InputEvent::BUTTON_UNDEFINED;
        if (buttonPress->detail == XCB_BUTTON_INDEX_1)       button = InputEvent::LEFT;
        else if (buttonPress->detail == XCB_BUTTON_INDEX_2)  button = InputEvent::MIDDLE;
        else if (buttonPress->detail == XCB_BUTTON_INDEX_3)  button = InputEvent::RIGHT;
//        else LOG_ERROR << "Unknown mouse button : 0x" << std::hex << (uint32_t)buttonPress->detail << std::endl;
        window->pushInputEvent( InputEvent( timeNow, InputEvent::MOUSE_KEY_PRESSED, button, window->lastMouseX, window->lastMouseY ) );
      }
      break;
    }
    case XCB_BUTTON_RELEASE:
    {
      xcb_button_press_event_t* buttonPress = (xcb_button_press_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(buttonPress->event);
      if(window!=nullptr)
      {
        InputEvent::MouseButton button = InputEvent::BUTTON_UNDEFINED;
        if (buttonPress->detail == XCB_BUTTON_INDEX_1)       button = InputEvent::LEFT;
        else if (buttonPress->detail == XCB_BUTTON_INDEX_2)  button = InputEvent::MIDDLE;
        else if (buttonPress->detail == XCB_BUTTON_INDEX_3)  button = InputEvent::RIGHT;
//        else LOG_ERROR << "Unknown mouse button : 0x" << std::hex << (uint32_t)buttonPress->detail << std::endl;
        window->pushInputEvent( InputEvent( timeNow, InputEvent::MOUSE_KEY_RELEASED, button, window->lastMouseX, window->lastMouseY ) );
      }
      break;
    }
    case XCB_KEY_PRESS:
    {
      const xcb_key_release_event_t* keyEvent = (const xcb_key_release_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(keyEvent->event);
      if(window!=nullptr)
      {
        InputEvent::Key key = window->xcbKeyCodeToPumex(keyEvent->detail);
        window->pushInputEvent( InputEvent( timeNow, InputEvent::KEYBOARD_KEY_PRESSED, key ) );
      }
      break;
    }
    case XCB_KEY_RELEASE:
    {
      const xcb_key_release_event_t* keyEvent = (const xcb_key_release_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(keyEvent->event);
      if(window!=nullptr)
      {
        InputEvent::Key key = window->xcbKeyCodeToPumex(keyEvent->detail);
        window->pushInputEvent( InputEvent( timeNow, InputEvent::KEYBOARD_KEY_RELEASED, key ) );
      }
      break;
    }
    case XCB_DESTROY_NOTIFY:
    {
      xcb_destroy_notify_event_t* destroyNotify = (xcb_destroy_notify_event_t*)event;
      WindowXcb* window = WindowXcb::getWindow(destroyNotify->window);
      if(window!=nullptr)
      {
        window->viewer.lock()->setTerminate();
        return false;
      }
      break;
    }
    case XCB_CONFIGURE_NOTIFY:
    {
      const xcb_configure_notify_event_t* cfgEvent = (const xcb_configure_notify_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(cfgEvent->window);
      if(window!=nullptr)
      {
        window->resizeCalled        = true;
        window->lastResizeTimePoint = timeNow;
        window->newWidth            = cfgEvent->width;
        window->newHeight           = cfgEvent->height;
      }
      break;
    }
    default:
      break;
    }
    free(event);
  }
  // check for a delayed window resize
  std::lock_guard<std::mutex> lock(regMutex);
  for( auto& win : WindowXcb::registeredWindows )
  {
    if( !win.second->resizeCalled )
      continue;
    // if last resize was called less than 1 second ago, then wait a little more
    if( inSeconds( timeNow - win.second->lastResizeTimePoint ) < 1.0 )
      continue;

    win.second->resizeCalled        = false;
    if( win.second->width == win.second->newWidth && win.second->height == win.second->newHeight  )
      continue;
    win.second->lastResizeTimePoint = timeNow;
    auto surf                      = win.second->surface.lock();
    surf->actions.addAction(std::bind(&Surface::resizeSurface, surf, win.second->newWidth, win.second->newHeight));
    win.second->width               = win.second->newWidth;
    win.second->height              = win.second->newHeight;
  }

  return true;

}

void WindowXcb::normalizeMouseCoordinates(float& x, float& y) const
{
  // x and y are defined in windows coordinates as oposed to Windows OS
  x = x / width;
  y = y / height;
}

InputEvent::Key WindowXcb::xcbKeyCodeToPumex(xcb_keycode_t keycode) const
{
  auto it = xcbKeycodes.find(keycode);
  if(it != end(xcbKeycodes) )
    return it->second;
// Line below is handy for recognizing new keycodes.
//  LOG_ERROR << "Unknown keycode : 0x" << std::hex << (uint32_t)keycode << std::endl;
  return InputEvent::KEY_UNDEFINED;
}

void WindowXcb::fillXcbKeycodes()
{
  // important keys
  xcbKeycodes.insert({0x9, InputEvent::ESCAPE});
  xcbKeycodes.insert({0x41, InputEvent::SPACE});
  xcbKeycodes.insert({0x17, InputEvent::TAB});
  xcbKeycodes.insert({0x32, InputEvent::SHIFT});

  uint32_t i=0;

  // keys F1-F10
  typedef EnumIterator<InputEvent::Key, InputEvent::Key::F1, InputEvent::Key::F10> FunKeyIterator;
  i=0x43;
  for(InputEvent::Key f : FunKeyIterator())
    xcbKeycodes.insert({i++, f});

  // numbers
  typedef EnumIterator<InputEvent::Key, InputEvent::Key::N1, InputEvent::Key::N9> NumKeyIterator;
  i=0xa;
  for(InputEvent::Key f : NumKeyIterator())
    xcbKeycodes.insert({i++, f});
  xcbKeycodes.insert({0x13, InputEvent::N0});

  // letters
  xcbKeycodes.insert({0x26, InputEvent::A});
  xcbKeycodes.insert({0x38, InputEvent::B});
  xcbKeycodes.insert({0x36, InputEvent::C});
  xcbKeycodes.insert({0x28, InputEvent::D});
  xcbKeycodes.insert({0x1a, InputEvent::E});
  xcbKeycodes.insert({0x29, InputEvent::F});
  xcbKeycodes.insert({0x2a, InputEvent::G});
  xcbKeycodes.insert({0x2b, InputEvent::H});
  xcbKeycodes.insert({0x1f, InputEvent::I});
  xcbKeycodes.insert({0x2c, InputEvent::J});
  xcbKeycodes.insert({0x2d, InputEvent::K});
  xcbKeycodes.insert({0x2e, InputEvent::L});
  xcbKeycodes.insert({0x3a, InputEvent::M});
  xcbKeycodes.insert({0x39, InputEvent::N});
  xcbKeycodes.insert({0x20, InputEvent::O});
  xcbKeycodes.insert({0x21, InputEvent::P});
  xcbKeycodes.insert({0x18, InputEvent::Q});
  xcbKeycodes.insert({0x1b, InputEvent::R});
  xcbKeycodes.insert({0x27, InputEvent::S});
  xcbKeycodes.insert({0x1c, InputEvent::T});
  xcbKeycodes.insert({0x1e, InputEvent::U});
  xcbKeycodes.insert({0x37, InputEvent::V});
  xcbKeycodes.insert({0x19, InputEvent::W});
  xcbKeycodes.insert({0x35, InputEvent::X});
  xcbKeycodes.insert({0x1d, InputEvent::Y});
  xcbKeycodes.insert({0x34, InputEvent::Z});
}
