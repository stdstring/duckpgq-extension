import duckdb
import os
import unittest

class DuckpgqTestCase(unittest.TestCase):

    def setUp(self):
        self.duckdb_conn = None
        extension_binary = os.getenv('DUCKPGQ_EXTENSION_BINARY_PATH')
        if extension_binary == '':
            raise Exception('Please make sure the `DUCKPGQ_EXTENSION_BINARY_PATH` is set to run the python tests')
        self.duckdb_conn = duckdb.connect('', config={'allow_unsigned_extensions': 'true'})
        self.duckdb_conn.execute(f"load '{extension_binary}'")

    def test_property_graph(self):
        self.duckdb_conn.execute("CREATE TABLE foo(i bigint)")
        self.duckdb_conn.execute("INSERT INTO foo(i) VALUES (1)")
        self.duckdb_conn.execute("-CREATE PROPERTY GRAPH t VERTEX TABLES (foo);")
        self.duckdb_conn.execute("-FROM GRAPH_TABLE(t MATCH (f:foo))")
        res = self.duckdb_conn.fetchall()
        assert res[0][0] == 1