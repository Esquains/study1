#! /usr/bin/env python

import torch
import torch.utils.benchmark as benchmark
from torch import nn
from torch.ao.pruning import WeightNormSparsifier
from itertools import product

class Model(nn.Module):
    def __init__(self, m, k):
        super().__init__()
        # transposed so reversed
        self.linear = nn.Linear(k, m)

    def forward(self, x):
        return self.linear(x)


class CUTLASSSparseLinear(nn.Module):
    def __init__(self, linear_model, n):
        super().__init__()

        m, k = linear_model.weight.shape
        mask = linear_model.weight.data != 0

        self.weight_tensor = linear_model.weight.data.masked_select(mask).view(m, k // 2)
        self.bias_tensor = linear_model.bias.repeat((n, 1)).T.contiguous()
        self.mask = mask
        self.meta = None

    def forward(self, x):
        if self.mask is not None:
            prod, self.meta = torch._cutlass_linear(self.weight_tensor, x.T, self.bias_tensor, self.mask)
            self.mask = None
        else:
            prod, _ = torch._cutlass_linear(self.weight_tensor, x.T, self.bias_tensor, self.meta)
        return prod.T


def get_linear(m, k, n):
    model = Model(m, k)
    model.half()
    model.cuda()

    pruner = WeightNormSparsifier(sparsity_level=1.0, sparse_block_shape=(1, 4), zeros_per_block=2)
    pruner.prepare(model, [{"tensor_fqn": "linear.weight"}])
    pruner.step()
    pruner.squash_mask()

    input_tensor = torch.randn(n, k, device=device, dtype=dtype)
    sparse_linear = CUTLASSSparseLinear(model.linear, n)
    dense_linear = model.linear

    for i in range(5):
        input_tensor = torch.randn(n, k, device=device, dtype=dtype)
        sparse_output = sparse_linear(input_tensor)
        dense_output = dense_linear(input_tensor)
        assert torch.allclose(sparse_output, dense_output, rtol=1e-3, atol=1e-3)

    return input_tensor, sparse_linear, dense_linear


results = []
device = "cuda"
dtype = torch.float16
torch.set_printoptions(precision=3, threshold=None, edgeitems=4, linewidth=460, profile=None, sci_mode=False)

shapes = [
    # distilbert shapes
    (768, 3072, 768),
    (3072, 768, 3072),
    # jiecao shapes
    (1024, 1536, 2048),
    (1024, 9408, 2048),
    (1024, 3200, 2048),
    (1024, 256, 9472),
    (1024, 10240, 256),
    (1024, 256, 12608),
    (1024, 2560, 1024),
    (1024, 512, 10240),
    (1024, 10240, 512),
    (1024, 2048, 1024),
    (1024, 512, 512),
    (1024, 1024, 1024),
    (1024, 2048, 2048),
    (2048, 1536, 2048),
    (2048, 9408, 2048),
    (2048, 3200, 2048),
    (2048, 256, 9472),
    (2048, 10240, 256),
    (2048, 256, 12608),
    (2048, 2560, 1024),
    (2048, 512, 10240),
    (2048, 10240, 512),
    (2048, 2048, 1024),
    (2048, 512, 512),
    (2048, 1024, 1024),
    (2048, 2048, 2048),
]
batch_sizes = [1]  # [1, 4, 16, 64, 256]

for batch_size, (m, k, n) in product(batch_sizes, shapes):
    # label and sub_label are the rows
    # description is the column
    label = 'CUTLASS based Linear vs. nn.Linear'
    sub_label = f'batch_size: {batch_size:2d} | m:{m:6d} | k:{k:6d} | n:{n:6d}'

    try:
        input_tensor, sparse_linear, dense_linear = get_linear(m, k, n)

        result = benchmark.Timer(
            stmt='sparse_linear(input_tensor)',
            globals={'input_tensor': input_tensor, 'sparse_linear': sparse_linear},
            label=label,
            sub_label=sub_label,
            description='sparse latency',
        )
        results.append(result.blocked_autorange(min_run_time=1))

        result = benchmark.Timer(
            stmt='dense_linear(input_tensor)',
            globals={'input_tensor': input_tensor, 'dense_linear': dense_linear},
            label=label,
            sub_label=sub_label,
            description='dense latency',
        )
        results.append(result.blocked_autorange(min_run_time=1))

    except Exception as e:
        print(sub_label)
        print(e)

compare = benchmark.Compare(results)
compare.colorize(rowwise=True)
compare.print()
