import time
import socket

from itertools import repeat
from urllib import request


def wait_for_server(server, port, sleep=5):
  while True:
    try:
      s = socket.create_connection((server, port))
    except:
      time.sleep(sleep)
    else:
      s.close()
      break


def run_req_test(f, req='http://localhost:8000', reps=10, *, endpoint=''):
  if isinstance(req, str):
    req += endpoint
  for _ in repeat(None, 10):
    with request.urlopen(req) as resp:
      f(resp)
