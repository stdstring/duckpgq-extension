#pragma once

#include "duckpgq/common.hpp"
#include "duckpgq/core/utils/duckpgq_utils.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/storage/table/scan_state.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/transaction/duck_transaction.hpp"
#include "duckdb/storage/data_table.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

// Подключаем родной заголовочный файл расширения duckpgq
#include "duckpgq_extension.hpp"

namespace duckdb {

struct ExperimentalGlobalState : GlobalTableFunctionState {
    bool finished = false;
};

// get graph name
struct GetGraphNameBindData : TableFunctionData {
    string graph_name;
    bool graph_exists = false;
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

    static unique_ptr<GlobalTableFunctionState> InitGlobal(ClientContext &context,
                                                           TableFunctionInitInput &input) {
        return make_uniq<ExperimentalGlobalState>();
    }

    static unique_ptr<FunctionData> Bind(ClientContext &context,
                                         TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types,
                                         vector<string> &names) {
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

    static void Main(ClientContext &context,
                     TableFunctionInput &data,
                     DataChunk &output) {
        auto &state = data.global_state->Cast<ExperimentalGlobalState>();
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
struct CountNodesBindData : TableFunctionData {
    int64_t node_count = 0;
    bool graph_exists = false;
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

    static unique_ptr<GlobalTableFunctionState> InitGlobal(ClientContext &context,
                                                           TableFunctionInitInput &input) {
        return make_uniq<ExperimentalGlobalState>();
    }

    static unique_ptr<FunctionData> Bind(ClientContext &context,
                                         TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types,
                                         vector<string> &names) {
        return_types.push_back(LogicalType::BIGINT);
        names.emplace_back("node_count");
        auto result = make_uniq<CountNodesBindData>();
        const string graph_name = StringValue::Get(input.inputs[0]);
        const auto pgq_state = GetDuckPGQState(context);
        const auto graph_it = pgq_state->registered_property_graphs.find(graph_name);
        if (graph_it != pgq_state->registered_property_graphs.end()) {
            result->graph_exists = true;
            const auto graph = dynamic_cast<CreatePropertyGraphInfo*>(graph_it->second.get());
            auto &catalog = Catalog::GetCatalog(context, INVALID_CATALOG);
            for (const auto &vertex_table : graph->vertex_tables) {
                auto &table_entry = catalog.GetEntry<TableCatalogEntry>(context, DEFAULT_SCHEMA, vertex_table->table_name);
                result->node_count += static_cast<int64_t>(table_entry.GetStorage().GetTotalRows());
            }
        }
        return std::move(result);
    }

    static void Main(ClientContext &context,
                     TableFunctionInput &data,
                     DataChunk &output) {
        auto &state = data.global_state->Cast<ExperimentalGlobalState>();
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

struct CyclomaticComplexityBindData : FunctionData {
    std::string graph_name;

    explicit CyclomaticComplexityBindData(std::string const &name) : graph_name(name) {}

    unique_ptr<FunctionData> Copy() const override {
        return make_uniq<CyclomaticComplexityBindData>(graph_name);
    }

    bool Equals(const FunctionData &other) const override {
        auto &other_data = other.Cast<CyclomaticComplexityBindData>();
        return graph_name == other_data.graph_name;
    }
};

class CyclomaticComplexityFunction : public TableFunction {
public:
    CyclomaticComplexityFunction() {
        name = "cyclomatic_complexity";
        arguments.push_back(LogicalType::VARCHAR);
        bind = Bind;
        function = Execute;
        init_global = Init;
    }

    static unique_ptr<GlobalTableFunctionState> Init(ClientContext &context,
                                                     TableFunctionInitInput &input) {
        return make_uniq<ExperimentalGlobalState>();
    }

    static unique_ptr<FunctionData> Bind(ClientContext &context,
                                         TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types,
                                         vector<string> &names) {
        if (input.inputs.empty()) {
            throw BinderException("graph name absent");
        }
        std::string graph_name = StringValue::Get(input.inputs[0]);
        names.emplace_back("vertices");
        return_types.emplace_back(LogicalType::BIGINT);
        names.emplace_back("edges");
        return_types.emplace_back(LogicalType::BIGINT);
        names.emplace_back("components");
        return_types.emplace_back(LogicalType::BIGINT);
        names.emplace_back("complexity");
        return_types.emplace_back(LogicalType::BIGINT);
        return make_uniq<CyclomaticComplexityBindData>(graph_name);
    }
    static void Execute(ClientContext &context, TableFunctionInput &data, DataChunk &output);
};

}