#include "utils.h"

#include <cstring>
#include <iostream>
#include <zmq.h>

namespace router_utils {

void SendHelper(void* worker, const string& data, int flags) {
  zmq_msg_t msg;
  int rc = zmq_msg_init_size(&msg, data.size());
  CHECK(rc == 0);
  memcpy(zmq_msg_data(&msg), data.c_str(), data.size());
  rc = zmq_msg_send(&msg, worker, flags);
  if (rc == -1) {
    cerr << "zmq errno: " << zmq_errno() << endl;
  }
  CHECK(rc == data.size());
}

}  // router_utils
