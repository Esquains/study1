# mypy: ignore-errors

# noqa: F401, E501
# This is an auto-generated file. Please do not modify it by hand.
# To re-generate, run:
# cd ~/pytorch && python torchgen/fuse/gen_patterns.py

import torch
import torch._inductor

aten = torch.ops.aten
prims = torch.ops.prims

from torch._inductor.pattern_matcher import (
   Arg,
   CallFunction,
   CallFunctionVarArgs,
   CallMethod,
   CallMethodVarArgs,
   CallModule,
   CallModuleVarArgs,
   ExclusiveKeywordArg,
   Ignored,
   KeywordArg,
   ListOf,
   MultiOutputPattern,
   PatternExpr,
   RepeatedExpr,
   _TargetArgsExpr,
   _TargetExpr,
   _TargetExprVarArgs,
)
rand_default = CallFunction(aten.rand.default, Ignored(), dtype=Ignored(), device=Ignored(), pin_memory=False)
gt_Scalar = CallFunction(aten.gt.Scalar, rand_default, KeywordArg('dropout_p'), _users=2)
expand_default = CallFunction(aten.expand.default, KeywordArg('query'), Ignored())
view_default = CallFunction(aten.view.default, expand_default, Ignored(), _users=2)
permute_default = CallFunction(aten.permute.default, KeywordArg('key'), Ignored())
expand_default_1 = CallFunction(aten.expand.default, permute_default, Ignored())
view_default_1 = CallFunction(aten.view.default, expand_default_1, Ignored(), _users=2)
bmm_default = CallFunction(aten.bmm.default, view_default, view_default_1)
view_default_2 = CallFunction(aten.view.default, bmm_default, Ignored())
mul_Tensor = CallFunction(aten.mul.Tensor, view_default_2, KeywordArg('scale_factor'), _users=2)
amax_default = CallFunction(aten.amax.default, mul_Tensor, Ignored(), True)
sub_Tensor = CallFunction(aten.sub.Tensor, mul_Tensor, amax_default)
exp_default = CallFunction(aten.exp.default, sub_Tensor, _users=2)
sum_dim_IntList = CallFunction(aten.sum.dim_IntList, exp_default, Ignored(), True)
div_Tensor = CallFunction(aten.div.Tensor, exp_default, sum_dim_IntList, _users=2)
mul_Tensor_1 = CallFunction(aten.mul.Tensor, gt_Scalar, div_Tensor)
mul_Tensor_2 = CallFunction(aten.mul.Tensor, mul_Tensor_1, Ignored())
expand_default_2 = CallFunction(aten.expand.default, mul_Tensor_2, Ignored())
view_default_3 = CallFunction(aten.view.default, expand_default_2, Ignored(), _users=2)
expand_default_3 = CallFunction(aten.expand.default, KeywordArg('value'), Ignored())
view_default_4 = CallFunction(aten.view.default, expand_default_3, Ignored(), _users=2)
bmm_default_1 = CallFunction(aten.bmm.default, view_default_3, view_default_4)
view_default_5 = CallFunction(aten.view.default, bmm_default_1, Ignored())
alias_default = CallFunction(aten.alias.default, div_Tensor)
alias_default_1 = CallFunction(aten.alias.default, alias_default)
alias_default_2 = CallFunction(aten.alias.default, alias_default_1)
alias_default_3 = CallFunction(aten.alias.default, alias_default_2, _users=2)
neg_default = CallFunction(aten.neg.default, alias_default_3)
view_default_6 = CallFunction(aten.view.default, KeywordArg('tangents_1'), Ignored(), _users=2)
permute_default_1 = CallFunction(aten.permute.default, view_default_4, Ignored())
bmm_default_2 = CallFunction(aten.bmm.default, view_default_6, permute_default_1)
view_default_7 = CallFunction(aten.view.default, bmm_default_2, Ignored())
convert_element_type_default = CallFunction(prims.convert_element_type.default, gt_Scalar, Ignored())
mul_Tensor_3 = CallFunction(aten.mul.Tensor, convert_element_type_default, Ignored())
mul_Tensor_4 = CallFunction(aten.mul.Tensor, view_default_7, mul_Tensor_3)
clone_default = CallFunction(aten.clone.default, mul_Tensor_4, memory_format=torch.contiguous_format)
mul_Tensor_5 = CallFunction(aten.mul.Tensor, clone_default, alias_default_3, _users=2)
sum_dim_IntList_1 = CallFunction(aten.sum.dim_IntList, mul_Tensor_5, Ignored(), True)
fma_default = CallFunction(prims.fma.default, neg_default, sum_dim_IntList_1, mul_Tensor_5)
mul_Tensor_6 = CallFunction(aten.mul.Tensor, fma_default, KeywordArg('scale_factor'))
view_default_8 = CallFunction(aten.view.default, mul_Tensor_6, Ignored(), _users=2)
permute_default_2 = CallFunction(aten.permute.default, view_default_1, Ignored())
bmm_default_3 = CallFunction(aten.bmm.default, view_default_8, permute_default_2)
view_default_9 = CallFunction(aten.view.default, bmm_default_3, Ignored())
permute_default_3 = CallFunction(aten.permute.default, view_default, Ignored())
bmm_default_4 = CallFunction(aten.bmm.default, permute_default_3, view_default_8)
view_default_10 = CallFunction(aten.view.default, bmm_default_4, Ignored())
permute_default_4 = CallFunction(aten.permute.default, view_default_10, Ignored())
permute_default_5 = CallFunction(aten.permute.default, view_default_3, Ignored())
bmm_default_5 = CallFunction(aten.bmm.default, permute_default_5, view_default_6)
view_default_11 = CallFunction(aten.view.default, bmm_default_5, Ignored())
_sfdp_pattern_4_training = MultiOutputPattern([view_default_5,
  view_default_9,
  permute_default_4,
  view_default_11,
  None,
  None
])


