#pragma once
#include <torch/csrc/jit/mobile/module.h>

#include <istream>
#include <memory>

#include <caffe2/serialize/file_adapter.h>

namespace torch {
namespace jit {
using caffe2::serialize::FileAdapter;
using caffe2::serialize::IStreamAdapter;
using caffe2::serialize::ReadAdapterInterface;

TORCH_API mobile::Module _load_for_mobile(
    std::istream& in,
    c10::optional<at::Device> device = c10::nullopt);

TORCH_API mobile::Module _load_for_mobile(
    const std::string& filename,
    c10::optional<at::Device> device = c10::nullopt);

TORCH_API mobile::Module _load_for_mobile(
    std::unique_ptr<ReadAdapterInterface> rai,
    c10::optional<c10::Device> device = c10::nullopt);

TORCH_API std::map<std::string, at::Tensor> _load_mobile_data(
    std::istream& in,
    c10::optional<at::Device> device = c10::nullopt);

TORCH_API std::map<std::string, at::Tensor> _load_mobile_data(
    const std::string& filename,
    c10::optional<at::Device> device = c10::nullopt);

TORCH_API std::map<std::string, at::Tensor> _load_mobile_data(
    std::unique_ptr<ReadAdapterInterface> rai,
    c10::optional<c10::Device> device = c10::nullopt);
} // namespace jit
} // namespace torch
