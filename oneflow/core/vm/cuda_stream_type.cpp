#include "oneflow/core/vm/cuda_stream_type.h"
#include "oneflow/core/vm/instruction_type.h"
#include "oneflow/core/vm/stream.msg.h"
#include "oneflow/core/vm/thread_ctx.msg.h"
#include "oneflow/core/vm/cuda_instruction_status_querier.h"
#include "oneflow/core/vm/cuda_stream_handle_device_context.h"
#include "oneflow/core/device/cuda_util.h"
#include "oneflow/core/common/util.h"

namespace oneflow {
namespace vm {

void CudaStreamType::InitDeviceCtx(std::unique_ptr<DeviceCtx>* device_ctx, Stream* stream) const {
  device_ctx->reset(
      new CudaStreamHandleDeviceCtx(stream->mut_callback_list(), stream->device_id()));
}

void CudaStreamType::InitInstructionStatus(const Stream& stream,
                                           InstructionStatusBuffer* status_buffer) const {
  static_assert(sizeof(CudaInstrStatusQuerier) < kInstructionStatusBufferBytes, "");
  CudaInstrStatusQuerier::PlacementNew(status_buffer->mut_buffer()->mut_data(), stream.device_id());
}

void CudaStreamType::DeleteInstructionStatus(const Stream& stream,
                                             InstructionStatusBuffer* status_buffer) const {
  // do nothing
}

bool CudaStreamType::QueryInstructionStatusDone(
    const Stream& stream, const InstructionStatusBuffer& status_buffer) const {
  return CudaInstrStatusQuerier::Cast(status_buffer.buffer().data())->done();
}

void CudaStreamType::Compute(Instruction* instruction) const {
  auto* stream = instruction->mut_stream();
  cudaSetDevice(stream->device_id());
  {
    const auto& instr_type_id = instruction->mut_instr_msg()->instr_type_id();
    CHECK_EQ(instr_type_id.stream_type_id().interpret_type(), InterpretType::kCompute);
    instr_type_id.instruction_type().Compute(instruction);
    CudaCheck(cudaGetLastError());
  }
  stream->mut_callback_list()->MoveTo(instruction->mut_callback_list());
  char* data_ptr = instruction->mut_status_buffer()->mut_buffer()->mut_data();
  CudaInstrStatusQuerier::MutCast(data_ptr)->SetLaunched(stream->device_ctx().get());
}

ObjectMsgPtr<StreamDesc> CudaStreamType::MakeStreamDesc(const Resource& resource,
                                                        int64_t this_machine_id) const {
  if (!resource.has_gpu_device_num()) { return ObjectMsgPtr<StreamDesc>(); }
  std::size_t device_num = resource.gpu_device_num();
  auto ret = ObjectMsgPtr<StreamDesc>::New();
  ret->mutable_stream_type_id()->__Init__(LookupStreamType4TypeIndex<CudaStreamType>());
  ret->set_num_machines(1);
  ret->set_num_streams_per_machine(device_num);
  ret->set_num_streams_per_thread(1);
  ret->set_start_global_device_id(this_machine_id * device_num);
  return ret;
}

}  // namespace vm
}  // namespace oneflow