#include "cmrclient.h"

#include <iostream>
#include <zmq.h>
#include <zlib.h>

#include "utils.h"
using namespace std;

Client::Client(const std::string& id) {
  cpickle_ = PyImport_ImportModule("cPickle");
  CHECK(cpickle_);

  dumps_ = PyObject_GetAttrString(cpickle_, "dumps");
  loads_ = PyObject_GetAttrString(cpickle_, "loads");
  CHECK(dumps_);
  CHECK(loads_);

  zlib_ = PyImport_ImportModule("zlib");
  CHECK(zlib_);

  compress_ = PyObject_GetAttrString(zlib_, "compress");
  CHECK(compress_);

  context_ = zmq_ctx_new();
  req_socket_ = zmq_socket(context_, ZMQ_REQ);
  zmq_connect(req_socket_, "tcp://localhost:5555");
  zmq_setsockopt(req_socket_, ZMQ_IDENTITY, id.c_str(), id.size());

  async_socket_ = zmq_socket(context_, ZMQ_PUSH);
  zmq_connect(async_socket_, "tcp://localhost:5556");
}

Client::~Client() {
  Py_DECREF(dumps_);
  Py_DECREF(loads_);
  Py_DECREF(cpickle_);
  Py_DECREF(compress_);
  Py_DECREF(zlib_);
}

void Client::reset_hosts() {
  host_list_.Clear();
}

void Client::add_host(const string& hostname, int port) {
  memcache_router::Server* server = host_list_.add_servers();
  server->set_hostname(hostname);
  server->set_port(port);
}

void Client::send_host_list() {
  string data;
  CHECK(host_list_.SerializeToString(&data));
  cout << "host list: " << host_list_.DebugString() << endl;
  cout << "send_host_list data size: "  << data.size() << endl;
  router_utils::SendHelper(req_socket_, data, 0);
  // Block until ack.
  zmq_msg_t message;
  int rc = zmq_msg_init(&message);
  CHECK(rc == 0);
  rc = zmq_msg_recv(&message, req_socket_, 0);
  CHECK(rc != -1);
  zmq_msg_close(&message);
}

PyObject* Client::get(const std::string& key) {
  memcache_router::Instruction i;
  vector<string> keys;
  keys.push_back(key);
  GetInternal(keys, &i);

  CHECK(i.get_keys_size() > 0);
  return Restore(i.get_keys(0).val(), i.get_keys(0).flags());
}

PyObject* Client::gets(const std::string& key) {
  memcache_router::Instruction i;
  vector<string> keys;
  keys.push_back(key);
  GetInternal(keys, &i);

  CHECK(i.get_keys_size() > 0);
  PyObject* val = Restore(i.get_keys(0).val(), i.get_keys(0).flags());
  return Py_BuildValue("Ol", val, i.get_keys(0).cas());
}

PyObject* Client::get_multi(const vector<string>& keys) {
  memcache_router::Instruction inst;
  GetInternal(keys, &inst);

  PyObject* dict = PyDict_New();
  for (int i = 0; i < inst.get_keys_size(); ++i) {
    PyObject* val = Restore(inst.get_keys(i).val(), inst.get_keys(i).flags());
    CHECK(PyDict_SetItemString(dict, inst.get_keys(i).key().c_str(), val) == 0);
  }
  return dict;
}

void Client::GetInternal(const vector<std::string>& keys,
                         memcache_router::Instruction* response) {
  memcache_router::Instruction i;
  for (std::string k : keys) {
    i.add_get_keys()->set_key(k);
  }
  string data;
  CHECK(i.SerializeToString(&data));
  router_utils::SendHelper(req_socket_, data, 0);

  zmq_msg_t message;
  int rc = zmq_msg_init(&message);
  CHECK(rc == 0);
  rc = zmq_msg_recv(&message, req_socket_, 0);
  CHECK(rc != -1);
  CHECK(response->ParseFromArray(
      zmq_msg_data(&message), zmq_msg_size(&message)));
  zmq_msg_close(&message);
}

// TODO(manish): Set the expiry properly.
bool Client::set(const string& key, PyObject* val, uint64_t time) {
  SetInternal(key, val, time);
  return true;
}

bool Client::add(const string& key, PyObject* val, uint64_t time) {
  SetInternal(key, val, time, 0, false);
  return true;
}

