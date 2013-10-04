import a
import cPickle
import cmrclient
import time
import zlib

def test_types(client):
    d = {'testmrjn_int': int(45),
         'testmrjn_long': long(47),
         'testmrjn_str1': 'doeswork',
         'testmrjn_str2': 'abcdef' * 1024000,
         # 'testmrjn_str3': '',
         'testmrjn_bool': False,
         'testmrjn_bool2': True,
         'testmrjn_list': [x for x in range(0, 1000)],
         'testmrjn_set': set(['manish', 'rai']),
         'testmrjn_dict': {'manish': 'rai'}}
    for k, v in d.iteritems():
        client.set(k, v)
    time.sleep(3)
    for k, v in d.iteritems():
        print k
        ret = client.get(k)
        assert v == ret
    print 'types OK'

def test_counter(client):
    client.set('testmrjn_counter', 1)
    time.sleep(2)
    assert 6 == client.incr('testmrjn_counter', 5)
    assert 3 == client.decr('testmrjn_counter', 3)
    print 'counter OK'

def test_cas(client):
    val, cas = client.gets('testmrjn_dict')
    client.cas('testmrjn_dict', 'non_dict', cas)
    time.sleep(1)
    assert 'non_dict' == client.get('testmrjn_dict')

    client.cas('testmrjn_dict', 'something else', cas)
    time.sleep(2)
    assert 'non_dict' == client.get('testmrjn_dict')
    print 'cas OK'

def test_get(client):
    for x in range(10):
        client.set('testmrjn_testget' + str(x), x)
    time.sleep(2)
    d = client.get_multi(['testmrjn_testget' + str(x) for x in range(10)])
    for x in range(10):
        assert x == d.get('testmrjn_testget' + str(x), 0)
    print 'multi get OK'

def test_compression(client):
    data = [x for x in range(0, 1000000)]
    data = cPickle.dumps(data)
    print 'orig len:', len(data)
    comp = zlib.compress(data)
    print 'zlib compressed len:', len(comp)
    decomp = client.DecompressTest(comp)
    print 'received len:', len(decomp)
    assert data == decomp
    print 'decompress OK'

if __name__ == '__main__':
    client = cmrclient.Client('test_benchmark')
    print client.Echo(['a', 'b', 'c'])
    print client.Test(['Hello', 'World'])

    a.instance.switch_instance('dwc')
    for box in a.instance.current_data['mc'][0]:
        client.add_host(a.box.host_of_box(box), 11211)
    client.send_host_list()
    test_types(client)
    test_counter(client)
    test_cas(client)
    test_get(client)
    test_compression(client)

