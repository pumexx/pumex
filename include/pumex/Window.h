#pragma once

#include <memory>
#include <vector>
#include <string>
#include <array>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/HPClock.h>

namespace pumex
{

class Viewer;
class Device;
class Surface;
struct SurfaceTraits;

// struct holding all information required to create a window
struct PUMEX_EXPORT WindowTraits
{
  WindowTraits(uint32_t screenNum, uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool fullscreen, const std::string& windowName);

  uint32_t screenNum = 0;
  uint32_t x         = 0;
  uint32_t y         = 0;
  uint32_t w         = 1;
  uint32_t h         = 1;
  bool fullscreen    = false;
  std::string windowName;
};

// Class holding a single mouse input event
struct MouseEvent
{
  enum Type { MOVE, KEY_PRESSED, KEY_RELEASED, KEY_DOUBLE_PRESSED };
  enum Button { NONE, LEFT, MIDDLE, RIGHT };
  MouseEvent(Type mt, Button b, float mx, float my, pumex::HPClock::time_point t)
    : type{ mt }, button{ b }, x{ mx }, y{ my }, time{ t }
  {
  }
  Type type;
  Button button;
  float x;
  float y;
  pumex::HPClock::time_point time;
};

// Abstract base class representing a system window. Window is associated to a surface ( 1 to 1 association )
class PUMEX_EXPORT Window
{
public:
  Window()                         = default;
  Window(const Window&)            = delete;
  Window& operator=(const Window&) = delete;
  virtual ~Window()
  {}

  static std::shared_ptr<Window> createWindow(const WindowTraits& windowTraits);

  virtual std::shared_ptr<pumex::Surface> createSurface(std::shared_ptr<pumex::Viewer> viewer, std::shared_ptr<pumex::Device> device, const pumex::SurfaceTraits& surfaceTraits) = 0;
  uint32_t width     = 1;
  uint32_t height    = 1;
  uint32_t newWidth  = 1;
  uint32_t newHeight = 1;

  // temporary solution for keyboard input ( Windows dependent, need to be reconsidered )
  void setKeyState(const std::array<uint8_t, 256>& newKeyState);
  bool isKeyPressed(uint8_t key) const;

  // temporary solution for mouse input
  void pushMouseEvent( const MouseEvent& event );
  std::vector<MouseEvent> getMouseEvents();
protected:
  std::weak_ptr<pumex::Viewer>  viewer;
  std::weak_ptr<pumex::Surface> surface;

  mutable std::mutex inputMutex;
  std::array<uint8_t, 256> keyState;
  std::vector<MouseEvent> mouseEvents;

};

}