#include "objectfs_extension.hpp"
#include "object_file_system.hpp"
#include "server_client.hpp"

#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"

namespace duckdb {

// The formats our server can parse and duckdb cannot. Everything else — parquet, csv,
// json, xlsx — already has a reader, so the replacement scan must not touch it.
static const vector<string> CONVERTED_FORMATS = {"sav", "sas7bdat", "xpt"};

unique_ptr<TableRef> ObjectfsReplacement(ClientContext &context, ReplacementScanInput &input,
                                         optional_ptr<ReplacementScanData> data) {
	auto table_name = ReplacementScan::GetFullPath(input);
	if (ReplacementScan::CanReplace(table_name, CONVERTED_FORMATS)) {
		// The server downloads what the name points at, parses it, and writes the parquet
		// beside the cached original. A pattern names more than one file, so what comes back
		// is one parquet path per line — and read_parquet takes the whole list, which is how
		// 's3://bucket/*.sav' becomes a single scan over four converted files.
		auto body = ServerGet(ServerAddress(context), "/api/duckdb/localize?uri=" + UrlEncode(table_name));
		auto lines = SplitLines(body);
		if (lines.empty()) {
			throw IOException("objectfs: the server returned no parquet for %s", table_name);
		}

		vector<Value> paths;
		for (auto &line : lines) {
			paths.push_back(Value(line));
		}

		auto table_function = make_uniq<TableFunctionRef>();
		vector<unique_ptr<ParsedExpression>> children;
		if (paths.size() == 1) {
			children.push_back(make_uniq<ConstantExpression>(paths[0]));
		} else {
			children.push_back(make_uniq<ConstantExpression>(Value::LIST(LogicalType::VARCHAR, paths)));
		}
		table_function->function = make_uniq<FunctionExpression>("read_parquet", std::move(children));
		table_function->alias = FileSystem::GetFileSystem(context).ExtractBaseName(table_name);
		return std::move(table_function);
	} else {
		return nullptr;
	}
}

static void LoadInternal(ExtensionLoader &loader) {
	auto &database = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(database);

	config.AddExtensionOption("objectfs_server",
	                          "host:port of the explorer web server that serves cloud objects to duckdb",
	                          LogicalType::VARCHAR, Value(""));

	database.GetFileSystem().RegisterSubSystem(make_uniq<ObjectFileSystem>());
	config.replacement_scans.emplace_back(ObjectfsReplacement);
}

void ObjectfsExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string ObjectfsExtension::Name() {
	return "objectfs";
}

std::string ObjectfsExtension::Version() const {
#ifdef EXT_VERSION_OBJECTFS
	return EXT_VERSION_OBJECTFS;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(objectfs, loader) {
	duckdb::LoadInternal(loader);
}
}
