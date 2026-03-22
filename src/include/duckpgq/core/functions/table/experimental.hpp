#pragma once

#include "duckpgq/common.hpp"

#include "duckpgq/core/utils/duckpgq_utils.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"

namespace duckdb {

// get graph name
struct GetGraphNameBindData : public TableFunctionData {
    string graph_name;
    bool graph_exists = false;
};

struct GetGraphNameGlobalState : public GlobalTableFunctionState {
    bool finished = false;
};

class GetGraphNameFunction : public TableFunction {
public:
    GetGraphNameFunction() {
        name = "get_graph_name";
        arguments.push_back(LogicalType::VARCHAR);
        init_global = InitGlobal;
        bind = Bind;
        function = Main;
    }

    static unique_ptr<GlobalTableFunctionState> InitGlobal(ClientContext &context, TableFunctionInitInput &input) {
        return make_uniq<GetGraphNameGlobalState>();
    }

    static unique_ptr<FunctionData> Bind(ClientContext &context, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<string> &names) {
        return_types.push_back(LogicalType::VARCHAR);
        names.emplace_back("graph_name");
        auto result = make_uniq<GetGraphNameBindData>();
        string graph_name = StringValue::Get(input.inputs[0]);
        auto pgq_state = GetDuckPGQState(context);
        auto graph_it = pgq_state->registered_property_graphs.find(graph_name);
        if (graph_it != pgq_state->registered_property_graphs.end()) {
            result->graph_exists = true;
            result->graph_name = graph_it->first;
        }
        return std::move(result);
    }

    static void Main(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
        auto &state = data.global_state->Cast<GetGraphNameGlobalState>();
        auto &bind_data = data.bind_data->Cast<GetGraphNameBindData>();
        if (state.finished) {
          output.SetCardinality(0);
          return;
        }
        output.SetValue(0, 0, bind_data.graph_exists ? Value(bind_data.graph_name) : Value(LogicalType::VARCHAR));
        output.SetCardinality(1);
        state.finished = true;
    }
};

// count graph nodes
struct CountNodesBindData : public TableFunctionData {
    int64_t node_count = 0;
    bool graph_exists = false;
};

struct CountNodesGlobalState : public GlobalTableFunctionState {
    bool finished = false;
};

class CountNodesFunction : public TableFunction {
public:
    CountNodesFunction() {
        name = "count_nodes";
        arguments.push_back(LogicalType::VARCHAR);
        init_global = InitGlobal;
        bind = Bind;
        function = Main;
    }

    static unique_ptr<FunctionData> Bind(ClientContext &context, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<string> &names) {
        return_types.push_back(LogicalType::BIGINT);
        names.emplace_back("node_count");
        auto result = make_uniq<CountNodesBindData>();
        string graph_name = StringValue::Get(input.inputs[0]);
        auto pgq_state = GetDuckPGQState(context);
        auto graph_it = pgq_state->registered_property_graphs.find(graph_name);
        if (graph_it != pgq_state->registered_property_graphs.end()) {
            result->graph_exists = true;
            auto graph = dynamic_cast<CreatePropertyGraphInfo*>(graph_it->second.get());
            auto &catalog = Catalog::GetCatalog(context, INVALID_CATALOG);
            for (auto &vertex_table : graph->vertex_tables) {
                auto &table_entry = catalog.GetEntry<TableCatalogEntry>(context, DEFAULT_SCHEMA, vertex_table->table_name);
                result->node_count += table_entry.GetStorage().GetTotalRows();
            }
        }
        return std::move(result);
    }

    static unique_ptr<GlobalTableFunctionState> InitGlobal(ClientContext &context, TableFunctionInitInput &input) {
        return make_uniq<CountNodesGlobalState>();
    }

    static void Main(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
        auto &state = data.global_state->Cast<CountNodesGlobalState>();
        auto &bind_data = data.bind_data->Cast<CountNodesBindData>();
        if (state.finished) {
            output.SetCardinality(0);
            return;
        }
        output.SetValue(0, 0, bind_data.graph_exists ? Value::BIGINT(bind_data.node_count) : Value(LogicalType::BIGINT));
        output.SetCardinality(1);
        state.finished = true;
    }
};

}