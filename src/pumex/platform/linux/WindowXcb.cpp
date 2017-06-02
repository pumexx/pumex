#include <pumex/platform/linux/WindowXcb.h>
#include <cstring>
#include <pumex/Viewer.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>

using namespace pumex;

xcb_connection_t*                            WindowXcb::connection        = nullptr;
const xcb_setup_t*                           WindowXcb::connectionSetup   = nullptr;
std::unordered_map<xcb_window_t, WindowXcb*> WindowXcb::registeredWindows;

WindowXcb::WindowXcb(const WindowTraits& windowTraits)
{
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
  
  xcb_create_window( connection, XCB_COPY_FROM_PARENT, window, screen->root, windowTraits.x, windowTraits.y, windowTraits.w, windowTraits.h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, eventMask, valueList);  
  xcb_change_property( connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, windowTraits.windowName.size(),   windowTraits.windowName.c_str());  
  
  /* Magic code that will send notification when window is destroyed */
//  xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
//  atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");
//  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, (*reply).atom, 4, 32, 1, &(*atom_wm_delete_window).atom);

  registerWindow(window, this);
  
  xcb_map_window(connection, window); 
}

WindowXcb::~WindowXcb()
{
  unregisterWindow(window);
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
