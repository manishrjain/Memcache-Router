# Makefile

# The following 2 options are specific to cmrclient, which has to be compiled as 32 bit. The rest of the code is compiled by default to 64 bit.
# SITE_PKG="/tmp/pybindgen/lib/python2.7/site-packages"
SITE_PKG=.
OPTIONS=-std=c++11 -fPIC -pthread -fno-strict-aliasing -fwrapv -fvisibility=hidden -m32 -I../venv/include/python2.7
OPTIONS_64BIT=-std=c++11 -fPIC -pthread -fno-strict-aliasing -fwrapv -fvisibility=hidden -m64 -I../venv64/include/python2.7

all: memcache_router benchmark_lru_cache cmrclient_32bit cmrclient_64bit
client: cmrclient_32bit cmrclient_64bit

memdata_proto: memdata.proto
	protoc --cpp_out=. --python_out=../web/lib/memcache_router/ memdata.proto

utils: utils.cpp
	g++ -c -std=c++11 utils.cpp

lru_cache: lru_cache.h lru_cache.cpp memdata_proto
	g++ -c -std=c++11 lru_cache.cpp

consistent_hash: consistent_hash.h consistent_hash.cpp
	g++ -std=c++11 memdata.pb.cc consistent_hash.cpp -o consistent_hash -lcrypto -L lib -lprotobuf

benchmark_lru_cache: lru_cache memdata_proto benchmark_lru_cache.cpp
	pkg-config --cflags protobuf  # Fails if protobuf is not installed.
	g++ -std=c++11 lru_cache.cpp memdata.pb.cc benchmark_lru_cache.cpp `pkg-config --cflags --libs protobuf` -o benchmark_lru_cache -static-libstdc++ -L lib -ltcmalloc -lprofiler

communicate: utils communicate.cpp
	g++ -std=c++11 -fPIC -pthread utils.cpp communicate.cpp lib/libzmq.a -o communicate -lrt -static-libstdc++

routerlib: utils routerlib.cpp
	g++ -std=c++11 -fPIC -pthread utils.cpp protocol.cpp routerlib.cpp lib/libzmq.a -o routerlib -lrt -static-libstdc++

memclient: memclient.h memclient.cpp lru_cache memdata_proto
	pkg-config --cflags protobuf  # Fails if protobuf is not installed.
	g++ -c -std=c++11 memclient.cpp lru_cache.cpp memdata.pb.cc `pkg-config --cflags --libs protobuf` -L lib -lmemcached

memcache_router: memcache_router.cpp lru_cache memclient memdata_proto
	pkg-config --cflags protobuf  # Fails if protobuf is not installed.
	g++ -std=c++11 memcache_router.cpp lru_cache.cpp memclient.cpp memdata.pb.cc utils.cpp `pkg-config --cflags --libs protobuf` lib/libtcmalloc.a lib/libprofiler.a lib/libzmq.a lib/libmemcached.a -o memcache_router -lrt -lunwind -static-libstdc++ -lz

# This rule should generate a shared object which can then be imported into python modules.
# 1) Generate .o from C++ code.
# 2) Generate and compile wrapper code which exposes C++ functions in the C++ code above.
# 3) Generate final shared object which would be used by python code.
cmrclient_32bit: cmrclient.cpp memdata_proto
	g++ $(OPTIONS) -m32 cmrclient.cpp -c -o cmrclient.cc.2.o
	PYTHONPATH=$(PYTHONPATH):$(SITE_PKG) ../venv/bin/python2.7 modulegen.py > cmrclient_module.cc
	g++ $(OPTIONS) -m32 cmrclient_module.cc -c -o cmrclient_module.cc.2.o
	/usr/bin/g++ $(OPTIONS) -m32 -shared -Wl,-Bsymbolic-functions -pthread -Wl,-O1 -Wl,-Bsymbolic-functions cmrclient.cc.2.o cmrclient_module.cc.2.o lib32/libzmq.a lib32/libprotobuf.a memdata.pb.cc utils.cpp -o cmrclient.so -lz -lrt -static-libstdc++  # -Wl,-rpath -Wl,lib32
	../venv/bin/python cmrclient_doesitwork.py
	cp cmrclient.so ../web/lib/memcache_router/cmrclient.so

cmrclient_64bit: cmrclient.cpp memdata_proto
	g++ $(OPTIONS_64BIT) cmrclient.cpp -c -o cmrclient.cc.2.o
	PYTHONPATH=$(PYTHONPATH):$(SITE_PKG) ../venv64/bin/python modulegen.py > cmrclient_module.cc
	g++ $(OPTIONS_64BIT) cmrclient_module.cc -c -o cmrclient_module.cc.2.o
	/usr/bin/g++ $(OPTIONS_64BIT) -shared -Wl,-Bsymbolic-functions -pthread -Wl,-O1 -Wl,-Bsymbolic-functions cmrclient.cc.2.o cmrclient_module.cc.2.o lib/libzmq.a lib/libprotobuf.a memdata.pb.cc utils.cpp -o cmrclient.so -lz -lrt -static-libstdc++  # -Wl,-rpath -Wl,lib32
	../venv64/bin/python cmrclient_doesitwork.py
	cp cmrclient.so ../web/lib/memcache_router/lib64/cmrclient.so

clean:
	rm -f memcache_router
	rm -f benchmark_lru_cache
	rm -f routerlib
	rm -f communicate
	rm -f consistent_hash
	rm -f memclient.o memdata.pb.o
	rm -f utils.o
	rm -f lru_cache.o
	rm -f *.pyc
	rm -f ../web/lib/memcache_router/memdata_pb2.pyc
	rm -f cmrclient.cc.2.*
	rm -f cmrclient_module.*
	rm -f cmrclient.so
