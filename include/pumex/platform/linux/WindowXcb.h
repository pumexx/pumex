#pragma once

#include <pumex/Window.h>

namespace pumex
{
  class WindowXcb : public Window
  {
    public:
    WindowXcb(const WindowTraits& windowTraits);
  };
}
