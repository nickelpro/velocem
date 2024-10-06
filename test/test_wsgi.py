import pytest
import multiprocessing
from urllib import request

import velocem

from apps import wsgi
from util import wait_for_server, run_req_test


@pytest.fixture(scope='module')
def wsgi_server():
  p = multiprocessing.Process(target=velocem.wsgi, args=(wsgi.app, ))
  p.start()
  wait_for_server('localhost', 8000)
  yield p
  p.kill()


def test_hello_world(wsgi_server):
  def f(resp):
    assert resp.read() == b'Hello World'

  run_req_test(f, endpoint='/hello')


def test_echo(wsgi_server):
  req = request.Request(
      'http://localhost:8000/echo',
      b'Hello World',
      {'Hello': 'World'},
  )

  def f(resp):
    assert resp.headers['Hello'] == 'World'
    assert resp.read() == b'Hello World'

  run_req_test(f, req)


def test_required_headers(wsgi_server):
  serv = f'Velocem/{velocem.__version__}'

  def f(resp):
    assert resp.headers['Server'] == serv
    assert bool(resp.headers['Date'])

  run_req_test(f, endpoint='/hello')
