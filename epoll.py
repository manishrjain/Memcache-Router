import socket, select
import time

EOF='EOF'
serversocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
serversocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
serversocket.bind(('0.0.0.0', 8080))
serversocket.listen(1)
serversocket.setblocking(0)

epoll = select.epoll()
epoll.register(serversocket.fileno(), select.EPOLLIN)

connections = {}
requests = {}

try:
  while True:
    events = epoll.poll(1)
    for fileno, event in events:
      if fileno == serversocket.fileno():
        connection, address = serversocket.accept()
        connection.setblocking(0)
        # print 'Connection from ', address
        epoll.register(connection.fileno(), select.EPOLLIN)
        connections[connection.fileno()] = connection  # store
      elif event & select.EPOLLIN:
        data = connections[fileno].recv(1024)  # receive
        # print '%.6f %s' % (time.time(), data)
        # epoll.modify(fileno, select.EPOLLOUT)
        # print 'received data', data
        if not data:
          # print 'done'
          epoll.unregister(fileno)  # commented this out.
          # del connections[fileno]
          #epoll.modify(fileno, select.EPOLLOUT)  # reply
          # print 'data: ', data
      elif event & select.EPOLLOUT:
        written = connections[fileno].send('ack')  # write.
        epoll.modify(fileno, select.EPOLLIN)
        # connections[fileno].shutdown(socket.SHUT_RDWR)
        # epoll.unregister(fileno)
      elif event & select.EPOLLHUP:
        epoll.unregister(fileno)
        connections[fileno].close()
        del connections[fileno]
finally:
  epoll.unregister(serversocket.fileno())
  epoll.close()
  serversocket.close()

