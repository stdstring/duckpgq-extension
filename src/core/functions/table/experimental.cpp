#include "duckpgq/core/functions/table/experimental.hpp"
#include <duckpgq/core/functions/table.hpp>

namespace duckdb {

namespace {

    int64_t ExtractGraphData(ClientContext &context,
                             CreatePropertyGraphInfo const* property_graph,
                             std::unordered_set<int64_t> &all_vertices,
                             std::unordered_map<int64_t, std::vector<int64_t>> &adjacency_list) {
        int64_t edge_count = 0;
        auto &catalog = Catalog::GetCatalog(context, INVALID_CATALOG);
        auto &transaction = DuckTransaction::Get(context, catalog);
        // scanning the physical edge tables of the graph
        for (auto &edge_table_meta : property_graph->edge_tables) {
            string edge_table_name = edge_table_meta->table_name;
            if (edge_table_meta->source_fk.empty() || edge_table_meta->destination_fk.empty()) {
                throw BinderException("Таблица ребер '" + edge_table_name + "' должна иметь корректно заданные SOURCE/TARGET KEY.");
            }
            string source_col = edge_table_meta->source_fk[0];
            string target_col = edge_table_meta->destination_fk[0];
            auto &table_entry = catalog.GetEntry<TableCatalogEntry>(context, DEFAULT_SCHEMA, edge_table_name);
            auto &data_table = table_entry.GetStorage();
            idx_t src_col_idx = DConstants::INVALID_INDEX;
            idx_t tgt_col_idx = DConstants::INVALID_INDEX;
            for (idx_t i = 0; i < table_entry.GetColumns().LogicalColumnCount(); i++) {
                auto &col = table_entry.GetColumns().GetColumn(LogicalIndex(i));
                if (col.GetName() == source_col)
                    src_col_idx = i;
                if (col.GetName() == target_col)
                    tgt_col_idx = i;
            }
            if (src_col_idx == DConstants::INVALID_INDEX || tgt_col_idx == DConstants::INVALID_INDEX) {
                continue;
            }
            // constructing a StorageIndex vector for a physical scan of the storage
            vector<StorageIndex> column_ids = { StorageIndex(src_col_idx), StorageIndex(tgt_col_idx) };
            TableScanState scan_state;
            data_table.InitializeScan(context, transaction, scan_state, column_ids, nullptr);
            vector<LogicalType> scan_types;
            scan_types.push_back(table_entry.GetColumns().GetColumn(LogicalIndex(src_col_idx)).GetType());
            scan_types.push_back(table_entry.GetColumns().GetColumn(LogicalIndex(tgt_col_idx)).GetType());
            DataChunk scan_chunk;
            scan_chunk.Initialize(context, scan_types, STANDARD_VECTOR_SIZE);
            // Buffer chunk for safe type casting to BIGINT (int64_t)
            vector<LogicalType> cast_types(2, LogicalType::BIGINT);
            DataChunk cast_chunk;
            cast_chunk.Initialize(context, cast_types, STANDARD_VECTOR_SIZE);
            while (true) {
                scan_chunk.Reset();
                data_table.Scan(transaction, scan_chunk, scan_state);
                if (scan_chunk.size() == 0) {
                    break;
                }
                cast_chunk.Reset();
                cast_chunk.SetCardinality(scan_chunk.size());
                VectorOperations::Cast(context, scan_chunk.data[0], cast_chunk.data[0], scan_chunk.size());
                VectorOperations::Cast(context, scan_chunk.data[1], cast_chunk.data[1], scan_chunk.size());
                auto src_data = FlatVector::GetData<int64_t>(cast_chunk.data[0]);
                auto tgt_data = FlatVector::GetData<int64_t>(cast_chunk.data[1]);
                for (idx_t i = 0; i < scan_chunk.size(); i++) {
                    int64_t u = src_data[i];
                    int64_t v = tgt_data[i];
                    all_vertices.insert(u);
                    all_vertices.insert(v);
                    adjacency_list[u].push_back(v);
                    adjacency_list[v].push_back(u);
                    edge_count++;
                }
            }
        }
        return edge_count;
    }

    int64_t CalcComponentsCount(std::unordered_set<int64_t> const &all_vertices,
                                std::unordered_map<int64_t, std::vector<int64_t>> const &adjacency_list) {
        std::unordered_set<int64_t> visited;
        int64_t components_count = 0;
        for (auto vertex : all_vertices) {
            if (visited.find(vertex) == visited.end()) {
                components_count++;
                std::queue<int64_t> q;
                q.push(vertex);
                visited.insert(vertex);
                while (!q.empty()) {
                    int64_t curr = q.front();
                    q.pop();
                    for (auto neighbor : adjacency_list.at(curr)) {
                        if (visited.find(neighbor) == visited.end()) {
                            visited.insert(neighbor);
                            q.push(neighbor);
                        }
                    }
                }
            }
        }
        return components_count;
    }

}

void CyclomaticComplexityFunction::Execute(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
    auto &global_state = data.global_state->Cast<ExperimentalGlobalState>();
    if (global_state.finished) {
        return;
    }
    auto &bind_data = data.bind_data->Cast<CyclomaticComplexityBindData>();
    auto duckpgq_state = GetDuckPGQState(context);
    if (!duckpgq_state) {
        throw BinderException("Internal state of DuckPGQ isn't initialized in current session.");
    }
    auto property_graph = duckpgq_state->GetPropertyGraph(bind_data.graph_name);
    if (!property_graph) {
        throw BinderException("Property Graph named '" + bind_data.graph_name + "' not found.");
    }
    std::unordered_set<int64_t> all_vertices;
    std::unordered_map<int64_t, std::vector<int64_t>> adjacency_list;
    int64_t edge_count = ExtractGraphData(context, property_graph, all_vertices, adjacency_list);
    int64_t components_count = CalcComponentsCount(all_vertices, adjacency_list);
    int64_t vertex_count = all_vertices.size();
    // Complexity: M = E - V + 2P
    int64_t complexity = edge_count - vertex_count + (2 * components_count);
    // Result
    output.SetCardinality(1);
    output.SetValue(0, 0, Value::BIGINT(vertex_count));
    output.SetValue(1, 0, Value::BIGINT(edge_count));
    output.SetValue(2, 0, Value::BIGINT(components_count));
    output.SetValue(3, 0, Value::BIGINT(complexity));
    global_state.finished = true;
}

//------------------------------------------------------------------------------
// Register functions
//------------------------------------------------------------------------------

void CoreTableFunctions::RegisterExperimentalTableFunction(ExtensionLoader &loader) {
    loader.RegisterFunction(GetGraphNameFunction());
    loader.RegisterFunction(CountNodesFunction());
    loader.RegisterFunction(CyclomaticComplexityFunction());
}

}