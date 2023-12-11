# Copyright (c) Meta Platforms, Inc. and affiliates
from typing import cast, Dict, List, Tuple

import torch
import torch.distributed._tensor.api as dtensor
from torch.distributed._tensor.device_mesh import DeviceMesh
from torch.distributed._tensor.placement_types import (
    _Partial,
    DTensorSpec,
    Placement,
    Replicate,
    Shard,
)


def redistribute_local_tensor(
    local_tensor: torch.Tensor,
    current_spec: DTensorSpec,
    target_spec: DTensorSpec,
) -> torch.Tensor:
    """
    This redistribute the local tensor (torch.Tensor) from the current DTensorSpec to
    the target DTensorSpec, which involves the necessary collective calls to transform
    the local shard of the DTensor from its current spec to the target spec.
    """

    if current_spec.mesh != target_spec.mesh:
        # TODO: alltoall/permute reshuffling to change device_mesh if they are not the same
        raise NotImplementedError("Cross device mesh comm not supported yet!")

    new_local_tensor = None
    device_mesh = current_spec.mesh

    my_coordinate = device_mesh.get_coordinate()

    if my_coordinate is None:
        # if rank is not part of mesh, we skip redistribute and simply return local_tensor,
        # which should be an empty tensor
        return local_tensor

    current_placements = current_spec.placements
    target_placements = target_spec.placements

    # logical shape defining the logic tensor shape on the mesh dimension
    initial_logical_shape = list(current_spec.shape)
    mesh_dims_to_logical_shape = [initial_logical_shape]

    for i in range(len(current_placements) - 1):
        current_logical_shape = mesh_dims_to_logical_shape[i]
        if current_placements[i].is_shard():
            mesh_dim_size = device_mesh.size(mesh_dim=i)
            shard_placement = cast(Shard, current_placements[i])
            local_shard_size, _ = shard_placement._local_shard_size_on_dim(
                current_logical_shape[shard_placement.dim],
                mesh_dim_size,
                my_coordinate[i],
            )
            new_logical_shape = list(current_logical_shape)
            new_logical_shape[shard_placement.dim] = local_shard_size
            mesh_dims_to_logical_shape.append(new_logical_shape)
        else:
            mesh_dims_to_logical_shape.append(current_logical_shape)

    inner_first_sharding_combs = reversed(
        list(enumerate(zip(current_placements, target_placements)))
    )

    for i, (current, target) in inner_first_sharding_combs:
        num_chunks = device_mesh.size(mesh_dim=i)

        if current == target:
            # short cut, just use the original local tensor
            new_local_tensor = local_tensor
            continue

        if target.is_replicate():
            # Case 1: target is Replicate
            if current.is_partial():
                partial_spec = cast(_Partial, current)
                new_local_tensor = partial_spec._to_replicate(
                    local_tensor, device_mesh, i
                )
            elif current.is_shard():
                current_placement = cast(Shard, current)
                new_local_tensor = current_placement._to_replicate_tensor(
                    local_tensor, device_mesh, i, mesh_dims_to_logical_shape[i]
                )
            else:
                raise RuntimeError(
                    f"redistribute from {current_placements} to {target_placements} not supported yet"
                )
        elif target.is_shard():
            # Case 2: target is Shard
            target_placement = cast(Shard, target)
            target_dim = target_placement.dim
            if current.is_partial():
                partial_spec = cast(_Partial, current)
                new_local_tensor = partial_spec._to_shard(
                    local_tensor, device_mesh, i, target_placement
                )
            elif current.is_replicate():
                # split the tensor and return the corresponding cloned local shard
                shards, _ = target_placement._split_tensor(
                    local_tensor,
                    num_chunks,
                    with_padding=False,
                    contiguous=False,
                )
                new_local_tensor = shards[my_coordinate[i]].clone()
            else:
                # NOTE: we don't support this case efficiently yet, the fallback path we are going here is
                # to decompose Shard(0) -> Shard(1) into Shard(0) -> Replicate -> Shard(1)
                # TODO: enable this with all_to_all
                assert (
                    current.is_shard()
                ), f"Current placement should be shard but found {current}"
                shard_spec = cast(Shard, current)
                if shard_spec.dim != target_placement.dim:
                    new_local_tensor = shard_spec._to_replicate_tensor(
                        local_tensor, device_mesh, i, mesh_dims_to_logical_shape[i]
                    )
                    new_local_tensor = target_placement._split_tensor(
                        local_tensor,
                        num_chunks,
                        with_padding=False,
                        contiguous=False,
                    )[my_coordinate[i]]
        elif target.is_partial():
            if current.is_replicate():
                # For replicate -> partial, we perform division to num of chunks and generate
                # parial, and recover it back when pending sum get cleared.
                new_local_tensor = local_tensor / num_chunks
            else:
                raise RuntimeError(
                    f"redistribute from {current_placements} to {target_placements} not supported yet"
                )

        assert new_local_tensor is not None
        local_tensor = new_local_tensor

    assert new_local_tensor is not None, "redistribute failed!"

    return new_local_tensor


class Redistribute(torch.autograd.Function):
    @staticmethod
    def forward(  # type: ignore[override]
        # pyre-fixme[2]: Parameter must be annotated.
        ctx,
        input: "dtensor.DTensor",
        device_mesh: DeviceMesh,
        placements: Tuple[Placement, ...],
    ):
        current_spec = input._spec
        ctx.current_spec = current_spec
        target_spec = DTensorSpec(
            device_mesh, placements, tensor_meta=input._spec.tensor_meta
        )

        local_tensor = input._local_tensor
        output = redistribute_local_tensor(local_tensor, current_spec, target_spec)

        return dtensor.DTensor(
            output,
            device_mesh,
            target_spec.placements,
            shape=input.shape,
            dtype=input.dtype,
            requires_grad=input.requires_grad,
            stride=input.stride(),
        )

    @staticmethod
    def backward(ctx, grad_output: "dtensor.DTensor"):  # type: ignore[override]
        previous_spec = ctx.current_spec
        # When we run backward pass of redistribute (i.e. manual redistribute from
        # user code instead of torch_dispatch), we scan first and see if we need
        # to change the target placement for one special case:
        #   replicate -> partial.
        # In this case we keep the grad as replicate, this is because we don't
        # want to convert the replicated gradients back to partial, although
        # that's logically conform with the same layout, converting the gradients
        # back to partial is actually useless as you would have to do reduce later
        # which would be more expensive than keeping it replicate! For this reason,
        # we keep the replicate grad here.
        # TODO: see if this make sense for all cases.
        current_spec = grad_output._spec

        target_placements: List[Placement] = []
        for current, target in zip(current_spec.placements, previous_spec.placements):
            if not current.is_partial() and target.is_partial():
                # keep target placement to replicate instead of partial in this case
                target_placements.append(Replicate())
            else:
                target_placements.append(target)
        target_spec = DTensorSpec(
            previous_spec.mesh,
            tuple(target_placements),
            tensor_meta=previous_spec.tensor_meta,
        )

        local_tensor = grad_output._local_tensor
        output = redistribute_local_tensor(local_tensor, current_spec, target_spec)
        output_dtensor = dtensor.DTensor(
            output,
            target_spec.mesh,
            target_spec.placements,
            shape=grad_output.shape,
            dtype=grad_output.dtype,
            requires_grad=grad_output.requires_grad,
            stride=grad_output.stride(),
        )

        return (
            output_dtensor,
            None,
            None,
        )
