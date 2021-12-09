#include "lazy_tensors/computation_client/metrics_reader.h"

#include <torch/csrc/lazy/core/metrics.h>
#include <torch/csrc/lazy/core/util.h>

#include <cmath>
#include <sstream>

namespace lazy_tensors {
namespace metrics_reader {
namespace {

struct Percentile {
  enum class UnitOfMeaure {
    kNumber,
    kTime,
    kBytes,
  };
  struct Point {
    double percentile = 0.0;
    double value = 0.0;
  };

  UnitOfMeaure unit_of_measure = UnitOfMeaure::kNumber;
  uint64_t start_nstime = 0;
  uint64_t end_nstime = 0;
  double min_value = NAN;
  double max_value = NAN;
  double mean = NAN;
  double stddev = NAN;
  size_t num_samples = 0;
  size_t total_samples = 0;
  double accumulator = NAN;
  std::vector<Point> points;
};

struct Metric {
  c10::optional<Percentile> percentile;
  c10::optional<int64_t> int64_value;
};

struct MetricFnInfo {
  torch::lazy::MetricReprFn repr_fn;
  double scale;
};

MetricFnInfo GetMetricRenderInfo(const Percentile& percentile) {
  switch (percentile.unit_of_measure) {
    default:
    case Percentile::UnitOfMeaure::kNumber:
      return {torch::lazy::MetricFnValue, 1.0};
    case Percentile::UnitOfMeaure::kTime:
      return {torch::lazy::MetricFnTime, 1e6};
    case Percentile::UnitOfMeaure::kBytes:
      return {torch::lazy::MetricFnBytes, 1.0};
  }
}

std::string CreateXrtMetricReport() {
  //TODO(whc) I _think_ this (xrt_metrics) is empty currently
  // and all the metrics we care about come from metrics::CreateMetricReport,
  // need to confirm


  // auto xrt_metrics = ComputationClient::Get()->GetMetrics();
  std::stringstream ss;
  // for (auto& name_metric : xrt_metrics) {
  //   if (name_metric.second.percentile) {
  //     const Percentile& percentile = *name_metric.second.percentile;
  //     MetricFnInfo minfo = GetMetricRenderInfo(percentile);
  //     ss << "Metric: " << name_metric.first << std::endl;
  //     ss << "  TotalSamples: " << percentile.total_samples << std::endl;
  //     ss << "  Accumulator: "
  //        << minfo.repr_fn(percentile.accumulator * minfo.scale) << std::endl;
  //     ss << "  Mean: " << minfo.repr_fn(percentile.mean * minfo.scale)
  //        << std::endl;
  //     ss << "  StdDev: " << minfo.repr_fn(percentile.stddev * minfo.scale)
  //        << std::endl;

  //     uint64_t delta_time = percentile.end_nstime - percentile.start_nstime;
  //     if (delta_time > 0) {
  //       double count_sec = 1e6 * (static_cast<double>(percentile.num_samples) /
  //                                 (delta_time / 1000.0));
  //       ss << "  Rate: " << count_sec << " / second" << std::endl;
  //     }

  //     ss << "  Percentiles: ";
  //     for (size_t i = 0; i < percentile.points.size(); ++i) {
  //       if (i > 0) {
  //         ss << "; ";
  //       }
  //       ss << percentile.points[i].percentile
  //          << "%=" << minfo.repr_fn(percentile.points[i].value * minfo.scale);
  //     }
  //     ss << std::endl;
  //   } else if (name_metric.second.int64_value) {
  //     ss << "Counter: " << name_metric.first << std::endl;
  //     ss << "  Value: " << *name_metric.second.int64_value << std::endl;
  //   }
  // }
  return ss.str();
}

}  // namespace

std::string CreateMetricReport() {
  return torch::lazy::CreateMetricReport() + CreateXrtMetricReport();
}

}  // namespace metrics_reader
}  // namespace lazy_tensors
