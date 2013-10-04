#include "lru_cache.h"

#include <algorithm>

Cache::Cache(uint64_t capacity) : hits_(0), miss_(0) {
  threshold_ = max(capacity / kNumBuckets, static_cast<uint64_t>(10 << 20));
  decrease_by_ = max(static_cast<uint64_t>(threshold_ * 0.01),
                     static_cast<uint64_t>(100 << 10));  // ~1%

  for (int i = 0; i < kNumBuckets; ++i) {
    buckets_.push_back(new Bucket);
  }
}

Cache::~Cache() {
  for (int i = 0; i < kNumBuckets; ++i) {
    delete buckets_[i];
  }
}

// This function should already have lock acquired.
Data* Cache::FindOrInsertData(Bucket* bucket, const string& k) {
  Map::iterator itr = bucket->key_to_litr.find(k);
  if (itr != bucket->key_to_litr.end()) {
    // Move to end.
    bucket->access_list.splice(bucket->access_list.end(),
                               bucket->access_list,
                               itr->second);
    Itr back = --bucket->access_list.end();
    itr->second = back;  // Store new location in map.
    return *back;
  }

  Data* d = new Data;
  bucket->access_list.push_back(d);
  bucket->key_to_litr[k] = --bucket->access_list.end();
  return d;
}

void Cache::AddOrReplace(const string& k, const memcache_router::KeyValue& kv) {
  Bucket* bucket = buckets_[GetIndex(k)];
  lock_guard<mutex> l(bucket->m);

  if (bucket->memory > threshold_) {
    DeleteStaleData(bucket, decrease_by_);
  }

  Data* data = FindOrInsertData(bucket, k);
  bucket->memory -= data->Used();
  data->key = k;
  data->value = kv.val();
  data->flags = kv.flags();
  data->cas = kv.cas();
  bucket->memory += data->Used();
}

// This function should already have lock acquired.
void Cache::DeleteStaleData(Bucket* bucket, uint64_t decrease_by) {
  uint64_t target = bucket->memory - decrease_by;
  Itr itr = bucket->access_list.begin();
  while (itr != bucket->access_list.end()) {
    Data* data = *itr;
    itr = bucket->access_list.erase(itr);
    bucket->key_to_litr.erase(data->key);
    bucket->memory -= data->Used();
    delete data;
    if (bucket->memory < target)
      break;
  }
}

bool Cache::Get(const string& k, memcache_router::KeyValue* kv) {
  Bucket* bucket = buckets_[GetIndex(k)];
  lock_guard<mutex> l(bucket->m);
  Data* data = FindOrInsertData(bucket, k);

  if (data->value.empty()) {
    ++miss_;
    return false;
  }
  ++hits_;
  kv->set_val(data->value);
  kv->set_flags(data->flags);
  kv->set_cas(data->cas);
  return true;
}

