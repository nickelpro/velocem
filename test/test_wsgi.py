import pytest
import multiprocessing
from itertools import repeat
from urllib import request

import velocem

from apps import wsgi_echo
from util import wait_for_server


@pytest.fixture(scope='module')
def echo_server():
  p = multiprocessing.Process(target=velocem.wsgi, args=(wsgi_echo.app, ))
  p.start()
  wait_for_server('localhost', 8000)
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


def test_required_headers(echo_server):
  serv = f'Velocem/{velocem.__version__}'
  for _ in repeat(None, 10):
    with request.urlopen('http://localhost:8000') as resp:
      assert resp.headers['Server'] == serv
      assert bool(resp.headers['Date'])
