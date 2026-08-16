//===----------------------------------------------------------------------===//
//
// object_file_system.hpp
//
// A FileSystem for cloud object URIs — s3://, gs://, gcs://, az://, abfs://, abfss:// —
// that owns none of the cloud protocols itself.
//
// Every path is handed to our web server, which already holds the sign-in, the listing
// cache and the blob cache for that provider. The server downloads the object into
// ~/.objectexplorer and answers with the absolute local path; from there the file is an
// ordinary local file, so OpenFile returns a LocalFileSystem handle and every Read after
// that is a plain pread with no HTTP in it at all.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb.hpp"
#include "duckdb/common/local_file_system.hpp"

namespace duckdb {

class ObjectFileSystem : public FileSystem {
public:
	unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags,
	                                optional_ptr<FileOpener> opener = nullptr) override;
	vector<OpenFileInfo> Glob(const string &path, FileOpener *opener = nullptr) override;

	// Everything that takes an open handle is the local filesystem's work, because the
	// handle OpenFile returned is one of its own. Some readers ask the filesystem they
	// opened from rather than going back through the handle — json is one — so these
	// cannot be left to the base class, which throws.
	void Read(FileHandle &handle, void *buffer, int64_t bytes, idx_t location) override;
	int64_t Read(FileHandle &handle, void *buffer, int64_t bytes) override;
	int64_t GetFileSize(FileHandle &handle) override;
	timestamp_t GetLastModifiedTime(FileHandle &handle) override;
	string GetVersionTag(FileHandle &handle) override;
	FileType GetFileType(FileHandle &handle) override;
	void Seek(FileHandle &handle, idx_t location) override;
	idx_t SeekPosition(FileHandle &handle) override;
	void Reset(FileHandle &handle) override;
	void FileSync(FileHandle &handle) override;
	bool FileExists(const string &filename, optional_ptr<FileOpener> opener = nullptr) override;
	bool DirectoryExists(const string &directory, optional_ptr<FileOpener> opener = nullptr) override;
	bool CanHandleFile(const string &path) override;
	bool IsPathAbsolute(const string &path) override;
	bool CanSeek() override;
	bool OnDiskFile(FileHandle &handle) override;
	string PathSeparator(const string &path) override;
	std::string GetName() const override;

private:
	LocalFileSystem local;
};

// true for any URI scheme this filesystem answers for
bool IsObjectUri(const string &path);

// true for `demo/sales.parquet` — a path with no scheme whose first segment names a folder
// the user added to the explorer. The list of those names comes from the server.
bool IsRootRelative(const string &path);

// the configured "host:port" of our web server: the objectfs_server setting when one is
// set, else the OBJECTFS_SERVER environment variable, else localhost on the default port
string ServerAddress(optional_ptr<FileOpener> opener);
string ServerAddress(ClientContext &context);

// ask the server for the local path of a URI, downloading and converting it if needed
string LocalizeUri(const string &uri, optional_ptr<FileOpener> opener);

} // namespace duckdb
