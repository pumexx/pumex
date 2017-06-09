#include <pumex/Thread.h>

using namespace pumex;

void ThreadJoiner::addThread(Thread* th)
{
  threads.push_back( th );
  std::thread t([th]{ th->run();}); // awful lambda...
  th->thr = std::move(t);
}

ThreadJoiner::~ThreadJoiner()
{
  for (auto t : threads)
  {
    if (t->thr.joinable())
      t->thr.join();
  }
}
