import time
import socket


def wait_for_server(server, port, sleep=5):
  while True:
    try:
      s = socket.create_connection((server, port))
    except:
      time.sleep(sleep)
    else:
      s.close()
      break
