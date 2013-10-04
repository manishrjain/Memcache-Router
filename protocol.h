#ifndef MEMCACHE_ROUTER_PROTOCOL_H
#define MEMCACHE_ROUTER_PROTOCOL_H

#include <stdint.h>
#include <string>

#include "utils.h"

const uint8_t REQUEST = 0x80;
const uint8_t RESPONSE = 0x81;

enum Commands {
  GET = 0,
  SET,
  ADD,
  REPLACE,
  DELETE,
  INCREMENT,
  DECREMENT,
  QUIT,
  FLUSH,
  GETQ,
  NOOP,
  VERSION,
  GETK,
  GETKQ,
  APPEND,
  PREPEND,
  STAT,
  SETQ,
  ADDQ,
  REPLACEQ,
  DELETEQ,
  INCREMENTQ,
  DECREMENTQ,
  QUITQ,
  FLUSHQ,
  APPENDQ,
  PREPENDQ
};

// This is a 24 byte header.
struct ProtocolHeader {
  // Don't add any functions here. Keep it POD.
  uint8_t magic;
  uint8_t opcode;
  uint16_t key_length;
  uint8_t extra_length;
  uint8_t data_type;  // unused by memcached server.
  uint16_t reserved;  // unused.
  uint32_t total_body_length;  // This determins size of text.
  uint32_t opaque;
  uint64_t cas;
};

static void ResetHeader(ProtocolHeader* header) {
  header->magic = 0;
  header->opcode = 0;
  header->key_length = 0;
  header->extra_length = 0;
  header->data_type = 0;
  header->reserved = 0;
  header->total_body_length = 0;
  header->opaque = 0;
  header->cas = 0;
}

int ParseHeader(const string& response, int pos,
                ProtocolHeader* header);

class RequestPacket {
 public:
  RequestPacket() : num_(0) {}

  void Reset() {
    command_.clear();
    num_ = 0;
  }

  void Noop();
  void Get(const string& key);
  void Set(const string& key, const string& value,
           uint32_t flag, uint32_t expiry, uint64_t cas);

  const string& Command() {
    return command_;
  }

  int NumCommands() const {
    cout << "Num commmands: " << num_ << endl;
    return num_;
  }

  void PrintHex() const;

 private:
  string command_;
  int num_;
};

#endif

