#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <libmemcached/memcached.h>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include <zmq.h>

#include "lru_cache.h"
#include "memclient.h"
#include "memdata.pb.h"
#include "utils.h"
using namespace std;

using router_utils::Timer;
using router_utils::Stats;
using router_utils::ThreadSafeStats;
#define MAX_KEYS_PER_REQUEST 10000

struct Packet {
  Timer timer;
  vector<string> frame_ids;
  memcache_router::Instruction instruction;

  enum Type {
    UNKNOWN,
    GET,
    SET,
    INCREMENT,
    SERVER_LIST,
    STATS,
  };

  Type GetType() const {
    if (instruction.get_keys_size() > 0) {
      return GET;
    } else if (instruction.set_keys_size() > 0) {
      return SET;
    } else if (instruction.incr_keys_size() > 0) {
      return INCREMENT;
    } else if (instruction.servers_size() > 0) {
      return SERVER_LIST;
    } else if (instruction.has_stats()) {
      return STATS;
    } else {
      cerr << "Instruction: " << instruction.DebugString() << endl;
      CHECK(false);
    }
  }
  bool IsGet() const {
    return instruction.get_keys_size() > 0;
  }

  void Print() {
    for (int i = 0; i < frame_ids.size(); ++i) {
      cout << "client id: " << frame_ids[i] << endl;
    }
    cout << "packet data: " << instruction.DebugString() << endl;
  }
};

class PCQueue {
  public:
    PCQueue() : done_(false) {}

    void UnblockAll() {
      done_ = true;
      cond_.notify_all();
    }

    void Reset() {
      done_ = false;
    }

    void BlockingPop(vector<Packet*>* packets) {
      unique_lock<mutex> ul(m_);
      // The following line doesn't compile even with c++0x flag.
      // cond_.wait(ul, [this]{return !packets_.empty();});
      // The next expression should be equivalent.
      while (!done_ && packets_.empty()) {
        cond_.wait(ul);  // release lock, block.
        // When notified, this would wake up, and reacquire lock
        // before returning.
      }
      if (done_)
        return;

      Timer t;
      int num_get_keys = 0;
      while (!packets_.empty() && num_get_keys < MAX_KEYS_PER_REQUEST) {
        Packet* p = packets_.front();

        if (!p->IsGet()) {
          // All such operations should be run singly because
          // libmemcached doesn't allow batching them up.
          if (num_get_keys == 0) {
            packets_.pop_front();
            packets->push_back(p);
          }
          // else don't include this packet.
          break;
        } else {
          packets_.pop_front();
          packets->push_back(p);
          num_get_keys += p->instruction.get_keys_size();
        }
      }

      // If we didn't process all the packets, notify another thread.
      if (packets_.size() > 0) {
        cond_.notify_one();
      }
      pop_stats_.Increment(t.GetDelay());
    }

    void Push(Packet* p) {
      // Push is taking around 13 us.
      Timer t;
      lock_guard<mutex> lk(m_);
      packets_.push_back(p);
      cond_.notify_one();
      push_stats_.Increment(t.GetDelay());
    }

    void PopulateStats(Packet* p) {
      lock_guard<mutex> lk(m_);
      p->instruction.mutable_stats()->mutable_push_latency()->set_average(
          push_stats_.Average());
      p->instruction.mutable_stats()->mutable_push_latency()->set_count(
          push_stats_.counter);
      p->instruction.mutable_stats()->mutable_pop_latency()->set_average(
          pop_stats_.Average());
      p->instruction.mutable_stats()->mutable_pop_latency()->set_count(
          pop_stats_.counter);
    }

  private:
    Stats pop_stats_;
    Stats push_stats_;  // GUARDED_BY m_
    atomic_bool done_;
    condition_variable cond_;
    deque<Packet*> packets_;  // GUARDED_BY m_
    mutable mutex m_;
};

class MemcacheRouter {
 public:
  explicit MemcacheRouter(uint64_t cache_size, int num_threads)
      : cache_(NULL), done_(false),
        num_threads_(num_threads), server_list_(NULL) {
    cout << "Cache set to " << cache_size << endl;
    cout << "Threads set to " << num_threads << endl;
    if (cache_size > 0) {
      cache_ = new Cache(cache_size);
    }
    context_ = zmq_ctx_new();
    router_ = zmq_socket(context_, ZMQ_ROUTER);
    int rc = zmq_bind(router_, "tcp://*:5555");
    CHECK(rc == 0);

    async_ = zmq_socket(context_, ZMQ_PULL);
    rc = zmq_bind(async_, "tcp://*:5556");
    CHECK(rc == 0);

    worker_output_ = zmq_socket(context_, ZMQ_PULL);
    rc = zmq_bind(worker_output_, "inproc://workers");
    CHECK(rc == 0);
  }