expand_default = CallFunction(aten.expand.default, KeywordArg('query'), Ignored())
view_default = CallFunction(aten.view.default, expand_default, Ignored())
permute_default = CallFunction(aten.permute.default, KeywordArg('key'), Ignored())
expand_default_1 = CallFunction(aten.expand.default, permute_default, Ignored())
view_default_1 = CallFunction(aten.view.default, expand_default_1, Ignored())
bmm_default = CallFunction(aten.bmm.default, view_default, view_default_1)
view_default_2 = CallFunction(aten.view.default, bmm_default, Ignored())
mul_Tensor = CallFunction(aten.mul.Tensor, view_default_2, KeywordArg('scale_factor'), _users=2)
amax_default = CallFunction(aten.amax.default, mul_Tensor, Ignored(), True)
sub_Tensor = CallFunction(aten.sub.Tensor, mul_Tensor, amax_default)
exp_default = CallFunction(aten.exp.default, sub_Tensor, _users=2)
sum_dim_IntList = CallFunction(aten.sum.dim_IntList, exp_default, Ignored(), True)
div_Tensor = CallFunction(aten.div.Tensor, exp_default, sum_dim_IntList)
clone_default = CallFunction(aten.clone.default, div_Tensor)
expand_default_2 = CallFunction(aten.expand.default, clone_default, Ignored())
view_default_3 = CallFunction(aten.view.default, expand_default_2, Ignored())
expand_default_3 = CallFunction(aten.expand.default, KeywordArg('value'), Ignored())
view_default_4 = CallFunction(aten.view.default, expand_default_3, Ignored())
bmm_default_1 = CallFunction(aten.bmm.default, view_default_3, view_default_4)
_sfdp_pattern_4_inference = CallFunction(aten.view.default, bmm_default_1, Ignored(), _users=0)


