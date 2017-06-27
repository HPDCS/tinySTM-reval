# server.py

import sys
import socket
import subprocess
import signal

if len(sys.argv) == 1:
    print('Usage: python %s port' % sys.argv[0])
    exit(0)


def signal_handler(signal, frame):
    print('\nGoodbye.')
    exit(0)


signal.signal(signal.SIGINT, signal_handler)


serversocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
host = socket.gethostname()
port = int(sys.argv[1])

serversocket.bind((host, port))
serversocket.listen(5)

while True:
    print('Ready...')

    clientsocket, addr = serversocket.accept()
    print('Got a connection from %s' % str(addr))

    payload = clientsocket.recv(1024).decode('ascii')
    cmd = payload.split()

    print('Executing command `%s`' % payload)

    try:
        subprocess.call(cmd)
    except subprocess.CalledProcessError:
        print('Unable to execute command: ' + cmd[0])

    clientsocket.close()
