#ifndef MEMCACHE_ROUTER_CACHE_H
#define MEMCACHE_ROUTER_CACHE_H

/*
 * This is a thread safe LRU cache designed for multi-threaded access.
 *
 * NOTE: The following benchmarks were run on DEV box, which is a
 * SINGLE virtual core.
 * Production servers with 16 cores should yield different (better) results.
 *
 * Benchmark ran through benchmark_lru_cache.cpp
 * Note that key and val = prefix + string(int)
 * The bytes reported here are of prefix. string(int) is ignored.
 *
 * With 0 bytes key, and 0 bytes value (8 threads, 64 buckets):
 * 1M GETs done in usecs: 3160969 Avg us: 3.16097
 * 1M GETs done in usecs: 3091845 Avg us: 3.09185
 * 1M GETs done in usecs: 3042467 Avg us: 3.04247
 * 1M GETs done in usecs: 3039191 Avg us: 3.03919
 * 1M GETs done in usecs: 3271288 Avg us: 3.27129
 * 1M GETs done in usecs: 3447241 Avg us: 3.44724
 * 1M GETs done in usecs: 3507735 Avg us: 3.50773
 * 1M GETs done in usecs: 3517117 Avg us: 3.51712
 * 1M GETs done in usecs: 3199754 Avg us: 3.19975
 * 1M GETs done in usecs: 3408660 Avg us: 3.40866
 * 1M GETs done in usecs: 3175657 Avg us: 3.17566
 * 1M GETs done in usecs: 3303019 Avg us: 3.30302
 *
 * With 50 bytes key, and 1024 bytes value (8 threads, 64 buckets):
 * 1M GETs done in usecs: 9822841 Avg us: 9.82284
 * 1M GETs done in usecs: 9416186 Avg us: 9.41619
 * 1M GETs done in usecs: 9830444 Avg us: 9.83044
 * 1M GETs done in usecs: 9795482 Avg us: 9.79548
 * 1M GETs done in usecs: 9570298 Avg us: 9.5703
 * 1M GETs done in usecs: 9273941 Avg us: 9.27394
 * 1M GETs done in usecs: 9884296 Avg us: 9.8843
 * 1M GETs done in usecs: 9018078 Avg us: 9.01808
 * 1M GETs done in usecs: 9074221 Avg us: 9.07422
 * 1M GETs done in usecs: 9606789 Avg us: 9.60679
 * 1M GETs done in usecs: 9351699 Avg us: 9.3517
 * 1M GETs done in usecs: 9353338 Avg us: 9.35334
 * 1M GETs done in usecs: 9090081 Avg us: 9.09008
 * 1M GETs done in usecs: 9051074 Avg us: 9.05107
 * 25165824 done in seconds: 29 at throughput of 867787 per sec
 * 
 * With 50 bytes key, and 1024 bytes value (1 thread, 1 bucket):
 * 1M GETs done in usecs: 1873140 Avg us: 1.87314
 * 1M GETs done in usecs: 1868651 Avg us: 1.86865
 * 1M GETs done in usecs: 1869470 Avg us: 1.86947
 * 1M GETs done in usecs: 1867943 Avg us: 1.86794
 * 1M GETs done in usecs: 1876563 Avg us: 1.87656
 * 1M GETs done in usecs: 1873022 Avg us: 1.87302
 * 1M GETs done in usecs: 1867000 Avg us: 1.867
 * 1M GETs done in usecs: 1866528 Avg us: 1.86653
 * 1M GETs done in usecs: 1865553 Avg us: 1.86555
 * 1M GETs done in usecs: 1867219 Avg us: 1.86722
 * 25165824 done in seconds: 47 at throughput of 535443 per sec
 *
 * The following set up is close to what we have for Redis.
 * With 50 bytes key, and 1024 bytes value (24 threads, 1 bucket):
 * 1M GETs done in usecs: 49991728 Avg us: 49.9917
 * 1M GETs done in usecs: 50185525 Avg us: 50.1855
 * 1M GETs done in usecs: 50284454 Avg us: 50.2845
 * 1M GETs done in usecs: 50957539 Avg us: 50.9575
 * 1M GETs done in usecs: 51059132 Avg us: 51.0591
 * 1M GETs done in usecs: 51213574 Avg us: 51.2136
 * 1M GETs done in usecs: 51619008 Avg us: 51.619
 * 1M GETs done in usecs: 51725335 Avg us: 51.7253
 * 1M GETs done in usecs: 51772765 Avg us: 51.7728
 * 1M GETs done in usecs: 52229630 Avg us: 52.2296
 * 25165824 done in seconds: 52 at throughput of 483958 per sec
 *
 * ==============
 *
 * UPDATE from testbox2 (8 cores):
 * With 50 bytes key, and 1024 bytes value (8 threads, 64 buckets):
 * 1M GETs done in usecs: 1924808 Avg us: 1.92481
 * 1M GETs done in usecs: 1911369 Avg us: 1.91137
 * 1M GETs done in usecs: 1950417 Avg us: 1.95042
 * 1M GETs done in usecs: 2008319 Avg us: 2.00832
 * 1M GETs done in usecs: 1668306 Avg us: 1.66831
 * 1M GETs done in usecs: 1801276 Avg us: 1.80128
 * 1M GETs done in usecs: 1790689 Avg us: 1.79069
 * 25165824 done in seconds: 5 at throughput of 5033164 per sec
 */

#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "memdata.pb.h"
using namespace std;

const int kNumBuckets = 64;
struct Data {
  string key;
  string value;
  uint32_t flags;
  uint64_t cas;

  int Used() const {
    return key.size() + value.size() + sizeof(uint32_t) + sizeof(uint64_t);
  }

};
typedef list<Data*> List;
typedef List::iterator Itr;
typedef unordered_map<string, Itr> Map;

struct Bucket {
  Bucket() : memory(0) {}
  ~Bucket() {
    for (Itr itr = access_list.begin(); itr != access_list.end(); ++itr) {
      delete *itr;
    }
  }

  mutable mutex m;
  Map key_to_litr; // Map stores (key, List Iterator) pairs. 

  // Every time Data is accessed, its moved to the back of list.
  // Hence, the front of this list always has stale entries.
  List access_list;
  // Total memory used by bucket.
  uint64_t memory;
};

class Cache {
 public:
  explicit Cache(uint64_t capacity);
  ~Cache();

  void AddOrReplace(const string& k, const memcache_router::KeyValue& kv);
  bool Get(const string& k, memcache_router::KeyValue* kv);
  void PopulateStats(memcache_router::Stats* stats) {
    stats->mutable_cache_hit()->set_count(hits_);
    stats->mutable_cache_miss()->set_count(miss_);
  }

 private:
  int GetIndex(const string& k) {
    size_t h = str_hash(k);
    return h % kNumBuckets;
  }
  
  Data* FindOrInsertData(Bucket* bucket, const string& k);
  void DeleteStaleData(Bucket* bucket, uint64_t decrease_by);

  atomic_ullong hits_;
  atomic_ullong miss_;
  hash<string> str_hash;
  uint64_t decrease_by_;
  uint64_t threshold_;
  vector<Bucket*> buckets_;
};

#endif

