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
