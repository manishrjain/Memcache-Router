#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#include "utils.h"
using namespace std;

class ResponseState {
 public:
  ResponseState() : num_left_(0) {}

  void Reset() {
    lock_guard<mutex> l(m_);
    response_.clear();
    num_left_ = 0;
  }

  void IncrementPending() {
    lock_guard<mutex> l(m_);
    ++num_left_;
  }
  void DecrementPending() {
    lock_guard<mutex> l(m_);
    --num_left_;
    cout << "num left decrement" << endl;
    cond_.notify_one();
  }
  void AddResponse(const string& response, int start, int len) {
    lock_guard<mutex> l(m_);
    response_.append(response, start, len);
  }

  const string& BlockingGetResponse() {
    unique_lock<mutex> ul(m_);
    while (num_left_ > 0) {
      cout << "Num left: " << num_left_ << endl;
      cond_.wait(ul);
    }
    return response_;
  }

 private:
  atomic_int num_left_;
  condition_variable cond_;
  mutex m_;
  string response_;
};

struct RequestNode {
  string request;
  vector<ResponseState*> states;
  vector<int> num_instructions;
};

const int kNumThreads = 2;
const int kNumTries = 3;
const int kBufferLen = 512;
class Server {
 public:
  Server(const string& host, const string& port);
  ~Server();
  
  void AddCommand(const string& command, int num_instructions, ResponseState* state);
  RequestNode* BlockingGetRequestNode();  // Ownership is transferred to caller.

  void SetDone() {
    done_ = true;
    cond_.notify_all();
  }

 private:
  void InitThreads();
  void ProcessRequests();

  RequestNode* node_;
  atomic_bool done_;
  condition_variable cond_;
  mutable mutex m_;
  router_utils::ThreadPool thread_pool_;
  string dst_hostname_;
  string dst_port_;
};

