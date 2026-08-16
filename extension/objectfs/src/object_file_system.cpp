#include "object_file_system.hpp"
#include "server_client.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_opener.hpp"
#include "duckdb/common/mutex.hpp"

#include <ctime>

namespace duckdb {

static const char *SCHEMES[] = {"s3://", "gs://", "gcs://", "az://", "abfs://", "abfss://", nullptr};

static const char *DEFAULT_SERVER = "127.0.0.1:8080";

bool IsObjectUri(const string &path) {
	bool matched = false;
	for (idx_t index = 0; SCHEMES[index]; index++) {
		string scheme = SCHEMES[index];
		if (path.size() > scheme.size() && path.compare(0, scheme.size(), scheme) == 0) {
			matched = true;
		}
	}
	return matched;
}

// the setting when it holds something, else the environment, else localhost
static string ServerOr(const Value &setting) {
	if (!setting.IsNull() && !setting.ToString().empty()) {
		return setting.ToString();
	} else {
		auto from_environment = FileSystem::GetEnvVariable("OBJECTFS_SERVER");
		if (from_environment.empty()) {
			return DEFAULT_SERVER;
		} else {
			return from_environment;
		}
	}
}

// `demo/sales.parquet` is a file inside the folder the user named "demo", and only the
// server knows where that folder is. duckdb would otherwise resolve it against its own
// working directory and find nothing, so this filesystem has to claim it — which means
// knowing the folder names.
//
// The names are asked for once and held for a few seconds: this runs for every path duckdb
// resolves, and a folder added in the explorer should still show up without a restart.
// Anything absolute, or with a scheme, or without a slash is answered before asking.
bool IsRootRelative(const string &path) {
	static mutex roots_lock;
	static vector<string> roots;
	static time_t fetched = 0;

	auto slash = path.find('/');
	bool candidate = slash != string::npos && slash > 0 && path[0] != '/' && path[0] != '\\' &&
	                 path.find("://") == string::npos;
	if (!candidate) {
		return false;
	}

	lock_guard<mutex> guard(roots_lock);
	auto now = time(nullptr);
	if (now - fetched > 5) {
		fetched = now;
		try {
			roots = SplitLines(ServerGet(ServerAddress(nullptr), "/api/duckdb/roots"));
		} catch (std::exception &) {
			// no server, or it does not answer: nothing here is ours, and duckdb opens the
			// path itself exactly as it would have without this extension
			roots.clear();
		}
	}

	auto first = path.substr(0, slash);
	for (auto &root : roots) {
		if (root == first) {
			return true;
		}
	}
	return false;
}

string ServerAddress(optional_ptr<FileOpener> opener) {
	Value setting;
	FileOpener::TryGetCurrentSetting(opener, "objectfs_server", setting);
	return ServerOr(setting);
}

string ServerAddress(ClientContext &context) {
	Value setting;
	context.TryGetCurrentSetting("objectfs_server", setting);
	return ServerOr(setting);
}

// One round trip per distinct URI. The server is the one that knows whether the bytes are
// already cached, so there is no second cache here to keep in step with it.
string LocalizeUri(const string &uri, optional_ptr<FileOpener> opener) {
	auto body = ServerGet(ServerAddress(opener), "/api/duckdb/localize?uri=" + UrlEncode(uri));
	auto lines = SplitLines(body);
	if (lines.empty()) {
		throw IOException("objectfs: the server returned no local path for %s", uri);
	}
	return lines[0];
}

unique_ptr<FileHandle> ObjectFileSystem::OpenFile(const string &path, FileOpenFlags flags,
                                                  optional_ptr<FileOpener> opener) {
	if (flags.OpenForWriting()) {
		throw NotImplementedException("objectfs cannot write to %s — cloud objects open read only", path);
	}
	// the handle carries its own filesystem, so every Read/Seek after this goes straight to
	// the local file and never comes back through here
	return local.OpenFile(LocalizeUri(path, opener), flags, opener);
}

void ObjectFileSystem::Read(FileHandle &handle, void *buffer, int64_t bytes, idx_t location) {
	local.Read(handle, buffer, bytes, location);
}

int64_t ObjectFileSystem::Read(FileHandle &handle, void *buffer, int64_t bytes) {
	return local.Read(handle, buffer, bytes);
}

int64_t ObjectFileSystem::GetFileSize(FileHandle &handle) {
	return local.GetFileSize(handle);
}

timestamp_t ObjectFileSystem::GetLastModifiedTime(FileHandle &handle) {
	return local.GetLastModifiedTime(handle);
}

string ObjectFileSystem::GetVersionTag(FileHandle &handle) {
	return local.GetVersionTag(handle);
}

FileType ObjectFileSystem::GetFileType(FileHandle &handle) {
	return local.GetFileType(handle);
}

void ObjectFileSystem::Seek(FileHandle &handle, idx_t location) {
	local.Seek(handle, location);
}

idx_t ObjectFileSystem::SeekPosition(FileHandle &handle) {
	return local.SeekPosition(handle);
}

void ObjectFileSystem::Reset(FileHandle &handle) {
	local.Reset(handle);
}

void ObjectFileSystem::FileSync(FileHandle &handle) {
	local.FileSync(handle);
}

vector<OpenFileInfo> ObjectFileSystem::Glob(const string &path, FileOpener *opener) {
	auto body = ServerGet(ServerAddress(opener), "/api/duckdb/glob?uri=" + UrlEncode(path));
	vector<OpenFileInfo> matches;
	for (auto &uri : SplitLines(body)) {
		matches.push_back(OpenFileInfo(uri));
	}
	return matches;
}

bool ObjectFileSystem::FileExists(const string &filename, optional_ptr<FileOpener> opener) {
	auto body = ServerGet(ServerAddress(opener), "/api/duckdb/stat?uri=" + UrlEncode(filename));
	auto lines = SplitLines(body);
	return !lines.empty() && lines[0] != "missing";
}

// A prefix is not a directory in object storage, and nothing above us needs it to be one.
bool ObjectFileSystem::DirectoryExists(const string &directory, optional_ptr<FileOpener> opener) {
	return false;
}

bool ObjectFileSystem::CanHandleFile(const string &path) {
	return IsObjectUri(path) || IsRootRelative(path);
}

bool ObjectFileSystem::IsPathAbsolute(const string &path) {
	return IsObjectUri(path) || IsRootRelative(path);
}

bool ObjectFileSystem::CanSeek() {
	return true;
}

bool ObjectFileSystem::OnDiskFile(FileHandle &handle) {
	return true;
}

string ObjectFileSystem::PathSeparator(const string &path) {
	return "/";
}

std::string ObjectFileSystem::GetName() const {
	return "ObjectFileSystem";
}

} // namespace duckdb
