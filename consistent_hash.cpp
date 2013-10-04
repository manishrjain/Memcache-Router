#include "utils.h"
#include <iostream>
#include <openssl/md5.h>

#include "consistent_hash.h"

const int kPointsPerServer = 160;
const int kAlignment = 4;

// TODO(manish): These defaults and this algorithm doesn't match with hosts.cc
// from libmemcached. This logic is based upon ketama.py. Revisit later.
ConsistentHash::ConsistentHash(
    const RepeatedPtrField<memcache_router::Server>& servers) {
  for (int i = 0; i < servers.size(); ++i) {
    Resource resource;
    resource.server.CopyFrom(servers.Get(i));

    for (int j = 0; j < kPointsPerServer / kAlignment; ++j) {
      string resource_key = resource.GetKeyForPoint(j);

      for (int alignment = 0; alignment < kAlignment; ++alignment) {
        uint32_t pos = GetKetamaHash(resource_key, alignment);
        CHECK(resources_.insert(pair<uint32_t, Resource>(
            pos, resource)).second);
      }
    }
  }
}

const memcache_router::Server&
    ConsistentHash::ServerForHash(uint32_t hash) const {
  auto itr = resources_.lower_bound(hash);
  if (itr == resources_.end()) {
    itr = resources_.begin();
  }
  return itr->second.server;
}

uint32_t ConsistentHash::GetKetamaHash(const string& key, int alignment) const {
  unsigned char digest[MD5_DIGEST_LENGTH];
  MD5((const unsigned char*) key.c_str(), key.size(), digest);

  return ((uint32_t) (digest[3 + alignment * 4] & 0xFF) << 24)
    | ((uint32_t) (digest[2 + alignment * 4] & 0xFF) << 16)
    | ((uint32_t) (digest[1 + alignment * 4] & 0xFF) << 8)
    | (digest[0 + alignment * 4] & 0xFF);
}

int main() {
  memcache_router::Instruction instruction;
  for (int i = 0; i < 5; ++i) {
    memcache_router::Server* s = instruction.add_servers();
    char host = 'a' + i;
    s->set_hostname(&host);
    s->set_port(1);
  }

  ConsistentHash h(instruction.servers());
  // The following 3 are checking against ketama python implementation ketama.py
  CHECK(h.GetKetamaHash("manish", 0) == 2303838553);
  CHECK(h.GetKetamaHash("rai", 0) == 4198501049);
  CHECK(h.GetKetamaHash("jain", 0) == 901215935);
  cout << "Ketama hash OK" << endl;


  CHECK(h.ServerForKey("a").hostname() == "e");
  CHECK(h.ServerForKey("ab").hostname() == "b");
  CHECK(h.ServerForKey("abc").hostname() == "c");
  CHECK(h.ServerForKey("abcd").hostname() == "e");
  CHECK(h.ServerForKey("abcde").hostname() == "c");
  cout << "Server for key OK" << endl;
  return 0;
}
