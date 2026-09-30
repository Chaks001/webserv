*This project has been created as part of the 42 curriculum by jdellopo, vpoelman, paboute.*

# Webserv

## Description

Webserv is an HTTP/1.1 server written from scratch in C++98, without any external library. It serves static websites, accepts file uploads, deletes files, runs CGI scripts, and is configured through a file inspired by nginx.

The server is single-threaded and never blocks: a single `poll()` loop drives every listening socket, every client connection and every CGI pipe. The goal of the project is to understand HTTP end to end: reading raw requests from a TCP socket, handling many clients at once, and answering with accurate status codes when requests are malformed, too large or when a script fails.

## Instructions

### Requirements

- Linux or macOS
- A C++98 compiler available as `c++` (g++ or clang++) and `make`
- `python3` for the CGI examples; the provided configurations expect it at `/usr/bin/python3`

### Build

```bash
make
```

The binary `webserv` is compiled with `-Wall -Wextra -Werror -std=c++98`. The other targets are `make re` (full rebuild), `make clean` (object files) and `make fclean` (object files and binary).

### Run

```bash
./webserv [configuration_file]
```

Without an argument, `config/default.conf` is used. Stop the server with `Ctrl+C`.

### Provided configurations

| File | What it demonstrates |
|---|---|
| `config/eval.conf` | The defense setup: `localhost` and `alt_local` share port 8080 with different sites, a third server listens on 8081, plus a custom 404 page, a redirection, an upload and delete area, a 100-byte body limit and a directory with its own index file. |
| `config/default.conf` | A single server on port 8080 with directory listing, uploads and CGI. |
| `config/autoindex.conf` | Directory listing of `test_autoindex/`, including a subdirectory. |
| `config/test_official.conf` | The layout expected by the tester provided by 42. The `tester` and `cgi_tester` binaries are not part of this repository. |
| `config/error_server_empty.conf` | An empty `server` block, to check that defaults apply. |

## Features

- **Non-blocking I/O**: one `poll()` call watches listening sockets, clients and CGI pipes. Nothing is read or written without going through it.
- **Methods**: `GET`, `POST`, `DELETE` and `HEAD`, allowed or refused per location.
- **Static content**: files served with their MIME type, index files, and directory listing (`autoindex`).
- **Several servers**: several ports, and several `server_name` on the same port, chosen from the `Host` header.
- **Error pages and redirections**: custom error pages per status code, and `return` with 301, 302, 303, 307 or 308.
- **Request bodies**: `Content-Length` and chunked transfer encoding, limited by `client_max_body_size` (413). A client still sending a body that is too large receives the 413 answer before the connection closes.
- **Uploads**: raw bodies or `multipart/form-data`, as sent by an HTML form, saved under their original file name. Uploaded files can then be downloaded and deleted.
- **CGI**: chosen by file extension, run with a CGI/1.1 environment from the script's own directory, with the request body on standard input. A script that fails gives a 500, and a script inactive for 30 seconds is killed and gives a 504.
- **Timeouts**: a client connection inactive for 30 seconds is closed, so a request never hangs indefinitely.
- **Robust parsing**: malformed requests get 400, 413, 414, 431 or 505 instead of stalling. Paths are percent-decoded before routing, and any attempt to leave the root, even encoded, gets 403.
- **Validated configuration**: duplicate servers, unknown or repeated directives, a missing `;` or `root`, invalid values and invalid addresses stop the server at startup with an explicit message.

## Configuration reference

A configuration contains one or more `server` blocks. Each can hold `location` blocks, and the location with the longest matching prefix handles the request.

Each directive takes one value, ends with `;` and appears at most once per block. Only `location`, `error_page` and `cgi_pass` can be repeated, each time with a different path, status code or extension. Unknown directives are refused, and every location that serves files needs a `root`, its own or the server's. A configuration that breaks these rules is refused at startup rather than guessed.

```nginx
server {
    listen 8080;
    host 127.0.0.1;
    server_name localhost;
    root www;
    index index.html;
    error_page 404 errors/404.html;
    client_max_body_size 1000000;

    location /uploads {
        allow_methods GET POST DELETE;
        root www/uploads;
        upload_store www/uploads;
        autoindex on;
    }

    location /redirect {
        allow_methods GET;
        return 301 /;
    }

    location / {
        allow_methods GET POST;
        cgi_pass .py /usr/bin/python3;
    }
}
```

