def app(environ, start_response):
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
