#pragma once

#include <vector>
#include <functional>
#include <mutex>

namespace pumex
{
  // Handy class that may transfer actions between threads
  class ActionQueue
  {
  public:
    explicit ActionQueue()                     = default;
    ActionQueue(const ActionQueue&)            = delete;
    ActionQueue& operator=(const ActionQueue&) = delete;

    void addAction(const std::function<void(void)>& fun)
    {
      std::lock_guard<std::mutex> lock(mutex);
      actions.push_back(fun);
    }
    void performActions()
    {
      std::vector<std::function<void(void)>> actionCopy;
      {
        std::lock_guard<std::mutex> lock(mutex);
        actionCopy = actions;
        actions.resize(0);
      }
      for (auto a : actionCopy)
        a();
    }
  private:
	  std::vector<std::function<void(void)>> actions;
    mutable std::mutex mutex;
  };
	
}