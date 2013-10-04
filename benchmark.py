# This script benchmarks performance of Memcache Router Client against CMemcached.

from memcache_router import mrclient
import cmrclient
import a
import cmemcached
import time

class BenchmarkMemcache:
    def init(self):
        hosts = []
        for box in a.instance.current_data['mc'][0]:
            host = a.box.host_of_box(box)+':11211'
            print 'host:', host
            hosts.append(host)
        self.cmemcache_client = cmemcached.Client(hosts)

        print 'sending for mrclient'
        self.mrclient = mrclient.MemcacheRouterClient()
        for box in a.instance.current_data['mc'][0]:
            self.mrclient.add_host(a.box.host_of_box(box), 11211)
        self.mrclient.send_host_list()

        print 'sending for cmrclient'
        self.cmrclient = cmrclient.Client('test_benchmark')
        for box in a.instance.current_data['mc'][0]:
            self.cmrclient.add_host(a.box.host_of_box(box), 11211)
        self.cmrclient.send_host_list()

    def test_client_set(self, client, suffix, values):
        for i in range(0, 1000):
            client.set('testmrjn_' + suffix + str(i), values[i])

    def test_client_get(self, client, suffix, values):
        for i in range(0, 1000):
            if values[i] != client.get('testmrjn_' + suffix + str(i)):
                print 'Wrong value'
                print 'Got:', client.get('testmrjn_' + suffix + str(i))
                print 'Expected:', values[i]
                assert False

def timeit(message, fn, arg1, arg2, arg3):
    print
    print '**** ' + message + ' ****'
    durs = []
    for x in range(0, 3):
        start = time.time()
        fn(arg1, arg2, arg3)
        dur = (time.time() - start) * 1000.0
        durs.append(dur)
        print 'millis: ', dur
    print 'best of 3:', min(durs)


if __name__ == '__main__':
    a.instance.switch_instance('dwc')
    b = BenchmarkMemcache()
    b.init()

    values = []
    val = set([x for x in range(0, 1000)])
    for i in range(0, 1000):
        values.append(val)

    timeit('Cmemcached SET', b.test_client_set, b.cmemcache_client, 'cm', values)
    time.sleep(5)
    timeit('Cmemcached GET', b.test_client_get, b.cmemcache_client, 'cm', values)

    timeit('MRClient SET', b.test_client_set, b.mrclient, 'mr', values)
    time.sleep(5)
    timeit('MRClient GET', b.test_client_get, b.mrclient, 'mr', values)

    timeit('C MRClient SET', b.test_client_set, b.cmrclient, 'cmr', values)
    time.sleep(10)
    timeit('C MRClient GET', b.test_client_get, b.cmrclient, 'cmr', values)

