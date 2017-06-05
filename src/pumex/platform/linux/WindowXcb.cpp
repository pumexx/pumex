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
  
  if (windowTraits.fullscreen)
    xcb_create_window( connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, screen->width_in_pixels, screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, eventMask, valueList);  
  else
    xcb_create_window( connection, XCB_COPY_FROM_PARENT, window, screen->root, windowTraits.x, windowTraits.y, windowTraits.w, windowTraits.h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, eventMask, valueList);  
  xcb_change_property( connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, windowTraits.windowName.size(),   windowTraits.windowName.c_str());  
  
  xcb_intern_atom_cookie_t wmDeleteCookie    = xcb_intern_atom(connection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
  xcb_intern_atom_cookie_t wmProtocolsCookie = xcb_intern_atom(connection, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
  xcb_intern_atom_reply_t* wmDeleteReply     = xcb_intern_atom_reply(connection, wmDeleteCookie, NULL);
  xcb_intern_atom_reply_t* wmProtocolsReply  = xcb_intern_atom_reply(connection, wmProtocolsCookie, NULL);
  wmDeleteWin = wmDeleteReply->atom;
  wmProtocols = wmProtocolsReply->atom;
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, wmProtocolsReply->atom, 4, 32, 1, &wmDeleteReply->atom);
  
  registerWindow(window, this);
  
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
  
  if (windowTraits.fullscreen)
  {
    xcb_intern_atom_cookie_t cookieWms = xcb_intern_atom(connection, false, strlen("_NET_WM_STATE"), "_NET_WM_STATE");
    xcb_intern_atom_reply_t* atomWms   = xcb_intern_atom_reply(connection, cookieWms, NULL);
    
    xcb_intern_atom_cookie_t cookieFs  = xcb_intern_atom(connection, false, strlen("_NET_WM_STATE_FULLSCREEN"), "_NET_WM_STATE_FULLSCREEN");
    xcb_intern_atom_reply_t* atomFs = xcb_intern_atom_reply(connection, cookieFs, NULL);
    
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, atomWms->atom, XCB_ATOM_ATOM, 32, 1, &(atomFs->atom));
    free(atomFs);
    free(atomWms);
  }
  
  xcb_map_window(connection, window); 
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

std::shared_ptr<pumex::Surface> WindowXcb::createSurface(std::shared_ptr<pumex::Viewer> v, std::shared_ptr<pumex::Device> device, const pumex::SurfaceTraits& surfaceTraits)
{
  VkSurfaceKHR vkSurface;
  VkXcbSurfaceCreateInfoKHR surfaceCreateInfo{};
    surfaceCreateInfo.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.connection = connection;
    surfaceCreateInfo.window     = window;
  VK_CHECK_LOG_THROW(vkCreateXcbSurfaceKHR(v->getInstance(), &surfaceCreateInfo, nullptr, &vkSurface), "Could not create surface");

  std::shared_ptr<pumex::Surface> result = std::make_shared<pumex::Surface>(v, shared_from_this(), device, vkSurface, surfaceTraits);
  // create swapchain
  result->resizeSurface(width, height);

  viewer = v;
  surface = result;
  swapChainResizable = true; 
  return result;
}

void WindowXcb::registerWindow(xcb_window_t windowID, WindowXcb* window)
{
  registeredWindows.insert({windowID,window});
}

void WindowXcb::unregisterWindow(xcb_window_t windowID)
{
  registeredWindows.erase(windowID);
}

WindowXcb* WindowXcb::getWindow(xcb_window_t windowID)
{
  auto it = registeredWindows.find(windowID);
  if (it == registeredWindows.end())
    return nullptr;
  return it->second;
}

bool WindowXcb::checkWindowMessages()
{
  auto timeNow = pumex::HPClock::now();
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
      xcb_client_message_event_t* clientMessage = (xcb_client_message_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(clientMessage->window);
      if(window!=nullptr)
      {
        if (clientMessage->data.data32[0] == window->wmDeleteWin)
        {
          window->viewer.lock()->setTerminate();
          return false;
        }
      }
      break;
    }
    case XCB_CONFIGURE_NOTIFY:
    {
      // FIXME : currently Linux calls this event many times during single mouse resize and
      // such situation generates A LOT of memory reallocations.
      // On Windows this problem is solved using WM_EXITSIZEMOVE.
      const xcb_configure_notify_event_t* cfgEvent = (const xcb_configure_notify_event_t *)event;
      WindowXcb* window = WindowXcb::getWindow(cfgEvent->window);
      if(window!=nullptr)
      {
        window->newWidth  = cfgEvent->width;
        window->newHeight = cfgEvent->height;
        auto surf = window->surface.lock();
        surf->actions.addAction(std::bind(&pumex::Surface::resizeSurface, surf, window->newWidth, window->newHeight));
        window->width = window->newWidth;
        window->height = window->newHeight;
      }
      break;
    }
    default:
      break;
    }
    free(event);
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
  if(it != xcbKeycodes.end() )
    return it->second;
//  LOG_ERROR << "Unknown keycode : 0x" << std::hex << (uint32_t)keycode << std::endl;
  return InputEvent::KEY_UNDEFINED;
}

void WindowXcb::fillXcbKeycodes()
{
  // important keys
  xcbKeycodes.insert({0x9, InputEvent::ESCAPE});
  xcbKeycodes.insert({0x41, InputEvent::SPACE});
  xcbKeycodes.insert({0x17, InputEvent::TAB});
  
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
