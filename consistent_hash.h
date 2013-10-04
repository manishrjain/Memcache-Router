#include <string>
#include <sstream>

#include "memdata.pb.h"
using namespace std;
using google::protobuf::RepeatedPtrField;

struct Resource {
  Resource() : position(-1) {}

  memcache_router::Server server;
  int position;

  string GetKeyForPoint(int point) {
    stringstream ss;
    ss << server.hostname();
    ss << "-";
    ss << point;
    return ss.str();
  }
};

class ConsistentHash {
 public:
  explicit ConsistentHash(
      const RepeatedPtrField<memcache_router::Server>& servers);

  uint32_t GetKetamaHash(const string& key, int alignment) const;
  const memcache_router::Server& ServerForHash(uint32_t hash) const;

  const memcache_router::Server& ServerForKey(const string& key) const {
    uint32_t hash = GetKetamaHash(key, 0);
    return ServerForHash(hash);
  }

 private:
  map<uint32_t, Resource> resources_;
};

