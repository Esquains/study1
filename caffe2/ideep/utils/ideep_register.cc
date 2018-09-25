#include <caffe2/core/event_cpu.h>
#include <caffe2/core/operator.h>
#include <caffe2/proto/caffe2_pb.h>
#include <ideep_pin_singletons.hpp>
#include "ideep_context.h"
#include <caffe2/ideep/utils/ideep_operator.h>

C10_DEFINE_REGISTRY(
    IDEEPOperatorRegistry,
    caffe2::OperatorBase,
    const caffe2::OperatorDef&,
    caffe2::Workspace*);

namespace caffe2 {

CAFFE_KNOWN_TYPE(ideep::tensor);

CAFFE_REGISTER_DEVICE_TYPE(DeviceType::IDEEP, c10::IDEEPOperatorRegistry);

REGISTER_EVENT_CREATE_FUNCTION(IDEEP, EventCreateCPU);
REGISTER_EVENT_RECORD_FUNCTION(IDEEP, EventRecordCPU);
REGISTER_EVENT_WAIT_FUNCTION(IDEEP, IDEEP, EventWaitCPUCPU);
REGISTER_EVENT_WAIT_FUNCTION(IDEEP, CPU, EventWaitCPUCPU);
REGISTER_EVENT_WAIT_FUNCTION(CPU, IDEEP, EventWaitCPUCPU);
REGISTER_EVENT_FINISH_FUNCTION(IDEEP, EventFinishCPU);
REGISTER_EVENT_QUERY_FUNCTION(IDEEP, EventQueryCPU);
REGISTER_EVENT_ERROR_MESSAGE_FUNCTION(IDEEP, EventErrorMessageCPU);
REGISTER_EVENT_SET_FINISHED_FUNCTION(IDEEP, EventSetFinishedCPU);
REGISTER_EVENT_RESET_FUNCTION(IDEEP, EventResetCPU);

BaseStaticContext* GetIDEEPStaticContext() {
  static IDEEPStaticContext context;
  return &context;
}

REGISTER_STATIC_CONTEXT(IDEEP, GetIDEEPStaticContext());

} // namespace caffe2
