#include "lru_cache.h"
#include "memdata.pb.h"
#include "utils.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
using namespace std;

struct ThreadPool {
  ~ThreadPool() {
    // Block until all threads finish.
    for (int i = 0; i < threads.size(); ++i) {
      if (threads[i].joinable())
        threads[i].join();
    }
  }
  vector<thread> threads;
};

static const int NUM_THREADS = 8;
static const int kTrials = 3 << 20;
static const int kMod = 10000;
static const int kKeySize = 50;
static const int kValSize = 1024;

class CacheLoadtest {
 public:
  CacheLoadtest() {
    cache_ = new Cache(2 << 30);
    string key_fill = string(kKeySize, 'k');
    string val_fill = string(kValSize, 'v');
    for (int i = 0; i < kMod; ++i) {
      stringstream ss;
      ss << i;
      string key = key_fill + ss.str();
      string val = val_fill + ss.str();

      memcache_router::KeyValue kv;
      kv.set_key(key);
      kv.set_val(val);
      data.push_back(pair<string, string>(key, val));
      cache_->AddOrReplace(key, kv);
    }

    start_ = chrono::high_resolution_clock::now();
    thread_pool_ = new ThreadPool;
    for (int i = 0; i < NUM_THREADS; ++i) {
      thread_pool_->threads.push_back(thread(&CacheLoadtest::QueryCache, this));
    }
  }

  void Wait() {
    delete thread_pool_;
    auto end = chrono::high_resolution_clock::now();
    int dur = chrono::duration_cast<chrono::seconds>(end - start_).count();
    int count = NUM_THREADS * kTrials;
    cout << count << " done in seconds: " << dur
         << " at throughput of " << count / dur << " per sec" << endl;
  }

 private:
  void QueryCache() {
    int num = 0;
    memcache_router::KeyValue kv;
    chrono::high_resolution_clock::time_point start =
        chrono::high_resolution_clock::now();
    for (int i = 1; i <= kTrials; ++i) {
      num = i % kMod;
      cache_->Get(data[num].first, &kv);
      CHECK(data[num].second == kv.val());
      if (i % 1000000 == 0) {
        auto end = chrono::high_resolution_clock::now();
        int duration = chrono::duration_cast<chrono::microseconds>(
            end - start).count();
        cout << "1M GETs done in usecs: " << duration
             << " Avg us: " << duration / 1000000.0 << endl;
        start = end;
      }
    }
  }

  Cache* cache_;
  ThreadPool* thread_pool_;
  chrono::high_resolution_clock::time_point start_;
  vector<pair<string, string> > data;
};

int main() {
  CacheLoadtest test;
  test.Wait();
  return 0;
}
