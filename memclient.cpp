// Author: Manish Jain (manish@quora.com)
// Implementation of MemClient.

#include <iostream>
#include "utils.h"
using namespace std;

#include "memclient.h"

MemClient::MemClient(Cache* cache) : cache_(cache) {
  memc_ = memcached_create(NULL);
}

MemClient::~MemClient() {
  memcached_server_free(servers_);
  memcached_free(memc_);
}

void MemClient::Init(const memcache_router::Instruction& instruction) {
  memcached_return_t rc;

  rc = memcached_behavior_set(memc_, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);
  CHECK(rc == MEMCACHED_SUCCESS);
  rc = memcached_behavior_set(memc_, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 1);
  CHECK(rc == MEMCACHED_SUCCESS);
  // rc = memcached_behavior_set(memc_, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
  // CHECK(rc == MEMCACHED_SUCCESS);
  // Don't set MEMCACHED_BEHAVIOR_NOREPLY as it causes increment
  // and decrement operations to not return any value.

  CHECK(instruction.servers_size() > 0);
  servers_ = memcached_server_list_append(NULL, instruction.servers(0).hostname().c_str(),
                                          instruction.servers(0).port(), &rc);
  CHECK(rc == MEMCACHED_SUCCESS);

  for (int i = 1; i < instruction.servers_size(); ++i) {
    servers_ = memcached_server_list_append(servers_, instruction.servers(0).hostname().c_str(),
                                            instruction.servers(0).port(), &rc);
    CHECK(rc == MEMCACHED_SUCCESS);
  }
  rc = memcached_server_push(memc_, servers_);
  CHECK(rc == MEMCACHED_SUCCESS);
}

void MemClient::GetKeys(map<string, memcache_router::KeyValue>* key_to_kvalp) {
  char** keys = new char* [key_to_kvalp->size()];
  size_t key_length[key_to_kvalp->size()];
  int num_keys = 0;

  int i = 0;
  bool fetch_from_memcached = false;
  for (auto itr = key_to_kvalp->begin(); itr != key_to_kvalp->end(); ++itr) {
    memcache_router::KeyValue& kv = itr->second;
    if (cache_ && cache_->Get(itr->first, &kv)) {
      // Filled from cache, no need to send to server.
      continue;
    }

    fetch_from_memcached = true;
    ++num_keys;
    keys[i] = new char[itr->first.length() + 1];
    strcpy(keys[i], itr->first.c_str());
    key_length[i] = itr->first.length();
    ++i;
  }

  // If all the keys are served from cache, there's no need for a round trip
  // to the memcached servers.
  if (!fetch_from_memcached)
    return;

  memcached_return_t rc = memcached_mget(memc_, keys, key_length, num_keys);
  if (rc != MEMCACHED_SUCCESS) {
    cerr << "return code: " << rc << endl;
    CHECK(rc == MEMCACHED_SUCCESS);
  }

  // Should use memcached_fetch_result instead.
  // And use memcached_result_cas with the result to find cas id.
  memcached_result_st* result = NULL;
  while (result = memcached_fetch_result(memc_, NULL, &rc)) {
    string key(memcached_result_key_value(result),
               memcached_result_key_length(result));
    auto itr = key_to_kvalp->find(key);
    CHECK(itr != key_to_kvalp->end());

    memcache_router::KeyValue& kv = itr->second;
    kv.set_val(memcached_result_value(result),
               memcached_result_length(result));
    kv.set_flags(memcached_result_flags(result));
    kv.set_cas(memcached_result_cas(result));
    kv.set_return_code(rc);
    kv.set_return_error(memcached_strerror(memc_, rc));
    free(result);

    if (cache_)
      cache_->AddOrReplace(key, kv);
  }
}

void MemClient::SetKeys(memcache_router::Instruction* instruction) {
  // TODO(manish): Find a way to send a single RPC for setting multiple keys.
  for (int i = 0; i < instruction->set_keys_size(); ++i)  {
    memcache_router::KeyValue* kv = instruction->mutable_set_keys(i);
    if (cache_)
      cache_->AddOrReplace(kv->key(), *kv);

    if (kv->no_propagate()) {
      // don't forward to memcached servers.
      continue;
    }

    memcached_return_t rc;
    if (kv->cas() > 0) {
      rc = memcached_cas(
          memc_, kv->key().c_str(), kv->key().size(),
          kv->val().c_str(), kv->val().size(),
          (time_t) kv->expire_in_seconds(), kv->flags(), kv->cas());

    } else if (!kv->allow_replace()) {
      rc = memcached_add(
          memc_, kv->key().c_str(), kv->key().size(),
          kv->val().c_str(), kv->val().size(),
          (time_t) kv->expire_in_seconds(), kv->flags());

    } else {
      rc = memcached_set(
          memc_, kv->key().c_str(), kv->key().size(),
          kv->val().c_str(), kv->val().size(),
          (time_t) kv->expire_in_seconds(), kv->flags());
    }
    kv->set_return_code(rc);
    kv->set_return_error(memcached_strerror(memc_, rc));
  }
}

void MemClient::IncrKeys(memcache_router::Instruction* instruction) {
  for (int i = 0; i < instruction->incr_keys_size(); ++i) {
    memcache_router::KeyValue* kv = instruction->mutable_incr_keys(i);
    // No application of cache for this method for now.

    uint64_t val = 0;
    memcached_return_t rc;
    if (kv->offset() >= 0) {
      // increment.
      if (kv->has_default_counter_val()) {
        rc = memcached_increment_with_initial(
            memc_, kv->key().c_str(), kv->key().size(),
            kv->offset(), kv->default_counter_val(),
            (time_t)kv->expire_in_seconds(), &val);
      } else {
        rc = memcached_increment(
            memc_, kv->key().c_str(), kv->key().size(),
            kv->offset(), &val);
      }

    } else {
      // decrement.
      unsigned int offset = abs(kv->offset());
      if (kv->has_default_counter_val()) {
        rc = memcached_decrement_with_initial(
            memc_, kv->key().c_str(), kv->key().size(),
            offset, kv->default_counter_val(),
            (time_t)kv->expire_in_seconds(), &val);
      } else {
        rc = memcached_decrement(
            memc_, kv->key().c_str(), kv->key().size(),
            offset, &val);
      }
    }
    kv->set_counter_val(val);
    kv->set_return_code(rc);
    kv->set_return_error(memcached_strerror(memc_, rc));
  }
}

