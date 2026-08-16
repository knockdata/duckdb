//===----------------------------------------------------------------------===//
//
// objectfs_extension.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ObjectfsExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;
};

// SELECT * FROM 'x.sav' — formats duckdb has no reader for become read_parquet of the
// copy our server wrote into the cache
unique_ptr<TableRef> ObjectfsReplacement(ClientContext &context, ReplacementScanInput &input,
                                         optional_ptr<ReplacementScanData> data);

} // namespace duckdb