  ~MemcacheRouter() {
    zmq_close(router_);
    zmq_close(async_);
    zmq_close(worker_output_);
    zmq_ctx_destroy(context_);
  }

  void Loop() {
    while (true) {
      zmq_pollitem_t items [] = {
        { router_, 0, ZMQ_POLLIN, 0 },
        { async_, 0, ZMQ_POLLIN, 0 },
        { worker_output_, 0, ZMQ_POLLIN, 0 }
      };

      zmq_poll(items, 3, -1);
      // Poll for inter-process communication (through clients).
      if (items[0].revents & ZMQ_POLLIN) {
        Packet* p = ReceiveOnePacket(router_);

        // The following statement is useful for benchmarking purposes.
        // SendAndDeletePacket(router_, p);

        Packet::Type packet_type = p->GetType();
        if (packet_type == Packet::SERVER_LIST) {
          // Until an instruction arrives for setting hosts,
          // the router wouldn't process any requests.
          SetMemcacheServers(p);
          SendEmptyPacket(router_, p);
          delete p;

        } else if (packet_type == Packet::SET) {
          SendEmptyPacket(router_, p);
          get_queue_.Push(p);

        } else if (packet_type == Packet::STATS) {
          PopulateStats(p);
          SendAndDeletePacket(router_, p);
        } else {
          // For GET and INCR, we need to wait before replying.
          get_queue_.Push(p);
        }
      }

      // For SETs, client doesn't have to wait. So we use PULL socket.
      if (items[1].revents & ZMQ_POLLIN) {
        Packet* p = ReceiveOnePacket(async_, false);
        CHECK(p->GetType() == Packet::SET);
        get_queue_.Push(p);
      }

      // Poll for intra-process communication (through workers).
      if (items[2].revents & ZMQ_POLLIN) {
        int more = 1;
        while (more) {
          zmq_msg_t message;
          zmq_msg_init(&message);
          zmq_msg_recv(&message, worker_output_, 0);

          more = zmq_msg_more(&message) ? ZMQ_SNDMORE : 0;
          zmq_msg_send(&message, router_, more);
          zmq_msg_close(&message);
        }
      }
    }
  }

  void BlockingWait() {
    done_ = true;
    get_queue_.UnblockAll();
    thread_pool_.Reset();
  }

  void ProcessPackets() {
    MemClient client(cache_);
    {
      lock_guard<mutex> lk(server_list_m_);
      client.Init(server_list_->instruction);
    }

    void* worker = zmq_socket(context_, ZMQ_PUSH);
    zmq_connect(worker, "inproc://workers");

    Stats loop_latency;
    Stats batch_stats;
    Stats packet_stats;
    int counter = 0;
    random_device rd;

    while (!done_) {
      vector<Packet*> packets;
      get_queue_.BlockingPop(&packets);
      batch_stats.Increment(packets.size());

      Timer t;
      map<string, memcache_router::KeyValue> key_to_kvalp;
      vector<Packet*> get_packets;
      for (int i = 0; i < packets.size(); ++i) {
        Packet* p = packets[i];
        Packet::Type t = p->GetType();
        if (t == Packet::SET) {
          client.SetKeys(&p->instruction);
          packet_stats.Increment(p->timer.GetDelay());
          delete p;  // No need to send.

        } else if (t == Packet::INCREMENT) {
          client.IncrKeys(&p->instruction);
          packet_stats.Increment(p->timer.GetDelay());
          SendAndDeletePacket(worker, p);

        } else if (t == Packet::GET) {
          for (int j = 0; j < p->instruction.get_keys_size(); ++j) {
            const memcache_router::KeyValue& kv = p->instruction.get_keys(j);
            key_to_kvalp.insert(make_pair(
                kv.key(), memcache_router::KeyValue()));
          }
          get_packets.push_back(p);
        }
      }

      if (get_packets.size() > 0) {
        client.GetKeys(&key_to_kvalp);
        for (int i = 0; i < get_packets.size(); ++i) {
          Packet* p = get_packets[i];
          for (int j = 0; j < p->instruction.get_keys_size(); ++j) {
            memcache_router::KeyValue* kv = p->instruction.mutable_get_keys(j);
            auto itr = key_to_kvalp.find(kv->key());
            CHECK(itr != key_to_kvalp.end());
            kv->MergeFrom(itr->second);
          }
          packet_stats.Increment(p->timer.GetDelay());
          SendAndDeletePacket(worker, p);
        }
      }
      loop_latency.Increment(t.GetDelay());

      // Let's merge with final stats once in a while,
      // to avoid lock contention.
      // rd() calls runs in 1us, but counter is almost free.
      if (++counter > 100) {
        counter = 0;
        if (rd() % num_threads_ == 1) {
          loop_latency_.Merge(loop_latency);
          batch_size_.Merge(batch_stats);
          packet_latency_.Merge(packet_stats);

          loop_latency.Reset();
          batch_stats.Reset();
          packet_stats.Reset();
        }
      }
    }
  }