rand_default = CallFunction(aten.rand.default, Ignored(), dtype=Ignored(), device=Ignored(), pin_memory=False)
gt_Scalar = CallFunction(aten.gt.Scalar, rand_default, KeywordArg('dropout_p'), _users=2)
expand_default = CallFunction(aten.expand.default, KeywordArg('query'), Ignored())
view_default = CallFunction(aten.view.default, expand_default, Ignored(), _users=2)
permute_default = CallFunction(aten.permute.default, KeywordArg('key'), Ignored())
expand_default_1 = CallFunction(aten.expand.default, permute_default, Ignored())
view_default_1 = CallFunction(aten.view.default, expand_default_1, Ignored(), _users=2)
bmm_default = CallFunction(aten.bmm.default, view_default, view_default_1)
view_default_2 = CallFunction(aten.view.default, bmm_default, Ignored())
mul_Tensor = CallFunction(aten.mul.Tensor, view_default_2, KeywordArg('scale_factor'))
convert_element_type_default = CallFunction(prims.convert_element_type.default, mul_Tensor, Ignored(), _users=2)
amax_default = CallFunction(aten.amax.default, convert_element_type_default, Ignored(), True)
sub_Tensor = CallFunction(aten.sub.Tensor, convert_element_type_default, amax_default)
exp_default = CallFunction(aten.exp.default, sub_Tensor, _users=2)
sum_dim_IntList = CallFunction(aten.sum.dim_IntList, exp_default, Ignored(), True)
div_Tensor = CallFunction(aten.div.Tensor, exp_default, sum_dim_IntList)
convert_element_type_default_1 = CallFunction(prims.convert_element_type.default, div_Tensor, Ignored(), _users=2)
mul_Tensor_1 = CallFunction(aten.mul.Tensor, gt_Scalar, convert_element_type_default_1)
mul_Tensor_2 = CallFunction(aten.mul.Tensor, mul_Tensor_1, Ignored())
expand_default_2 = CallFunction(aten.expand.default, mul_Tensor_2, Ignored())
view_default_3 = CallFunction(aten.view.default, expand_default_2, Ignored(), _users=2)
expand_default_3 = CallFunction(aten.expand.default, KeywordArg('value'), Ignored())
view_default_4 = CallFunction(aten.view.default, expand_default_3, Ignored(), _users=2)
bmm_default_1 = CallFunction(aten.bmm.default, view_default_3, view_default_4)
view_default_5 = CallFunction(aten.view.default, bmm_default_1, Ignored())
alias_default = CallFunction(aten.alias.default, convert_element_type_default_1)
alias_default_1 = CallFunction(aten.alias.default, alias_default)
alias_default_2 = CallFunction(aten.alias.default, alias_default_1)
alias_default_3 = CallFunction(aten.alias.default, alias_default_2)
convert_element_type_default_2 = CallFunction(prims.convert_element_type.default, alias_default_3, Ignored(), _users=2)
neg_default = CallFunction(aten.neg.default, convert_element_type_default_2)
view_default_6 = CallFunction(aten.view.default, KeywordArg('tangents_1'), Ignored(), _users=2)
permute_default_1 = CallFunction(aten.permute.default, view_default_4, Ignored())
bmm_default_2 = CallFunction(aten.bmm.default, view_default_6, permute_default_1)
view_default_7 = CallFunction(aten.view.default, bmm_default_2, Ignored())
convert_element_type_default_3 = CallFunction(prims.convert_element_type.default, gt_Scalar, Ignored())
mul_Tensor_3 = CallFunction(aten.mul.Tensor, convert_element_type_default_3, Ignored())
mul_Tensor_4 = CallFunction(aten.mul.Tensor, view_default_7, mul_Tensor_3)
clone_default = CallFunction(aten.clone.default, mul_Tensor_4, memory_format=torch.contiguous_format)
convert_element_type_default_4 = CallFunction(prims.convert_element_type.default, clone_default, Ignored())
mul_Tensor_5 = CallFunction(aten.mul.Tensor, convert_element_type_default_4, convert_element_type_default_2, _users=2)
sum_dim_IntList_1 = CallFunction(aten.sum.dim_IntList, mul_Tensor_5, Ignored(), True)
fma_default = CallFunction(prims.fma.default, neg_default, sum_dim_IntList_1, mul_Tensor_5)
convert_element_type_default_5 = CallFunction(prims.convert_element_type.default, fma_default, Ignored())
mul_Tensor_6 = CallFunction(aten.mul.Tensor, convert_element_type_default_5, KeywordArg('scale_factor'))
view_default_8 = CallFunction(aten.view.default, mul_Tensor_6, Ignored(), _users=2)
permute_default_2 = CallFunction(aten.permute.default, view_default_1, Ignored())
bmm_default_3 = CallFunction(aten.bmm.default, view_default_8, permute_default_2)
view_default_9 = CallFunction(aten.view.default, bmm_default_3, Ignored())
permute_default_3 = CallFunction(aten.permute.default, view_default, Ignored())
bmm_default_4 = CallFunction(aten.bmm.default, permute_default_3, view_default_8)
view_default_10 = CallFunction(aten.view.default, bmm_default_4, Ignored())
permute_default_4 = CallFunction(aten.permute.default, view_default_10, Ignored())
permute_default_5 = CallFunction(aten.permute.default, view_default_3, Ignored())
bmm_default_5 = CallFunction(aten.bmm.default, permute_default_5, view_default_6)
view_default_11 = CallFunction(aten.view.default, bmm_default_5, Ignored())
_sfdp_pattern_4_half_training = MultiOutputPattern([view_default_5,
  view_default_9,
  permute_default_4,
  view_default_11,
  None,
  None
])


