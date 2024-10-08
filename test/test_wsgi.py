import pytest
import multiprocessing
from urllib import request

import velocem
from apps import wsgi

from util import wait_for_server, run_req_test, run_fail_test


def serv():
  velocem.wsgi(wsgi.app)


@pytest.fixture(scope='module')
def wsgi_server():
  p = multiprocessing.Process(target=serv)
  p.start()
  wait_for_server('localhost', 8000)
  yield p
  p.kill()


def root_OK():
  def f(resp):
    assert resp.read() == b''

  run_req_test(f)


def check_hello(resp):
  assert resp.read() == b'Hello World'


def test_hello_world(wsgi_server):
  run_req_test(check_hello, endpoint='/hello')


def test_list(wsgi_server):
  run_req_test(check_hello, endpoint='/list')


def test_tuple(wsgi_server):
  run_req_test(check_hello, endpoint='/tuple')


def test_iterator(wsgi_server):
  run_req_test(check_hello, endpoint='/iterator')


def test_generator(wsgi_server):
  run_req_test(check_hello, endpoint='/generator')


def test_call_close(wsgi_server):
  run_req_test(check_hello, endpoint='/call_close')

  def f(resp):
    val = int(resp.read().decode('ascii'))
    assert val == 10

  run_req_test(f, endpoint='/called_close')


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


def test_no_start_response(wsgi_server):
  run_fail_test(endpoint='/no_start_response')
  root_OK()


def test_invalid_body(wsgi_server):
  run_fail_test(endpoint='/invalid_body')
  root_OK()


def test_raise_exception_before_sr(wsgi_server):
  run_fail_test(endpoint='/raise_exception_before_sr')
  root_OK()


def test_raise_exception_after_sr(wsgi_server):
  run_fail_test(endpoint='/raise_exception_after_sr')
  root_OK()
