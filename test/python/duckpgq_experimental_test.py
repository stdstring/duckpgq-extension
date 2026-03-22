import duckdb
import os
import pytest

@pytest.fixture
def duckdb_conn():
    extension_binary = os.getenv('DUCKPGQ_EXTENSION_BINARY_PATH')
    if extension_binary == '':
        raise Exception('Please make sure the `DUCKPGQ_EXTENSION_BINARY_PATH` is set to run the python tests')
    conn = duckdb.connect('', config={'allow_unsigned_extensions': 'true'})
    conn.execute(f"load '{extension_binary}'")
    return conn

def test_experimental_func(duckdb_conn):
    for function_name in ["get_graph_name", "count_nodes"]:
        duckdb_conn.execute(f"SELECT function_name FROM duckdb_functions() WHERE function_name = '{function_name}';")
        res = duckdb_conn.fetchall()
        assert len(res) > 0

def test_get_graph_name(duckdb_conn):
    duckdb_conn.execute("CREATE TABLE person (id INTEGER PRIMARY KEY, name VARCHAR)")
    duckdb_conn.execute("CREATE TABLE knows (from_id INTEGER, to_id INTEGER)")
    duckdb_conn.execute("-CREATE PROPERTY GRAPH social_graph VERTEX TABLES ( person ) EDGE TABLES ( knows SOURCE KEY (from_id) REFERENCES person (id) DESTINATION KEY (to_id) REFERENCES person (id) )")
    duckdb_conn.execute("SELECT graph_name FROM get_graph_name('social_graph')")
    res = duckdb_conn.fetchall()
    assert len(res) == 1
    assert res[0][0] == "social_graph"
    duckdb_conn.execute("SELECT graph_name FROM get_graph_name('unknown_graph')")
    res = duckdb_conn.fetchall()
    assert len(res) == 1
    assert res[0][0] is None

def test_count_nodes(duckdb_conn):
    duckdb_conn.execute("CREATE TABLE person (id INTEGER PRIMARY KEY, name VARCHAR)")
    duckdb_conn.execute("CREATE TABLE knows (from_id INTEGER, to_id INTEGER)")
    duckdb_conn.execute("INSERT INTO person VALUES (1, 'Alice'), (2, 'Bob'), (3, 'Charlie'), (4, 'David'), (5, 'Eve')")
    duckdb_conn.execute("INSERT INTO knows VALUES (1, 2), (3, 4), (4, 3)")
    duckdb_conn.execute("-CREATE PROPERTY GRAPH social_graph VERTEX TABLES ( person ) EDGE TABLES ( knows SOURCE KEY (from_id) REFERENCES person (id) DESTINATION KEY (to_id) REFERENCES person (id) )")
    duckdb_conn.execute("CREATE TABLE city (id INTEGER PRIMARY KEY, name VARCHAR)")
    duckdb_conn.execute("INSERT INTO city VALUES (1, 'Moscow'), (2, 'London')")
    duckdb_conn.execute("-CREATE PROPERTY GRAPH city_graph VERTEX TABLES ( city )")
    duckdb_conn.execute("SELECT node_count FROM count_nodes('social_graph')")
    res = duckdb_conn.fetchall()
    assert len(res) == 1
    assert res[0][0] == 5
    duckdb_conn.execute("SELECT node_count FROM count_nodes('city_graph')")
    res = duckdb_conn.fetchall()
    assert len(res) == 1
    assert res[0][0] == 2
    duckdb_conn.execute("SELECT node_count FROM count_nodes('unknown_graph')")
    res = duckdb_conn.fetchall()
    assert len(res) == 1
    assert res[0][0] is None