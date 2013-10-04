import memdata_pb2
import os
import time
import zmq

class QueryStats:
    def __init__(self):
        pid = str(os.getpid())
        context = zmq.Context()
        self.socket = context.socket(zmq.REQ)
        self.socket.setsockopt(zmq.IDENTITY, pid)
        self.socket.connect("tcp://localhost:5555")
        self.counter = 0

    def run(self):
        instruction = memdata_pb2.Instruction()
        instruction.stats.touch = True
        self.socket.send(instruction.SerializeToString())

        message = self.socket.recv()
        instruction.ParseFromString(message)
        print 'counter: ', self.counter
        self.counter += 1
        print instruction

if __name__ == '__main__':
    stats = QueryStats()
    while True:
        stats.run()
        time.sleep(5)
        
