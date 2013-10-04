#ifndef MEMCACHE_ROUTER_UTILS_H
#define MEMCACHE_ROUTER_UTILS_H

#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "memdata.pb.h"
using namespace std;

#define CHECK(condition) \
{ \
  if (!(condition)) { \
    cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__; \
    cerr << " fn: " << __FUNCTION__; \
    cerr << " Condition: " << #condition << endl; \
    abort(); \
  } \
}

namespace router_utils {

void SendHelper(void* worker, const string& data, int flags);

// This class is not thread safe.
struct Timer {
  Timer() {
    start = chrono::high_resolution_clock::now();
  }

  int GetDelay() const {
    auto end = chrono::high_resolution_clock::now();
    return chrono::duration_cast<chrono::microseconds>(end - start).count();
  }
  chrono::high_resolution_clock::time_point start;
};

// This class is not thread safe, and needs external locking mechanism.
struct Stats {
  Stats() : counter(0), value(0) {}

  void Reset() {
    counter = 0;
    value = 0;
  }

  void Increment(int x) {
    value += x;
    ++counter;
  }

  double Average() const {
    if (counter == 0)
      return 0;
    return static_cast<double>(value) / counter;
  }

  uint64_t counter;
  uint64_t value;
};

class ThreadSafeStats {
 public:
  void Merge(const Stats& s) {
    lock_guard<mutex> lk(m_);
    stats_.counter += s.counter;
    stats_.value += s.value;
  }

  void Set(memcache_router::Breakdown* breakdown) const {
    lock_guard<mutex> lk(m_);
    breakdown->set_average(stats_.Average());
    breakdown->set_count(stats_.counter);
  }

 private:
  mutable mutex m_;
  Stats stats_;
};

struct ThreadPool {
  void Reset() {
    // Block until all threads finish.
    for (int i = 0; i < threads.size(); ++i) {
      if (threads[i].joinable())
        threads[i].join();
    }
    threads.clear();
  }
  vector<thread> threads;
};

}  // namespace router_utils

#endif