bool Client::cas(const string& key, PyObject* val,
                 uint64_t cas, uint64_t time) {
  SetInternal(key, val, time, cas);
}

void Client::SetInternal(const string& key, PyObject* val, uint64_t time,
                         uint64_t cas, bool replace) {
  // router_utils::Timer timer;
  memcache_router::Instruction i;
  memcache_router::KeyValue* kv = i.add_set_keys();
  kv->set_key(key);

  PrepareValue(val, kv);
  // int prepare_val_lap = timer.GetDelay();

  kv->set_cas(cas);
  kv->set_expire_in_seconds(time);
  kv->set_allow_replace(replace);
  // kv->set_no_propagate(..)

  string data;
  CHECK(i.SerializeToString(&data));
  router_utils::SendHelper(async_socket_, data, 0);
  // cout << "Set Internal us: " << prepare_val_lap
  //      << " " << timer.GetDelay() << endl;
}

uint64_t Client::incr(const string& key, int offset) {
  memcache_router::Instruction i;
  memcache_router::KeyValue* kv = i.add_incr_keys();
  kv->set_key(key);
  kv->set_offset(offset);

  string data;
  CHECK(i.SerializeToString(&data));
  router_utils::SendHelper(req_socket_, data, 0);

  memcache_router::Instruction response;
  zmq_msg_t message;
  int rc = zmq_msg_init(&message);
  CHECK(rc == 0);
  rc = zmq_msg_recv(&message, req_socket_, 0);
  CHECK(rc != -1);
  CHECK(response.ParseFromArray(zmq_msg_data(&message),
                                zmq_msg_size(&message)));
  zmq_msg_close(&message);

  CHECK(response.incr_keys_size() > 0);

  if (response.incr_keys(0).return_code() == 16) {
    // NOT FOUND
    return 0;
  }
  return response.incr_keys(0).counter_val();
}

uint64_t Client::decr(const string& key, int offset) {
  return incr(key, 0 - offset);
}

/*
void Client::cas(const string& key, const string& val,
                 uint64_t cas = 0, uint64_t time = 0, bool replace = true) {
}
*/
static int FLAG_PICKLE = 1 << 0;
static int FLAG_INTEGER = 1 << 1;
static int FLAG_LONG = 1 << 2;
static int FLAG_COMPRESSED = 1 << 3;
static int FLAG_BOOL = 1 << 4;
PyObject* true_str = PyObject_Str(Py_BuildValue("i", 1));
PyObject* false_str = PyObject_Str(Py_BuildValue("i", 0));

void Client::WriteToKV(PyObject* str_obj, uint32_t flags,
                       memcache_router::KeyValue* kv) {
  Py_ssize_t len;
  char* buf;
  PyString_AsStringAndSize(str_obj, &buf, &len);  // Pointer to internal.
  kv->set_val(buf, len);
  kv->set_flags(flags);
}

void Client::PrepareValue(PyObject* val, memcache_router::KeyValue* kv) {
  uint32_t flags = 0;
  bool can_compress = false;
  PyObject* rep = NULL;

  if (PyString_Check(val)) {
    can_compress = true;
    rep = val;
    Py_INCREF(rep);

  } else if (PyBool_Check(val)) {
    flags |= FLAG_BOOL;
    if (val == Py_True) {
      rep = true_str;
    } else {
      rep = false_str;
    }
    Py_INCREF(rep);

  } else if (PyInt_Check(val)) {
    flags |= FLAG_INTEGER;
    rep = PyObject_Str(val);

  } else if (PyLong_Check(val)) {
    flags |= FLAG_LONG;
    rep = PyObject_Str(val);

  } else {
    flags |= FLAG_PICKLE;
    PyObject* args = Py_BuildValue("Oi", val, -1);
    rep = PyObject_CallObject(dumps_, args);
    Py_DECREF(args);
    can_compress = true;
  }

  if (can_compress && PyString_Size(rep) > 3072) {
    // We don't want to compress all the string rep all the time.
    // As we have to pay for the compression time. So, I set a threshold
    // based upon how much the expected savings would be.
    // Based upon some analysis I ran, 3KB compresses to 66%.
    // This saves us 1KB over the wire, which I picked up as a threshold.
    // This is not a great way of picking up a threshold. Ideally, we'd
    // see the size distribution of our values over a day, and base it
    // such that we find a sweet spot where this threshold covers a lot
    // of keys.
    flags |= FLAG_COMPRESSED;
    PyObject* args = Py_BuildValue("(O)", rep);
    PyObject* final = PyObject_CallObject(compress_, args);
    WriteToKV(final, flags, kv);
    Py_DECREF(final);
    Py_DECREF(args);
  } else {
    WriteToKV(rep, flags, kv);
  }
  Py_DECREF(rep);
}

