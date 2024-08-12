import pytest
import velocem
import multiprocessing
import time
import socket

from apps import wsgi_echo
from itertools import repeat
from urllib import request


@pytest.fixture(scope='session')
def echo_server():
  p = multiprocessing.Process(target=velocem.wsgi, args=(wsgi_echo.app, ))
  p.start()

  while True:
    try:
      s = socket.create_connection(('localhost', 8000))
    except:
      time.sleep(5)
    else:
      s.close()
      break

  yield p
  p.kill()


def test_hello_world(echo_server):
  req = request.Request(
      'http://localhost:8000',
      b'Hello World',
      {'Hello': 'World'},
  )
  for _ in repeat(None, 10):
    with request.urlopen(req) as resp:
      assert resp.headers['Hello'] == 'World'
      assert resp.read() == b'Hello World'