 private:
  void InitThreads() {
    thread_pool_.Reset();
    done_ = false;
    get_queue_.Reset();
    for (int i = 0; i < num_threads_; ++i) {
      thread_pool_.threads.push_back(thread(&MemcacheRouter::ProcessPackets, this));
    }
  }

  Packet* ReceiveOnePacket(void* medium, bool multi_frame = true) {
    Packet* p = new Packet;
    int more = 0;
    bool is_data = !multi_frame;
    do {
      zmq_msg_t msg;
      int rc = zmq_msg_init(&msg);
      CHECK(rc == 0);
      rc = zmq_msg_recv(&msg, medium, 0);
      CHECK(rc != -1);
      if (is_data) {
        p->instruction.ParseFromArray(zmq_msg_data(&msg), zmq_msg_size(&msg));
      } else {
        string id(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
        if (id.empty()) {
          is_data = true;
        } else {
          p->frame_ids.push_back(id);
        }
      }
      more = zmq_msg_more(&msg);
      zmq_msg_close(&msg);
    } while (more);
    return p;
  }

  void SendFrames(void* worker, Packet* p) {
    for (int i = 0; i < p->frame_ids.size(); ++i) {
      string fid = p->frame_ids[i];
      router_utils::SendHelper(worker, fid, ZMQ_SNDMORE);
    }
    router_utils::SendHelper(worker, "", ZMQ_SNDMORE);
  }

  void SendEmptyPacket(void* worker, Packet* p) {
    SendFrames(worker, p);
    router_utils::SendHelper(worker, "", 0);
  }

  void SendAndDeletePacket(void* worker, Packet* p) {
    SendFrames(worker, p);
    string data;
    CHECK(p->instruction.SerializeToString(&data));
    router_utils::SendHelper(worker, data, 0);
    delete p;
  }

  // NOTE: This function should already have mutex lock acquired.
  bool IsServerListMatching(Packet* p) {
    if (!server_list_)
      return false;

    for (int i = 0; i < server_list_->instruction.servers_size(); ++i) {
      if (server_list_->instruction.servers(i).hostname() !=
          p->instruction.servers(i).hostname()) {
        return false;
      }
      if (server_list_->instruction.servers(i).port() !=
          p->instruction.servers(i).port()) {
        return false;
      }
    }
    return true;
  }

  void SetMemcacheServers(Packet* p) {
    CHECK(p->GetType() == Packet::SERVER_LIST);
    {
      lock_guard<mutex> lk(server_list_m_);
      if (!IsServerListMatching(p)) {
        BlockingWait();  // Wait for all threads to die.
        delete server_list_;
        server_list_ = new Packet(*p);
        InitThreads();
      }
    }
  }

  void PopulateStats(Packet* p) {
    get_queue_.PopulateStats(p);
    batch_size_.Set(p->instruction.mutable_stats()->mutable_batch_size());
    loop_latency_.Set(p->instruction.mutable_stats()->mutable_loop_latency());
    packet_latency_.Set(p->instruction.mutable_stats()
        ->mutable_packet_latency());
    if (cache_) cache_->PopulateStats(p->instruction.mutable_stats());
  }

  Cache* cache_;  // Shared among all threads.
  PCQueue get_queue_;
  router_utils::ThreadPool thread_pool_;
  ThreadSafeStats batch_size_;
  ThreadSafeStats loop_latency_;
  ThreadSafeStats packet_latency_;
  atomic_bool done_;
  int num_threads_;
  memcache_router::Instruction empty_;
  void* async_;
  void* context_;
  void* router_;
  void* worker_output_;

  mutable mutex server_list_m_;
  Packet* server_list_;
};

int main(int argc, char* argv[]) {
  if (argc < 3) {
    cerr << "Usage: " << argv[0] << " <cache size (Set zero to avoid cache)>"
         << " <num threads>" << endl;
    return -1;
  }

  uint64_t cache_size = strtoull(argv[1], NULL, 10);
  int threads = atoi(argv[2]);
  MemcacheRouter* router = new MemcacheRouter(cache_size, threads);
  router->Loop();  // This would block forever.
  router->BlockingWait();
  delete router;
  return 0;
}