PyObject* Client::Restore(const string& data, uint32_t flags) {
  string decomp;
  if (flags & FLAG_COMPRESSED) {
    if (!DecompressInternal(data, &decomp)) {
      return Py_None;
    }
  }

  PyObject* retval = Py_None;
  if (flags & FLAG_BOOL) {
    if (data.empty()) {
      Py_RETURN_FALSE;
    } else {
      PyObject* int_val = PyInt_FromString(const_cast<char*>(data.c_str()), NULL, 0);
      retval = PyBool_FromLong(PyInt_AsLong(int_val));
      Py_DECREF(int_val);
    }

  } else if (flags & FLAG_INTEGER) {
    if (data.empty()) retval = PyInt_FromLong(0);
    else retval = PyInt_FromString(const_cast<char*>(data.c_str()), NULL, 0);

  } else if (flags & FLAG_LONG) {
    if (data.empty()) retval = PyInt_FromLong(0);
    else retval = PyLong_FromString(const_cast<char*>(data.c_str()), NULL, 0);

  } else if (flags & FLAG_PICKLE) {
    PyObject* args = NULL;
    if (decomp.empty()) {
      args = Py_BuildValue("(s#)", data.c_str(), data.size());
    } else {
      args = Py_BuildValue("(s#)", decomp.c_str(), decomp.size());
    }
    retval = PyObject_CallObject(loads_, args);
    Py_DECREF(args);

  } else {
    if (decomp.empty() && !data.empty()) {
      retval = PyString_FromStringAndSize(data.c_str(), data.size());
    } else if (!decomp.empty()) {
      retval = PyString_FromStringAndSize(decomp.c_str(), decomp.size());
    } else {
      retval = Py_None;
    }
  }
  return retval;
}

static const int kBufferLen = 64 << 10;
bool Client::DecompressInternal(const string& input, string* output) {
  z_stream zst;
  memset(&zst, 0, sizeof(zst));
  output->clear();

  if (inflateInit(&zst) != Z_OK)
    return false;

  zst.next_in = (Bytef*)input.data();
  zst.avail_in = input.size();

  int ret;
  vector<char*> output_data;
  do {
    char* out = (char*) malloc(kBufferLen);
    output_data.push_back(out);

    zst.next_out = reinterpret_cast<Bytef*>(out);
    zst.avail_out = kBufferLen;

    ret = inflate(&zst, 0);
  } while (ret == Z_OK);

  inflateEnd(&zst);

  int total_out = zst.total_out;
  if (ret == Z_STREAM_END) {
    output->resize(total_out);

    int pos = 0;
    for (char* s : output_data) {
      int size = min(total_out, kBufferLen);
      memcpy(&(output->at(pos)), s, size);
      total_out -= size;
      pos += size;
      free(s);
    }
    CHECK(total_out == 0);
    return true;
  }

  for (char* s : output_data) {
    free(s);
  }
  return false;
}

string Client::Echo(const vector<string>& messages) {
  memcache_router::Instruction i;
  memcache_router::KeyValue* kv = i.add_get_keys();
  kv->set_key("key1");
  string data;
  CHECK(i.SerializeToString(&data));
  CHECK(data.length() > 0);
  for (const string& s: messages) {
    cout << "Message Echo: " << s << endl;
  }
  return "\ncmrclient is ready!";
}

PyObject* Client::Test(PyObject* obj) {
  PyObject* args = Py_BuildValue("Oi", obj, -1);
  PyObject* dump = PyObject_CallObject(dumps_, args);
  Py_DECREF(args);
  CHECK(dump);

  args = Py_BuildValue("(O)", dump);
  PyObject* load = PyObject_CallObject(loads_, args);
  Py_DECREF(args);
  Py_DECREF(dump);

  return load;  // Ownership transferred to caller.
}

