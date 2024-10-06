import velocem

router = velocem.Router()


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


def app(environ, start_response):
  h, _ = router.get_route(environ['REQUEST_METHOD'], environ['PATH_INFO'])
  return h(environ, start_response)


if __name__ == '__main__':
  velocem.wsgi(app)
