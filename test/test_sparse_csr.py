import torch

# NOTE: These tests are inspired from test_sparse.py and may duplicate some behaviour.
# Need to think about merging them both sometime down the line.

# Major differences between testing of CSR and COO is that we don't need to test CSR
# for coalesced/uncoalesced behaviour.

# TODO: remove this global setting
# Sparse tests use double as the default dtype
torch.set_default_dtype(torch.double)

import itertools
import functools
import random
import unittest
import operator
import numpy as np
import math
from collections import defaultdict
from torch.testing._internal.common_utils import TestCase, run_tests, load_tests

# load_tests from torch.testing._internal.common_utils is used to automatically filter tests for
# sharding on sandcastle. This line silences flake warnings
load_tests = load_tests

class TestSparseCSR(TestCase):
    def gen_sparse_csr(self, shape, nnz, fill_value=0):
        total_values = functools.reduce(operator.mul, shape, 1)
        dense = np.random.randn(total_values)
        fills = random.sample(list(range(total_values)), total_values-nnz)

        for f in fills:
            dense[f] = fill_value
        dense = torch.from_numpy(dense.reshape(shape))

        return dense.to_sparse_csr(fill_value)
    
    def setUp(self):
        # These parameters control the various ways we can run the test.
        # We will subclass and override this method to implement CUDA
        # tests
        self.is_cuda = False
        self.device = 'cpu'
    
    def test_csr_layout(self):
        self.assertEqual(str(torch.sparse_csr), 'torch.sparse_csr')
        self.assertEqual(type(torch.sparse_csr), torch.layout)

    def test_sparse_csr_from_dense(self):
        sp = torch.tensor([[1, 2], [3, 4]]).to_sparse_csr(-999)
        self.assertEqual(torch.tensor([0, 2, 4], dtype=torch.int32), sp.crow_indices())
        self.assertEqual(torch.tensor([0, 1, 0, 1], dtype=torch.int32), sp.col_indices())
        self.assertEqual(torch.tensor([1, 2, 3, 4], dtype=torch.int64), sp.values())

        dense = torch.tensor([[4, 5, 0], [0, 0, 0], [1, 0, 0]])
        sparse = dense.to_sparse_csr()
        self.assertEqual(torch.tensor([0, 2, 2, 3], dtype=torch.int32), sparse.crow_indices())
        self.assertEqual(torch.tensor([0, 1, 0], dtype=torch.int32), sparse.col_indices())
        self.assertEqual(torch.tensor([4, 5, 1]), sparse.values())

        dense = torch.tensor([[0, 0, 0], [0, 0, 1], [1, 0, 0]])
        sparse = dense.to_sparse_csr()
        self.assertEqual(torch.tensor([0, 0, 1, 2], dtype=torch.int32), sparse.crow_indices())
        self.assertEqual(torch.tensor([2, 0], dtype=torch.int32), sparse.col_indices())
        self.assertEqual(torch.tensor([1, 1]), sparse.values())

        dense = torch.tensor([[2, 2, 2], [2, 2, 2], [2, 2, 2]])
        sparse = dense.to_sparse_csr()
        self.assertEqual(torch.tensor([0, 3, 6, 9], dtype=torch.int32), sparse.crow_indices())
        self.assertEqual(torch.tensor([0, 1, 2] * 3, dtype=torch.int32), sparse.col_indices())
        self.assertEqual(torch.tensor([2] * 9), sparse.values())


    def test_dense_convert(self):
        size = (5, 5)
        dense = torch.randn(size)
        sparse = dense.to_sparse_csr(-999)
        self.assertEqual(sparse.to_dense(), dense)
        
        size = (4, 6)
        dense = torch.randn(size)
        sparse = dense.to_sparse_csr(-999)
        self.assertEqual(sparse.to_dense(), dense)

    def test_dense_convert_fill_value(self):
        dense = torch.tensor([[1, 2, 3], [2, 2, 2], [4, 5, 2]])
        sparse = dense.to_sparse_csr(2)
        self.assertEqual(torch.tensor([0, 2, 2, 4], dtype=torch.int32), sparse.crow_indices())
        self.assertEqual(torch.tensor([0, 2, 0, 1], dtype=torch.int32), sparse.col_indices())
        self.assertEqual(torch.tensor([1, 3, 4, 5]), sparse.values())

    def test_dense_convert_error(self):
        size = (4, 2, 4)
        dense = torch.randn(size)

        with self.assertRaisesRegex(RuntimeError, "Only 2D"):
            sparse = dense.to_sparse_csr(-99)

    def test_csr_matvec(self):
        side = 100
        csr = self.gen_sparse_csr((side, side), 1000)
        vec = torch.randn(side, dtype=torch.double)

        res = csr.matmul(vec)
        expected = csr.to_dense().matmul(vec)

        self.assertEqual(res, expected)

        bad_vec = torch.randn(side + 10, dtype=torch.double)
        with self.assertRaisesRegex(RuntimeError, "mv: expected"):
            csr.matmul(bad_vec)

    def test_coo_csr_conversion(self):
        size = (5, 5)
        dense = torch.randn(size)
        coo_sparse = dense.to_sparse()
        csr_sparse = coo_sparse.to_sparse_csr()

        self.assertEqual(csr_sparse.to_dense(), dense)

if __name__ == '__main__':
    run_tests()
