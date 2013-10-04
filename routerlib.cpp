#include "routerlib.h"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.h"

Server::Server(const string& host, const string& port) :
    node_(NULL), done_(false),
    dst_port_(port), dst_hostname_(host) {
  InitThreads();
}

Server::~Server() {
  done_ = true;
  cout << "Calling reset on thread pool" << endl;
  cond_.notify_all();
  thread_pool_.Reset();
}

void Server::AddCommand(const string& command,
                        int num_instructions,
                        ResponseState* state) {
  state->IncrementPending();

  lock_guard<mutex> l(m_);
  if (!node_) {
    cout << "New node.." << endl;
    node_ = new RequestNode;
  }
  node_->request.append(command);
  node_->num_instructions.push_back(num_instructions);
  node_->states.push_back(state);

  // Wake up a thread.
  cond_.notify_one();
}

RequestNode* Server::BlockingGetRequestNode() {
  unique_lock<mutex> ul(m_);
  while (!done_ && !node_) {
    cond_.wait(ul);
  }
  if (done_)
    return NULL;

  RequestNode* tmp = node_;
  node_ = NULL;
  return tmp;
}

void Server::InitThreads() {
  thread_pool_.Reset();
  done_ = false;
  for (int i = 0; i < kNumThreads; ++i) {
    thread_pool_.threads.push_back(thread(&Server::ProcessRequests, this));
  }
}

void Server::ProcessRequests() {
  struct addrinfo hints;
  struct addrinfo* results;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          /* Any protocol */

  CHECK(getaddrinfo(dst_hostname_.c_str(), dst_port_.c_str(),
                    &hints, &results) == 0);

  int socket_fd;
  struct addrinfo* rp;
  for (rp = results; rp != NULL; rp = rp->ai_next) {
    socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (socket_fd == -1)
      continue;

    if (connect(socket_fd, rp->ai_addr, rp->ai_addrlen) != -1)
      break;
  }
  CHECK(rp != NULL);
  freeaddrinfo(results);

  char* buf = (char*) malloc(kBufferLen);
  while (!done_) {
    RequestNode* n = BlockingGetRequestNode();

    // We have exclusive access over contents belonging to n now.
    // So, there's no need for a lock around it.
    cout << "Got a node: " << n << endl;

    if (done_) {
      delete n;
      break;
    }

    ssize_t bytes = 0;
    for (int i = 0; i < kNumTries; ++i) {
      cout << "Sending request: " << n->request << " size: " << n->request.size() << endl;
      bytes = send(socket_fd, n->request.c_str(), n->request.size(), 0);
      if (bytes == n->request.size())
        break;
    }
    CHECK(bytes == n->request.size());
    cout << "successfully sent bytes: " << bytes << endl;

    string response;
    int state_idx = 0;
    while (true) {
      bytes = read(socket_fd, buf, kBufferLen);
      cout << "Received bytes: " << bytes << endl;
      response.assign(buf, bytes);

      ProtocolHeader header;
      int pos = 0;
      while (pos < response.size()) {
        int end_pos = ParseHeader(response, pos, &header);
        cout << "end pos, response size: " << end_pos << " " << response.size() << endl;
        n->states[state_idx]->AddResponse(response, pos, end_pos - pos);
        cout << "Key length: " << header.key_length << endl;
        cout << "cas: " << header.cas << endl;
        cout << "Total body length: " << header.total_body_length << endl;
        if (header.extra_length > 0) {
          CHECK(header.extra_length == 4);
          string extra_s = response.substr(pos + 24, header.extra_length);
          uint32_t extra;
          memcpy(&extra, extra_s.c_str(), 4);
          cout << "extra: " << extra << endl;
        }
        cout << "data: " << response.substr(pos + 24 + header.extra_length, header.total_body_length - header.extra_length) << endl;

        cout << "num_instructions: " << n->num_instructions[state_idx] << endl;
        if (--n->num_instructions[state_idx] == 0) {
          n->states[state_idx]->DecrementPending();
          ++state_idx;
          cout << "next state" << endl;
        } else {
          cout << "same state" << endl;
        }

        pos = end_pos;
        cout << endl;
      }

      if (state_idx >= n->states.size()) {
        break;
      }  // breaks away from reading loop.
    }
    delete n;
  }
  free(buf);
}

int main(int argc, char* argv[]) {
  Server s("devservices1", "11211");
  RequestPacket packet;
  packet.Get("test20");
  // packet.Noop();
  packet.PrintHex();
  ResponseState state;
  s.AddCommand(packet.Command(), packet.NumCommands(), &state);

  packet.Reset();
  packet.Set("mrjn2", to_string(rand() % 100), 0, 0, 0);
  packet.Get("mrjn2");
  ResponseState state2;
  s.AddCommand(packet.Command(), packet.NumCommands(), &state2);

  state.BlockingGetResponse();
  state2.BlockingGetResponse();
  cout << "Unblocked" << endl;
}

