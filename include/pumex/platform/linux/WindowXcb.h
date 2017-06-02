#pragma once

#include <unordered_map>
#include <pumex/Window.h>
#include <xcb/xcb.h>

namespace pumex
{
    
class WindowXcb : public Window, public std::enable_shared_from_this<WindowXcb>
{
public:
  explicit WindowXcb(const WindowTraits& windowTraits);
  WindowXcb(const WindowXcb&)            = delete;
  WindowXcb& operator=(const WindowXcb&) = delete;
  virtual ~WindowXcb();
  
  static void registerWindow(xcb_window_t windowID, WindowXcb* window);
  static void unregisterWindow(xcb_window_t windowID);
  static WindowXcb* getWindow(xcb_window_t windowID);
  
  std::shared_ptr<pumex::Surface> createSurface(std::shared_ptr<pumex::Viewer> viewer, std::shared_ptr<pumex::Device> device, const pumex::SurfaceTraits& surfaceTraits) override;

protected:
  xcb_screen_t* screen      = nullptr;
  xcb_window_t  window      = 0;
  xcb_atom_t    wmProtocols;
  xcb_atom_t    wmDeleteWin;  
//  xcb_intern_atom_reply_t* atom_wm_delete_window = nullptr;
  
  static xcb_connection_t*                            connection;
  static const xcb_setup_t*                           connectionSetup;
  static std::unordered_map<xcb_window_t, WindowXcb*> registeredWindows;
  
  
};

}
