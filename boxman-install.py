import os
import signal
import socket

if __name__ == '__main__':
    # First find and kill the old process.
    for line in os.popen("ps ax"):
        fields = line.split()
        pid = fields[0]
        if not pid.isdigit():
            continue
        pid = int(pid)
        process = fields[4]
        if process.find('memcache_router') > 0:
            print 'killing pid:', pid, 'process:', process
            os.kill(pid, signal.SIGKILL)

    params = 'env CPUPROFILE=/tmp/memcache_router.prof CPUPROFILESIGNAL=16 LD_LIBRARY_PATH=./lib ./memcache_router '
    after = ' 1>/tmp/ans-memcache_router-stdout 2>/tmp/ans-memcache_router-stderr &'
    # Now run the new one.
    if socket.gethostname().find('mailconstruct') >= 0:
        print 'Running new memcache_router w/ 28GB ...'
        os.system(params + '30064771072 16' + after)
    else:
        print 'Running new memcache_router w/ no cache...'
        os.system(params + '0 16' + after)

