import boxman_push

def push():
    d = boxman_push.push('memcache_router',
                         ['memcache_router',
                          'benchmark_lru_cache',
                          'lib/',
                          'bin/',
                          'boxman-install.py',
                          'run',
                         ])

if __name__ == '__main__':
    push()