| Directive | Level | Meaning |
|---|---|---|
| `listen <port>` | server | Port to listen on, from 1 to 65535. |
| `host <address>` | server | IPv4 address to bind. `0.0.0.0` listens on every interface; anything that is not an IPv4 address is refused at startup. |
| `server_name <name>` | server | Name compared with the `Host` header of each request. |
| `root <path>` | server, location | Directory the requested paths are resolved in. Required for every location that serves files. |
| `index <file>` | server, location | File served when a directory is requested. |
| `error_page <code> <path>` | server | Custom page for a status code, relative to `root` and written without a leading `/`. |
| `client_max_body_size <bytes>` | server, location | Largest accepted request body. |
| `allow_methods <methods>` | location | Accepted methods; the others get 405. |
| `autoindex on\|off` | location | Directory listing when no index file exists. |
| `return <code> <url>` | location | Redirection. |
| `upload_store <path>` | location | Directory where uploaded files are written. |
| `cgi_pass <extension> <interpreter>` | location | Runs files with this extension through the interpreter. |

## Usage examples

These commands assume `./webserv config/eval.conf`.

```bash
curl -v http://localhost:8080/
```

Upload a file, as an HTML form would, then fetch and delete it:

```bash
curl -v -F "file=@my_file.txt" http://localhost:8080/uploads/
```

```bash
curl -v http://localhost:8080/uploads/my_file.txt
```

```bash
curl -v -X DELETE http://localhost:8080/uploads/my_file.txt
```

Run the CGI script, with and without a body:

```bash
curl -v http://localhost:8080/test.py
```

```bash
curl -v -X POST -d "hello" http://localhost:8080/test.py
```

Reach the second site hosted on the same port:

```bash
curl -v --resolve alt_local:8080:127.0.0.1 http://alt_local:8080/
```

Send a request by hand:

```bash
nc localhost 8080
```

Then type `GET / HTTP/1.1`, `Host: localhost`, and an empty line.

## Testing

- **In a browser**: with `eval.conf` or `default.conf`, open `http://localhost:8080/soutenance.html`. The page detects the loaded configuration, sends real requests and shows the expected status code next to each test.
- **With the tester provided by 42**: run `./webserv config/test_official.conf`, then `./tester http://localhost:8080`.
- **Under load**: `siege -b -t30S http://localhost:8080/` must keep an availability above 99.5%.

## Technical choices

- **One event loop.** Listening sockets, clients and CGI pipes all go through the same `poll()`. `errno` is never checked after `read`, `recv`, `write` or `send`; it is only read after `poll()`, to recognise an interruption by a signal, and after `bind()`, to report why the port could not be used.
- **`fork` only for CGI.** Before calling `execve`, the child closes every descriptor above standard error, so a script can neither reach other connections nor keep them open.
- **Timeouts count inactivity, not total time.** A client or a script that keeps making progress is never cut, while one that stops is.
- **Closing without losing the answer.** When a response ends the connection, the server first stops writing with `shutdown()`, then reads and discards what the client is still sending, for at most 30 seconds, before closing. Closing at once would make the system reset the connection, and a browser uploading a file that is too large would show an error instead of the 413 page.
- **A wrong configuration is an error, not a fallback.** The server refuses to start rather than guessing, for example rather than silently listening on every interface.

## Known limitations

- Pipelining is not supported: a connection handles one request at a time, and bytes sent after a complete request are discarded.
- Request bodies are kept in memory, so many simultaneous large uploads use a lot of RAM.
- Comments are not supported in the configuration: a `#` is refused as an unknown directive.
- Unlike nginx, a directive cannot list several values: one port per `listen`, one name per `server_name`, one file per `index`, one status code per `error_page`. Listening on several ports or answering to several names takes several `server` blocks.

## Resources

### References

- [RFC 9110: HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110) and [RFC 9112: HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112), which replace RFC 7230 to 7235
- [RFC 3875: The Common Gateway Interface (CGI) Version 1.1](https://www.rfc-editor.org/rfc/rfc3875)
- [nginx documentation](https://nginx.org/en/docs/), which inspired the configuration format
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [MDN Web Docs: HTTP](https://developer.mozilla.org/en-US/docs/Web/HTTP)
- Manual pages: `poll(2)`, `socket(2)`, `bind(2)`, `accept(2)`, `fork(2)`, `execve(2)`, `waitpid(2)`

### Use of AI

We used two AI assistants: ChatGPT (OpenAI) throughout the project, and Claude (Anthropic), through Claude Code, during its last phase. The design and the core of the server were written by us; the AI acted mainly as a tutor, a reviewer and an assistant.

- **Learning and understanding**: we spent a lot of time asking ChatGPT to explain HTTP, sockets, `poll()`, CGI and each part of our own code, until we understood how everything fits together.
- **Review and debugging**: finding and fixing bugs, and making the server more robust against malformed or unusual requests.
- **Some features**: help with file uploads from HTML forms.
- **Testing**: writing test scripts and running load and memory tests. These scripts are not part of this repository.
- **Documentation**: a first draft of this README.

Each proposal was reviewed and tested before being kept, and some were rejected or simplified.