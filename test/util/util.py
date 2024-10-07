import time
import socket

from itertools import repeat
from urllib import request, error


def wait_for_server(server, port, sleep=5):
  while True:
    try:
      s = socket.create_connection((server, port))
    except:
      time.sleep(sleep)
    else:
      s.close()
      break


def Empty(resp):
  pass


def run_req_test(f=Empty, req='http://localhost:8000', reps=10, *,
                 endpoint=''):
  if isinstance(req, str):
    req += endpoint
  for _ in repeat(None, reps):
    with request.urlopen(req) as resp:
      f(resp)


def run_fail_test(f=Empty, req='http://localhost:8000', reps=10, *,
                  endpoint=''):
  if isinstance(req, str):
    req += endpoint
  for _ in repeat(None, reps):
    try:
      request.urlopen(req)
    except error.HTTPError as e:
      f(e)
    else:
      raise TypeError('Request did not raise HTTPError')
