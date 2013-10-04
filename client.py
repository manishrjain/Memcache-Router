import socket, time

s = socket.socket()
s.connect(('0.0.0.0', 8080))
print 'sending %.6f' % time.time()
data = ''.join(['a' for x in range(0, 256)])
print s.send(data)
print s.recv(256)

print s.send(data)
print s.recv(256)

