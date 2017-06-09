#pragma once
#include <pumex/Export.h>
#include <thread>
#include <vector>
#include <memory>

namespace pumex
{
// std::thread wrapper ( because std::thread can't be held by pointer in a vector ? )
class PUMEX_EXPORT Thread
{
public:
  explicit Thread()                = default;
  Thread(const Thread&)            = delete;
  Thread& operator=(const Thread&) = delete;
  virtual ~Thread()
  {}

  std::thread thr;
  virtual void run() = 0;
  virtual void cleanup() = 0;
};

class PUMEX_EXPORT ThreadJoiner
{
public:
  std::vector<Thread*> threads;

  explicit ThreadJoiner()
  {
  }

  void addThread(Thread* thread);
  ~ThreadJoiner();
};
}