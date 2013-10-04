#include <stdio.h>
#include <string>
using namespace std;

const char* kMemName = "/memcacheroutercommunicate";
size_t kMemSize = 1 << 30;

class Communicate {
 public:
  explicit Communicate(bool is_creator);
  ~Communicate();
  void Write(const string& data);
  void Read();

 private:
  void* memptr_;
};

