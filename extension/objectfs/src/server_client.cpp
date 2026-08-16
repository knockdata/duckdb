#include "server_client.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET (-1)
#define CLOSE_SOCKET close
#endif

#include <cstring>

namespace duckdb {

static const char *UNRESERVED = "-_.~";

string UrlEncode(const string &value) {
	static const char *HEX = "0123456789ABCDEF";
	string encoded;
	for (auto character : value) {
		auto byte = static_cast<unsigned char>(character);
		bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
		             strchr(UNRESERVED, byte) != nullptr;
		if (plain) {
			encoded += static_cast<char>(byte);
		} else {
			encoded += '%';
			encoded += HEX[byte >> 4];
			encoded += HEX[byte & 0x0F];
		}
	}
	return encoded;
}

vector<string> SplitLines(const string &body) {
	vector<string> lines;
	string line;
	for (auto character : body) {
		if (character == '\n') {
			if (!line.empty()) {
				lines.push_back(line);
			}
			line.clear();
		} else if (character == '\r') {
			// a bare CR is never part of a path
		} else {
			line += character;
		}
	}
	if (!line.empty()) {
		lines.push_back(line);
	}
	return lines;
}

// "host:port" -> the two halves. A server without a port is not something we ever
// configure, so an absent colon is a configuration error rather than a default.
static void SplitServer(const string &server, string &host, string &port) {
	auto colon = server.rfind(':');
	if (colon == string::npos) {
		throw IOException("objectfs_server must be host:port, got '%s'", server);
	}
	host = server.substr(0, colon);
	port = server.substr(colon + 1);
}

#ifdef _WIN32
// winsock has to be started once per process before any socket call
static void StartSockets() {
	static bool started = false;
	if (!started) {
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
		started = true;
	}
}
#else
static void StartSockets() {
}
#endif

static socket_t ConnectTo(const string &host, const string &port) {
	StartSockets();

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *candidates = nullptr;
	if (getaddrinfo(host.c_str(), port.c_str(), &hints, &candidates) != 0) {
		throw IOException("objectfs cannot resolve %s:%s", host, port);
	}

	socket_t connected = INVALID_SOCKET;
	for (auto candidate = candidates; candidate && connected == INVALID_SOCKET; candidate = candidate->ai_next) {
		auto attempt = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
		if (attempt != INVALID_SOCKET) {
			if (connect(attempt, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
				connected = attempt;
			} else {
				CLOSE_SOCKET(attempt);
			}
		}
	}
	freeaddrinfo(candidates);

	if (connected == INVALID_SOCKET) {
		throw IOException("objectfs cannot reach the server at %s:%s — is the explorer running?", host, port);
	}
	return connected;
}

static void SendAll(socket_t handle, const string &text) {
	idx_t sent = 0;
	while (sent < text.size()) {
		auto written = send(handle, text.data() + sent, static_cast<int>(text.size() - sent), 0);
		if (written > 0) {
			sent += static_cast<idx_t>(written);
		} else {
			throw IOException("objectfs lost the connection while sending the request");
		}
	}
}

// Connection: close, so the body ends at EOF and no chunked decoding or Content-Length
// bookkeeping is needed.
static string ReceiveAll(socket_t handle) {
	string response;
	char buffer[8192];
	auto received = recv(handle, buffer, sizeof(buffer), 0);
	while (received > 0) {
		response.append(buffer, static_cast<size_t>(received));
		received = recv(handle, buffer, sizeof(buffer), 0);
	}
	return response;
}

string ServerGet(const string &server, const string &request_path) {
	string host;
	string port;
	SplitServer(server, host, port);

	auto handle = ConnectTo(host, port);
	string response;
	try {
		string request = "GET " + request_path + " HTTP/1.1\r\n";
		request += "Host: " + host + ":" + port + "\r\n";
		request += "Accept: text/plain\r\n";
		request += "Connection: close\r\n\r\n";
		SendAll(handle, request);
		response = ReceiveAll(handle);
	} catch (...) {
		CLOSE_SOCKET(handle);
		throw;
	}
	CLOSE_SOCKET(handle);

	auto separator = response.find("\r\n\r\n");
	if (separator == string::npos) {
		throw IOException("objectfs got a truncated reply for %s", request_path);
	}
	auto status_line = response.substr(0, response.find("\r\n"));
	auto headers = response.substr(0, separator);
	auto body = response.substr(separator + 4);

	// Reading to EOF is only the body when the body is the rest of the connection. A
	// chunked reply would put size markers through the middle of it, and a path with a
	// hex number in front of it fails much further away than here.
	if (StringUtil::Contains(StringUtil::Lower(headers), "transfer-encoding: chunked")) {
		throw IOException("objectfs got a chunked reply for %s, which this client cannot read", request_path);
	}
	if (status_line.find(" 200 ") == string::npos) {
		throw IOException("objectfs request %s failed: %s — %s", request_path, status_line, body);
	}
	return body;
}

} // namespace duckdb
