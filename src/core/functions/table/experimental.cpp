#include "duckpgq/core/functions/table/experimental.hpp"
#include <duckpgq/core/functions/table.hpp>

namespace duckdb {

//------------------------------------------------------------------------------
// Register functions
//------------------------------------------------------------------------------

void CoreTableFunctions::RegisterExperimentalTableFunction(ExtensionLoader &loader) {
    loader.RegisterFunction(GetGraphNameFunction());
    loader.RegisterFunction(CountNodesFunction());
}

}