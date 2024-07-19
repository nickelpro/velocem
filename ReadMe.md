[![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/nickelpro/velocem/main.yaml?style=for-the-badge)](https://github.com/nickelpro/velocem/actions/workflows/main.yaml)
[![Gunicorn](https://img.shields.io/badge/Faster%20Than%20Gunicorn-1473%25-08824c?style=for-the-badge)](https://github.com/benoitc/gunicorn)
[![FastWSGI](https://img.shields.io/badge/Faster%20Than%20FastWSGI-9.68%25-lightblue?style=for-the-badge)](https://github.com/jamesroberts/fastwsgi)

# Velocem

This is a Work-In-Progress experimental Python extension. It aims to provide
a stupid-fast, pointlessly low-latency, web development framework.

If you use this code in production you get what you deserve.

## Motivation

All of the fast Python server-providing web frameworks work off the same basic
theory, take fast native IO and HTTP components and bind them to Python. They
use slightly different optimization tricks and shortcuts along the way, but
that's the shape of things.

A quick survery of the field is presented below.

| Framework | IO Library | HTTP Library |              Notes                 |
| --------- | ---------- | ------------ | -----------------------------------|
| [Bjoern](https://github.com/jonashaag/bjoern) | [libev](https://github.com/enki/libev) | [http_parser](https://github.com/nodejs/http-parser)  | The OG, surpassed by modern frameworks, pioneered most of the micro-optimizations used by later entrants. Notable especially for being the first to take whatever Node.js was doing at the time and applying it to Python.  |
| [Japronto!](https://github.com/squeaky-pl/japronto/tree/master) | [libuv](https://github.com/libuv/libuv) via [uvloop](https://github.com/MagicStack/uvloop) | [picohttpparser](https://github.com/h2o/picohttpparser) | An early example of saying "Screw WSGI!" Every developer working on fast Python web frameworks quickly realizes WSGI is a deadend, to be supported only for compatibility. |
| [uWSGI](https://github.com/unbit/uwsgi) | None | None | Never very fast, but written in C and very popular. Worth mentioning because it pioneered the uwsgi _protocol_ as a replacement for HTTP/1.1 for communicating between load balancers and application servers. |
| [Socketify.py](https://github.com/cirospaciari/socketify.py) | [µWebSockets](https://github.com/uNetworking/uWebSockets) | [µWebSockets](https://github.com/uNetworking/uWebSockets) | The current industry standard for speed. Spends a lot of implementation effort to maximize compatibility with PyPy, under which it runs 2-3x faster. Still very fast under CPython, but is the undisputed king for PyPy-based stacks. |
| [emmet](https://github.com/emmett-framework/emmett) / [granian](https://github.com/emmett-framework/granian) | [tokio](https://github.com/tokio-rs/tokio) | [hyper](https://github.com/hyperium/hyper) | Rust! You knew one of these was going to be in Rust. Here it is. The one that's in Rust. |
| [FastWSGI](https://github.com/jamesroberts/fastwsgi) | [libuv](https://github.com/libuv/libuv) | [llhttp](https://github.com/nodejs/llhttp) | The modern successor to Bjoern, King of the "Do What Node Does" Throne. Screaming fast. Real thorn in the side of projects trying to claim to be the fastest WSGI server. Demolishes micro-benchmarks. All server, no framework. |
| Velocem | [asio](https://github.com/chriskohlhoff/asio) ([Docs](https://think-async.com/Asio/)) | [llhttp](https://github.com/nodejs/llhttp) | The one you're looking at right now. Notable for being the only framework on this list primarily written in C++. Will cross any lines and break any rules of software engineering to shave microseconds off the WSGI "Hello World" app.

**What about FastAPI/Starlette/Blacksheep/apidaora/Falcon/My Favorite Web Framework?**

Don't provide a server, or only provide a development server. Benchmarks where
these run screaming fast are relying on a server-providing framework such as
the ones above, so they don't get credit.

**What about CherryPy/Gunicorn/Uvicorn/Tornado/Twisted?**

Painfully slow, by a factor of 10x or more than the above frameworks. Most of
these rely heavily on pure-Python components which means that their latency
skyrockets regardless of what IO strategy they implement. Higher latency, lower
req/sec, higher resource usage, means you need more application servers to
handle a given load.

**What exactly is the motivation?**

To be the fastest, at everything. WSGI, ASGI, Routing, Velocem's own custom
interface, maybe more.

The goal is to be the anchor statistic, the 1x against which everything else
is measured in factors of. And, by demonstrating what is possible, hopefully
motivating other more commerical, production-oriented frameworks to adopt
more performant techniques as well.

## What Works

* **WSGI**: A complete PEP 3333 implementation, with only the lightest of
  shortcuts taken. Marginally more conforming than FastWSGI, and ahead by
  a fraction of a microsecond on benchmarks. Blows everything else out of the
  water.

* **HTTP/1.1**: We parse it and return valid responses. Yippee.

## What's On Deck

* **Tests and Benchmarks**: Need to clean these up and get them into the repo.

* **sendfile**: The single most obvious WSGI optimization, but needs special
handling code

* **ASGI**: Will likely only support the latest standard. Going to need to
implement our own asyncio loop for this to have any shot of being fast.

* **Router**: Routing is what massacres most benchmarks. The "Hello World"
  Flask app is 5x slower than the raw WSGI equivalent. A fast router is
  essential to a fast, low latency application.

* **uwsgi Protocol**: HTTP/1.1 is non-ideal as a lingua franca for web
  application backends, uwsgi is an improvement worth exploring.

--------------

## License

The Velocem source code (and associated build/test/utility files) is released
into the public domain via CC0-1.0, see `License` for details.

I (Vito Gamberini) hold no patents and have no knowledge of any patented
techniques used by Velocem. However, some organizations refuse to incorporate or
distribute public domain code due to patent concerns, for this reason Velocem is
additionally licensed for use under MIT-0, see `UsageLicense` for details.

The purpose in using these well known legal texts is that they are widely
trusted and understood by the open source community. A bespoke legal text might
limit usage by particularly skittish developers or organizations. However, I
want to make my intentions clear in plain English as well:

**Do what you want with this stuff. There are zero restrictions on the use of
this code in any context.**
