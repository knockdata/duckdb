//===----------------------------------------------------------------------===//
//
// server_client.hpp
//
// The smallest possible HTTP/1.1 client, over a plain socket.
//
// duckdb is built here with DISABLE_BUILTIN_HTTPLIB=1, and the only server this ever
// talks to is our own web server on localhost, so a vendored HTTP library would be
// several thousand lines to reach one host that is already in the same process tree.
// Request, read to EOF, split the headers off. That is the whole protocol we need.
//
// The server answers in plain text — one value or one path per line — so there is no
// JSON parser here either.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb.hpp"

namespace duckdb {

// GET http://<server>/<request_path> and return the response body.
// server is "host:port". Throws IOException for a connection failure or any status but 200.
string ServerGet(const string &server, const string &request_path);

// percent-encode everything that is not unreserved, so a URI can travel as a query value
string UrlEncode(const string &value);

// split a body into lines, dropping the trailing empty one
vector<string> SplitLines(const string &body);

} // namespace duckdb
