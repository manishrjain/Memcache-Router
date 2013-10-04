#include "communicate.h"

#include <cstring>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>

#include "utils.h"

Communicate::Communicate(bool is_creator) {
  int fd = 0;
  if (is_creator) {
    fd = shm_open(kMemName, O_CREAT | O_TRUNC | O_RDWR, 0666);
    CHECK(fd != -1);
  } else {
    cout << "for non creator" << endl;
    fd = shm_open(kMemName, O_RDWR, 0666);
    cout << "Checking" << endl;
    CHECK(fd != -1);
  }

    int r = ftruncate(fd, kMemSize);
    CHECK(r == 0);

  // TODO: This has some issues with reader processes unable to access
  // data after the first try. Find the cause and fix it.

  //int r = ftruncate(fd, kMemSize);
  //CHECK(r == 0);

  memptr_ = mmap(0, kMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  CHECK(memptr_ != MAP_FAILED);
}

Communicate::~Communicate() {
  int r = munmap(memptr_, kMemSize);
  CHECK(r == 0);

  r = shm_unlink(kMemName);
  CHECK(r == 0);
}

void Communicate::Write(const string& data) {
  memcpy(memptr_, data.c_str(), data.size());
  string written((const char*)memptr_, 24);
  cout << "data: " << written << endl;
}

void Communicate::Read() {
  string data((const char*)memptr_, 24);
  cout << "data: " << data << endl;
}

int main(int argc, char* argv[]) {
  bool is_creator = string(argv[1]) == "0";
  Communicate comm(is_creator);

  cout << "argv 1: " << argv[1] << endl;
  // while (strcmp(argv[1], "0") == 0) {
  while (string(argv[1]) == "0") {
    string data;
    cout << "Enter data: ";
    cin >> data;
    comm.Write(data);
    cout << "Written" << endl;
  }
  comm.Read();
  return 0;
}

