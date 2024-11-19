import json
import os
import sys

from benchmark_base import BenchmarkBase

import torch


class Benchmark(BenchmarkBase):
    N = 200

    def name(self):
        return "symint_sum"

    def description(self):
        return "see https://docs.google.com/document/d/11xJXl1etSmefUxPiVyk885e0Dl-4o7QwxYcPiMIo2iY/edit"

    def _prepare_once(self):
        torch._dynamo.config.capture_scalar_outputs = True
        torch.manual_seed(0)

        self.splits = torch.randint(10, (self.N,))

    def _prepare(self):
        torch._dynamo.reset()

    def _work(self):
        @torch.compile(fullgraph=True)
        def f(a):
            xs = a.tolist()
            y = sum(xs)
            return torch.tensor(y)

        f(self.splits)

    def _write_to_json(self, output_dir: str):
        records = []
        for entry in self.results:
            metric_name = entry[1]
            value = entry[2]

            if not metric_name or value is None:
                continue

            records.append(
                {
                    "benchmark": (
                        "pr_time_benchmarks",
                        "",  # training or inference, not use
                        "",  # dtype, not use
                        {
                            "device": "cpu",
                            "description": self.description(),
                        },
                    ),
                    "model": (
                        self.name(),
                        "symint_sum",
                        "",  # backend, not use
                    ),
                    "metric": (
                        metric_name,
                        [value],
                    ),
                }
            )

        with open(os.path.join(output_dir, f"{self.name()}.json"), "w") as f:
            json.dump(records, f)


def main():
    result_path = sys.argv[1]
    Benchmark().enable_compile_time_instruction_count().collect_all().append_results(
        result_path
    )


if __name__ == "__main__":
    main()
