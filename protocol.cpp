#include "protocol.h"

#include <endian.h>
#include <netinet/in.h>

int ParseHeader(const string& response, int pos,
                 ProtocolHeader* header) {
  if (response.size() < pos + sizeof(ProtocolHeader)) {
    return false;
  }

  memcpy(header, const_cast<char*>(response.c_str() + pos),
         sizeof(ProtocolHeader));

  // Convert from network standard big endian ordering to host endian ordering.
  header->key_length = be16toh(header->key_length);
  header->reserved = be16toh(header->reserved);
  header->total_body_length = be32toh(header->total_body_length);
  header->opaque = be32toh(header->opaque);
  header->cas = be64toh(header->cas);

  cout << "header magic: " << (int) header->magic << endl;
  CHECK(header->magic == RESPONSE);
  return pos + sizeof(ProtocolHeader) + header->total_body_length;
}

void RequestPacket::PrintHex() const {
  cout << "Command: ";
  for (int i = 0; i < command_.size(); ++i) {
    uint8_t c = command_[i];
    printf("%02x ", c);
    if (i % 4 == 3) {
      cout << " | ";
    }
  }
  cout << endl;
}

void RequestPacket::Get(const string& key) {
  ++num_;
  ProtocolHeader header;
  ResetHeader(&header);

  header.magic = REQUEST;
  header.opcode = GET;
  header.key_length = htobe16(key.size());
  header.total_body_length = htobe32(key.size());

  char buf[24];
  memcpy(&buf, &header, 24);
  command_.append(buf, 24);
  command_.append(key);
}

void RequestPacket::Set(const string& key, const string& value,
                        uint32_t flag, uint32_t expiry, uint64_t cas) {
  ++num_;
  ProtocolHeader header;
  ResetHeader(&header);

  header.magic = REQUEST;
  header.opcode = SET;
  header.key_length = htobe16(key.size());
  header.extra_length = 8;  // 4 byte flag + 4 byte expiry.
  header.total_body_length = htobe32(key.size() + value.size() + 8);
  header.cas = htobe64(cas);
  char buf[24];
  memcpy(&buf, &header, 24);
  command_.append(buf, 24);

  uint32_t be_flag = htobe32(flag);
  memcpy(&buf, &be_flag, 4);
  command_.append(buf, 4);

  uint32_t be_expiry = htobe32(expiry);
  memcpy(&buf, &be_expiry, 4);
  command_.append(buf, 4);

  command_.append(key);
  command_.append(value);
}

void RequestPacket::Noop() {
  ++num_;
  ProtocolHeader header;
  ResetHeader(&header);
  header.magic = REQUEST;
  header.opcode = NOOP;

  char buf[24];
  memcpy(&buf, &header, 24);
  command_.append(buf, 24);
}

