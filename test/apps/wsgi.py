import velocem

router = velocem.Router()


@router.get('/')
def root(environ, start_response):
  start_response('200 OK', [])
  return b''


@router.get('/hello')
def hello(environ, start_response):
  start_response('200 OK', [])
  return b'Hello World'


@router.post('/echo')
def echo(environ, start_response):
  headers = []
  for key, val in environ.items():
    if key.startswith('HTTP_') and key not in (
        'HTTP_ACCEPT_ENCODING',
        'HTTP_CONTENT_LENGTH',
        'HTTP_CONTENT_TYPE',
        'HTTP_HOST',
        'HTTP_USER_AGENT',
    ):
      headers.append((key[5:], val))
  start_response('200 OK', headers)
  return environ['wsgi.input'].read()


@router.get('/list')
def list_(environ, start_response):
  start_response('200 OK', [])
  return [b'Hello', b' ', b'World']


@router.get('/tuple')
def tuple_(environ, start_response):
  start_response('200 OK', [])
  return (b'Hello', b' ', b'World')


@router.get('/iterator')
def iter_(environ, start_response):
  class Iter:
    def __init__(self):
      self.val = 0

    def __iter__(self):
      return self

    def __next__(self):
      self.val += 1
      match self.val:
        case 1:
          return b'Hello'
        case 2:
          return b' '
        case 3:
          return b'World'
        case 4:
          raise StopIteration

  start_response('200 OK', [])
  return Iter()


@router.get('/generator')
def gen(environ, start_response):
  start_response('200 OK', [])
  yield b'Hello'
  yield b' '
  yield b'World'


called_close_count = 0


@router.get('/call_close')
def call_close(environ, start_response):
  start_response('200 OK', [])

  class Iter:
    def __init__(self):
      self.val = 0

    def __iter__(self):
      return self

    def __next__(self):
      if self.val:
        raise StopIteration
      self.val = 1
      return b'Hello World'

    def close(self):
      global called_close_count
      called_close_count += 1

  return Iter()


@router.get('/called_close')
def called_close(environ, start_response):
  start_response('200 OK', [])
  return f'{called_close_count}'.encode('ascii')


@router.get('/no_start_response')
def no_start_response(environ, start_response):
  return b'Hello World'


@router.get('/invalid_body')
def invalid_body(environ, start_response):
  start_response('200 OK', [])
  return None


@router.get('/raise_exception_before_sr')
def raise_exception_before_sr(environ, start_response):
  raise RuntimeError('Test Exception')


@router.get('/raise_exception_after_sr')
def raise_exception_after_sr(environ, start_response):
  start_response('200 OK', [])
  raise RuntimeError('Test Exception')


app = router.wsgi_app

if __name__ == '__main__':
  velocem.wsgi(app)
