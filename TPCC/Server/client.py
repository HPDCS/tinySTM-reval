# client.py

import sys
import socket

if len(sys.argv) <= 3:
    print('Usage: python %s address port command...' % sys.argv[0])
    exit(0)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
host = sys.argv[1]
port = int(sys.argv[2])

sock.connect((host, port))
print('Connected to %s:%s' % (host, port))

cmd = ' '.join(sys.argv[3:])

sock.send(cmd.encode('ascii'))
print('Sent command `%s`' % cmd)

sock.close()
