#include <libmemcached/memcached.h>

#include "lru_cache.h"
#include "memdata.pb.h"

using namespace std;

// This class is not thread safe.
class MemClient {
 public:
  explicit MemClient(Cache* cache);
  ~MemClient();
  void Init(const memcache_router::Instruction& instruction);
  void GetKeys(map<string, memcache_router::KeyValue>* key_to_kvalp);
  void SetKeys(memcache_router::Instruction* instruction);
  void IncrKeys(memcache_router::Instruction* instruction);

 private:
  Cache* cache_;  // not owned here.
  memcached_server_st* servers_;
  memcached_st* memc_;
};
