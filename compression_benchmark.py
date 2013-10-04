import string
import random
import zlib
import time

chars = string.ascii_uppercase + string.digits

def test_for_size(message, fn, data):
    #print
    #print '**** ' + message + ' ****'
    durs = []
    lens = []
    tries = 10
    for x in range(0, tries):
        start = time.time()
        output = fn(data)
        dur = (time.time() - start) * 1000.0
        durs.append(dur)
        lens.append(len(output))
    print '%s,%f,%f,%d' % (message, min(durs), float(min(lens)) / len(data), len(data) - min(lens))
    # print 'best of ' + str(tries) + ' ms:', min(durs)
    # print 'compression ratio:', float(min(lens)) / len(data)

if __name__ == '__main__':
    for i in range(1, 100):
        data = ''.join(random.choice(chars) for x in range(100 * i))
        test_for_size(str(100 * i), zlib.compress, data)
    
