#ifndef MEMCACHE_ROUTER_CMRCLIENT_H
#define MEMCACHE_ROUTER_CMRCLIENT_H

#include <Python.h>  // This has to be the FIRST library
                     // included by Python rules.

#include <string>
#include <vector>

#include "memdata.pb.h"
#include "utils.h"
using namespace std;

class Client {
 public:
  explicit Client(const string& id);
  ~Client();

  // The method names are written in Python style.
  void reset_hosts();
  void add_host(const string& hostname, int port);
  void send_host_list();

  PyObject* get(const string& key);
  PyObject* gets(const string& key);
  PyObject* get_multi(const vector<string>& keys);

  bool set(const string& key, PyObject* val, uint64_t time);
  bool add(const string& key, PyObject* val, uint64_t time);
  bool cas(const string& key, PyObject* val, uint64_t cas, uint64_t time);
  void SetInternal(const string& key, PyObject* val, uint64_t time = 0,
                   uint64_t cas = 0, bool replace = true);

  uint64_t incr(const string& key, int offset);
  uint64_t decr(const string& key, int offset);

  // Mostly for testing purposes.
  string Echo(const vector<string>& messages);
  PyObject* Test(PyObject* obj);

  string DecompressTest(const string& input) {
    string output;
    CHECK(DecompressInternal(input, &output));
    return output;
  }

 private:
  void WriteToKV(PyObject* str_obj, uint32_t flags,
                 memcache_router::KeyValue* kv);
  void PrepareValue(PyObject* val, memcache_router::KeyValue* kv);
  PyObject* Restore(const string& data, uint32_t flags);

  void GetInternal(const std::vector<std::string>& keys,
                   memcache_router::Instruction* response);

  // For compression we call zlib directly, because it can
  // deal with Py objects more efficiently.
  // But decompress requires dealing with bytes, which are
  // better handled through the below function.
  bool DecompressInternal(const string& input, string* output);

  PyObject* compress_;
  PyObject* cpickle_;
  PyObject* dumps_;
  PyObject* loads_;
  PyObject* zlib_;
  memcache_router::Instruction host_list_;
  void* async_socket_;
  void* context_;
  void* req_socket_;
};

#endif