expand_default = CallFunction(aten.expand.default, KeywordArg('query'), Ignored())
view_default = CallFunction(aten.view.default, expand_default, Ignored())
permute_default = CallFunction(aten.permute.default, KeywordArg('key'), Ignored())
expand_default_1 = CallFunction(aten.expand.default, permute_default, Ignored())
view_default_1 = CallFunction(aten.view.default, expand_default_1, Ignored())
bmm_default = CallFunction(aten.bmm.default, view_default, view_default_1)
view_default_2 = CallFunction(aten.view.default, bmm_default, Ignored())
mul_Tensor = CallFunction(aten.mul.Tensor, view_default_2, KeywordArg('scale_factor'))
convert_element_type_default = CallFunction(prims.convert_element_type.default, mul_Tensor, Ignored(), _users=2)
amax_default = CallFunction(aten.amax.default, convert_element_type_default, Ignored(), True)
sub_Tensor = CallFunction(aten.sub.Tensor, convert_element_type_default, amax_default)
exp_default = CallFunction(aten.exp.default, sub_Tensor, _users=2)
sum_dim_IntList = CallFunction(aten.sum.dim_IntList, exp_default, Ignored(), True)
div_Tensor = CallFunction(aten.div.Tensor, exp_default, sum_dim_IntList)
convert_element_type_default_1 = CallFunction(prims.convert_element_type.default, div_Tensor, Ignored())
clone_default = CallFunction(aten.clone.default, convert_element_type_default_1)
expand_default_2 = CallFunction(aten.expand.default, clone_default, Ignored())
view_default_3 = CallFunction(aten.view.default, expand_default_2, Ignored())
expand_default_3 = CallFunction(aten.expand.default, KeywordArg('value'), Ignored())
view_default_4 = CallFunction(aten.view.default, expand_default_3, Ignored())
bmm_default_1 = CallFunction(aten.bmm.default, view_default_3, view_default_4)
_sfdp_pattern_4_half_inference = CallFunction(aten.view.default, bmm_default_1, Ignored(), _users=0)
