// Copyright 2019 Bytedance Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================


#include <cuda_runtime.h>

#include <chrono>
#include <memory>

//Minghao
#include <iostream>
#include "test/test.cuh"
#include "gpu_compressor/terngrad.cuh"
//////////

#include "common.h"
#include "compressor/compressor.h"
#include "core_loops.h"
#include "global.h"
#include "logging.h"

#ifdef USE_P4ML
#define DO_QUANTIZE_IN_BYTEPS true
#define PRINT_SINGLE_TENSOR_RESNET false
#endif

namespace byteps {
namespace common {

#ifdef USE_P4ML
std::mutex _priorityQueue_mutex;
#endif
#ifdef BYTEPS_INCA
std::mutex _debugPrint_mutex;
#endif

void FinishOrProceed(std::shared_ptr<TensorTableEntry> task) {
  auto &queue_list = task->queue_list;
#ifdef USE_INCA
  if(queue_list.size() < 1){
    printf("Task %s with key %d has zero queue entering FinishOrProceed\n", (task->tensor_name).c_str(), task->dpdk_key);
  } 
#endif
  BPS_CHECK_GE(queue_list.size(), 1);
  auto this_op = queue_list[0];
  // Minghao
  // printf("Task %s Key %d with OP %d %s\n", (task->tensor_name).c_str(), task->dpdk_key, this_op, LogStrings[this_op].c_str());
  //BPS_LOG(INFO) << "FinishOrProceed op: " << LogStrings[this_op];
  //////////////////////
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
#ifdef TIMING
  if (this_op == COMPRESS){
    // BPS_LOG(INFO) << "Finish COMPRESS tensor: " << task->tensor_name;
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end - task->compress_start;
    std::cout << "Time to COMPRESS tensor: " << task->tensor_name << " of size: "
              << task->len << " is: " << diff.count() << " s\n";
  }
  else if (this_op == DECOMPRESS){
    // BPS_LOG(INFO) << "Finish DECOMPRESS tensor: " << task->tensor_name;
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end - task->decompress_start;
    std::cout << "Time to DECOMPRESS tensor: " << task->tensor_name << " of size: "
              << task->len << " is: " << diff.count() << " s\n";
  }
  else if(this_op == PULL){
    // BPS_LOG(INFO) << "Finish PULL tensor: " << task->tensor_name;
    // if (task->communication_call == 10){
    //   std::cout << task->compress_call << " " << task->decompress_call << " " << 
    //                task->communication_call << std::endl;
    // }
    // BPS_LOG(INFO) << "tensor: " << task->tensor_name << " COMPRESS calls: " << task->compress_call 
    //               << " DECOMPRESS calls: " << task->decompress_call << " PULL calls: " << task->communication_call;
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end - task->communication_start;
    std::cout << "Time to PUSHPULL tensor: " << task->tensor_name << " of size: "
              << task->len << " is: " << diff.count() << " s\n";
  }
#endif
  q->reportFinish(task->len);
  if (BytePSGlobal::IsTensorSampled(task->key)) {
    // We only support sampling
    BPS_CHECK(task->tensor->dtype() == common::BYTEPS_FLOAT32);
    size_t i = task->offset / 4;
    size_t j = (task->offset + task->len) / 4 - 1;
    if (task->device == CPU_DEVICE_ID) {
      BPS_LOG(DEBUG) << "Sampled key=" << task->key
                     << " rank=" << BytePSGlobal::GetLocalRank()
                     << " input[0]=" << *((float *)(task->tensor->data()) + i)
                     << "\tinput[-1]=" << *((float *)(task->tensor->data()) + j)
                     << "\toutput[0]=" << *((float *)(task->output->data()) + i)
                     << "\toutput[-1]="
                     << *((float *)(task->output->data()) + j)
                     << "\t after stage: " << LogStrings[this_op];
    } else {
      float i0, i1, o0, o1;
      cudaMemcpy(&i0, (float *)(task->tensor->data()) + i, 4,
                 cudaMemcpyDeviceToHost);
      cudaMemcpy(&i1, (float *)(task->tensor->data()) + j, 4,
                 cudaMemcpyDeviceToHost);
      cudaMemcpy(&o0, (float *)(task->output->data()) + i, 4,
                 cudaMemcpyDeviceToHost);
      cudaMemcpy(&o1, (float *)(task->output->data()) + j, 4,
                 cudaMemcpyDeviceToHost);
      BPS_LOG(DEBUG) << "Sampled key=" << task->key
                     << " rank=" << BytePSGlobal::GetLocalRank()
                     << " input[0]=" << i0 << "\tinput[-1]=" << i1
                     << "\toutput[0]=" << o0 << "\toutput[-1]=" << o1
                     << "\t after stage: " << LogStrings[this_op];
    }
  }

  if (task->context->profile_flag) {
    BPS_CHECK(task->context->part_comm_time[task->key][this_op].back()->dur ==
              0)
        << " tensor: " << task->tensor_name << " task->key:" << task->key
        << " type:" << this_op << " 'dur' has already been assigned:"
        << task->context->part_comm_time[task->key][this_op].back()->dur;
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    auto _ts =
        task->context->part_comm_time[task->key][this_op].back()->start_t;
    BPS_CHECK(task->context->part_comm_time.find(task->key) !=
              task->context->part_comm_time.end())
        << " tensor: " << task->tensor_name << " task->key:" << task->key
        << " type:" << this_op;
    BPS_CHECK(task->context->part_comm_time[task->key].find(this_op) !=
              task->context->part_comm_time[task->key].end())
        << " tensor: " << task->tensor_name << " task->key:" << task->key
        << " type:" << this_op;

    task->context->part_comm_time[task->key][this_op].back()->dur =
        (long long)(us.count()) - _ts;
  }

  // finish current QueueType of this task, erase current QueueType.
  queue_list.erase(queue_list.begin());
  if (queue_list.size() > 0) {
    BPS_CHECK(task->tensor_name != "");
    BPS_LOG(TRACE) << "Rank=" << BytePSGlobal::GetRank() << " finishes "
                   << LogStrings[this_op] << ", tensor: " << task->tensor_name
                   << ", key=" << task->key << "; Passing to the next queue.";
// #ifdef USE_P4ML
//     if (LogStrings[queue_list[0]] == "PUSH" &&
//         !BytePSGlobal::ReadyForP4ML(task->p4ml_key)) {
//       std::lock_guard<std::mutex> lock(_priorityQueue_mutex);
//       BytePSGlobal::push_queue.push(task);
//     } else {
//       BytePSGlobal::GetScheduledQueue(queue_list[0])->addTask(task);
//     }
// #else
    BytePSGlobal::GetScheduledQueue(queue_list[0])->addTask(task);
// #endif  
  } else {
    // this is the last QueueType of this current sub-task.
    BPS_CHECK(task->counter_ptr) << task->tensor_name << " counter_ptr is null";
#ifdef USE_INCA
    // update the global timer
    BytePSGlobal::timer = std::chrono::high_resolution_clock::now();
#endif
    int v = task->counter_ptr.get()->fetch_add(1);
    if (v == (int)(task->total_partnum - 1)) {
      // if meet this condition, that means all sub-tasks of this task have been
      // done

      //* Update the norm associated with this task in the context
      task->context->norm = task->norm;
      // printf("update norm of %s to be %.6f\n", (task->context->tensor_name).c_str(), task->context->norm);

      BPS_CHECK(task->tensor_name != "");
      BPS_LOG(TRACE) << "Rank=" << BytePSGlobal::GetRank()
                     << " finish processing tensor: " << task->tensor_name;

      if (PushPullSpeed::ShouldRecord()) {
        PushPullSpeed::RecordSpeed(task);
      }

      task->callback(Status::OK());
      //* Add for profiling communication events
      if (task->context->profile_flag) {
        BPS_CHECK(task->context->comm_time.back()->dur == 0)
            << " tensor: " << task->tensor_name
            << " 'dur' has already been assigned:"
            << task->context->comm_time.back()->dur;
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(duration);
        auto _ts = task->context->comm_time.back()->start_t;
        task->context->comm_time.back()->dur = (long long)(us.count()) - _ts;
      }
      // Set the profile_flag first
      // *step_cnt* denotes the number this gradient has been synchronized.
      task->context->step_cnt += 1;
      BytePSGlobal::SetProfileFlag(task->context);
    }
  }
  return;
}

#ifdef USE_INCA
void DPDKListenerLoop(){
  while (!BytePSGlobal::ShouldShutdown()){
    int64_t finish_key = BytePSGlobal::GetDPDK()->PollFinishKey();
    if (finish_key >= 0) {
      // printf("call to PollFinishKey: %d\n", finish_key);
      FinishOrProceed(BytePSGlobal::DPDKTask[finish_key]);
      BytePSGlobal::DPDKTask[finish_key] = nullptr;
    }
    else{
      usleep(1);
    }
  }
}

void DPDKResetLoop(){
  while (!BytePSGlobal::ShouldShutdown()){
    auto current_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_span =
      (current_time - BytePSGlobal::timer);
    if(time_span.count() >= 10 * 1000) BytePSGlobal::GetDPDK()->Reset();
    else sleep(1);
  }
}
#endif

#ifdef USE_P4ML
void TaskAddListener() {
  while (!BytePSGlobal::ShouldShutdown()) {
    std::chrono::time_point<std::chrono::system_clock> current_time =
        std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time_span =
        std::chrono::duration_cast<std::chrono::duration<double>>(
            current_time - BytePSGlobal::timer);
    std::chrono::duration<double> total_time =
        std::chrono::duration_cast<std::chrono::duration<double>>(
            current_time - BytePSGlobal::start_time);
    if (BytePSGlobal::measure_interval > 0.0) {
      if (time_span.count() >= BytePSGlobal::measure_interval) {
        // printf("total send %" PRIu64 " bytes, time %lf, throughput: %lf\n",
        // BytePSGlobal::total_sent, total_time, BytePSGlobal::total_sent /
        // 1024.0 / 1024.0 / 1024.0 * 8.0 / 1.0);
        // BytePSGlobal::GetP4ML()->GetLossRate();
        // int tmp = BytePSGlobal::GetP4ML()->GetCollisionTimeAndClear();
        // if (tmp)
        //   printf("%d\n", tmp);
        printf("bandwidth: %lf\n", BytePSGlobal::total_sent / 1024.0 / 1024.0 /
                                       1024.0 * 8.0 / time_span.count());
        BytePSGlobal::total_sent = 0;
        BytePSGlobal::timer = current_time;
      }
    }

    if (!BytePSGlobal::push_queue.empty()) {
      std::lock_guard<std::mutex> lock(_priorityQueue_mutex);
      auto tmpTask = BytePSGlobal::push_queue.top();
      // This queue used to guarantee the order of PUSH task
      if (BytePSGlobal::ReadyForP4ML(tmpTask->p4ml_key)) {
        // printf("tmpTask->p4ml_key: %d ", tmpTask->p4ml_key);
        // std::cout << LogStrings[tmpTask->queue_list[0]] << std::endl;
        BytePSGlobal::GetScheduledQueue((tmpTask->queue_list)[0])
            ->addTask(tmpTask);
        BytePSGlobal::push_queue.pop();
      }
    }

    int64_t finishedTask = BytePSGlobal::GetP4ML()->GetFinishKey();
    // printf("try finishedTask: %d\n", finishedTask);
    if (finishedTask >= 0) {
      // printf("Finish PUSH Task: %d\n", finishedTask);
      FinishOrProceed(BytePSGlobal::Task[finishedTask]);
    }
    usleep(1);
  }
}
#endif

bool RunCoordinateLoopOnce(QueueType this_op) {
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  if (task) {
    int rank = BytePSGlobal::GetLocalRank();
    auto key = task->key;

    // first send to next queue and then broadcast signal
    // to guarantee the entry is available when getTask(key) at Reduce/Broadcast
    // thread
    FinishOrProceed(task);

    BytePSCommSignal sig = PUSH_READY;
    std::shared_ptr<BytePSComm> comm;

    switch (this_op) {
      case COORDINATE_REDUCE: {
        sig = REDUCE_READY;
        comm = BytePSGlobal::GetNccl()->GetSignalComm();
        break;
      }
      case COORDINATE_BROADCAST: {
        sig = BCAST_READY;
        comm = BytePSGlobal::GetNccl()->GetSignalComm();
        break;
      }
      case COORDINATE_PUSH: {
        sig = PUSH_READY;
        comm = BytePSGlobal::GetBasicComm();
        break;
      }
      default:
        BPS_CHECK(0) << "unsupported op: " << this_op;
    }

    BPS_CHECK_NE(rank, comm->getRoot())
        << "only non-root device should enter COORDINATE loop";

    // Minghao: if sig is PUSH_READY, we should also include scale and norm in the msg
    struct BytePSCommMsg msg;
    if (sig == PUSH_READY){
      msg = {rank, sig, key, task->scale, task->norm};
      // printf("non-root sent norm %.6f\n", task->norm);
    }
    else{
      msg = {rank, sig, key};
    }
    comm->sendSignalToRoot(&msg, sizeof(BytePSCommMsg));

    BPS_CHECK(task->tensor_name != "");
    BPS_LOG(TRACE) << task->tensor_name << " send coordinate info: "
                   << "Signal=" << sig << ", rank=" << rank << ", key=" << key;

  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

inline void PostNcclCalls(
    std::shared_ptr<byteps::common::TensorTableEntry> task, QueueType this_op) {
  BPS_CHECK(this_op == REDUCE || this_op == BROADCAST)
      << "Only REDUCE and BROADCAST use NCCL.";
  auto tensor = (this_op == REDUCE) ? task->tensor : task->output;
  BPS_CHECK(tensor);
  BPS_CHECK_EQ(0, tensor->size() % tensor->shape().num_elements());

  auto key = task->key;
  auto len = task->len;
  auto offset = task->offset;
  auto unit_len = tensor->size() / tensor->shape().num_elements();
  auto p = (char *)(tensor->data()) + offset;
  if (task->device == CPU_DEVICE_ID) {
    p = (char *)(task->gpu_ptr) + offset;
  }

  auto nccl_dtype = getNcclDataType(tensor->dtype());

  auto nccl = BytePSGlobal::GetNccl();
  auto nccl_stream = nccl->GetStream(key, this_op);
  auto nccl_comm = nccl->GetComm(key, this_op);
  auto nccl_root = nccl->GetRoot(key, this_op);
  auto nccl_size = nccl->GetSize();
  auto nccl_rank = nccl->GetRank(key, this_op);

  auto num_elem_per_gpu = len / nccl_size / unit_len;
  auto left_elem = (len / unit_len) - (num_elem_per_gpu * nccl_size);
  if (BytePSGlobal::IsUsingReduce()) {
    nccl_root = BytePSGlobal::GetReduceRootByKey(key);
    num_elem_per_gpu = 0;
    left_elem = len / unit_len;
    // Minghao
    // NOTE: it seems that we DON'T have this "IsUsingReduce" set by default!!!
    // BPS_LOG(INFO) << "Reduce key=" << key << " to root=" << nccl_root
    //                << " rank=" << BytePSGlobal::GetLocalRank();
    BPS_LOG(TRACE) << "Reduce key=" << key << " to root=" << nccl_root
                   << " rank=" << BytePSGlobal::GetLocalRank();
    //////////////
  }

  BPS_CHECK(task->tensor_name != "");
  /* Minghao */
  // BPS_LOG(INFO) << task->tensor_name << " calling NCCL " << LogStrings[this_op]
  //                << " (rank=" << nccl_rank << ") key=" << key
  //                << ", elements=" << len / unit_len
  //                << ", device=" << task->device;
  
  BPS_LOG(TRACE) << task->tensor_name << " calling NCCL " << LogStrings[this_op]
                 << " (rank=" << nccl_rank << ") key=" << key
                 << ", elements=" << len / unit_len
                 << ", device=" << task->device;
  /////////////

  if (this_op == REDUCE) {
    // We reduce to task->output except that it is a CPU tensor
    auto out_p = (char *)(task->output->data()) + offset;
    if (task->device == CPU_DEVICE_ID && task->tensor == task->output) {
      out_p = p;
    }

    if (num_elem_per_gpu) {
      /* Minghao */
      //BPS_LOG(INFO) << "!!!!!!!!ncclReduceScatter!!!!!!!!!!!!";
      #ifndef CPU_COMPRESS
      //nccl_dtype = getNcclDataType(BYTEPS_UINT8);
      // test clipping here by changing the size field in ncclReduceScatter
      NCCLCHECK(ncclReduceScatter(
          (const void *)p,
          (void *)(out_p + nccl_rank * num_elem_per_gpu * unit_len),
          (size_t)num_elem_per_gpu, (ncclDataType_t)nccl_dtype,
          (ncclRedOp_t)ncclSum, (ncclComm_t)nccl_comm,
          (cudaStream_t)nccl_stream));
      #else
      NCCLCHECK(ncclReduceScatter(
          (const void *)p,
          (void *)(out_p + nccl_rank * num_elem_per_gpu * unit_len),
          (size_t)num_elem_per_gpu, (ncclDataType_t)nccl_dtype,
          (ncclRedOp_t)ncclSum, (ncclComm_t)nccl_comm,
          (cudaStream_t)nccl_stream));
      #endif
      // do scale finding here
      // task->scale = terngrad_scale((void *)(out_p + nccl_rank * num_elem_per_gpu * unit_len), (size_t)num_elem_per_gpu);
      // BPS_LOG(INFO) << "Rank: " << nccl_rank << " Tensor: " << task->tensor_name <<" ReduceScatter ptr: " 
      //               << (void *)(out_p + nccl_rank * num_elem_per_gpu * unit_len) 
      //               << " len: " << num_elem_per_gpu << " scale: " << task->scale;
    }
    if (left_elem) {
      /* Minghao */
      //BPS_LOG(INFO) << "!!!!!!!!ncclReduce!!!!!!!!!!!!";
      #ifndef CPU_COMPRESS
      NCCLCHECK(ncclReduce((const void *)(p + len - left_elem * unit_len),
                           (void *)(out_p + len - left_elem * unit_len),
                           (size_t)left_elem, (ncclDataType_t)nccl_dtype,
                           (ncclRedOp_t)ncclSum, (int)nccl_root,
                           (ncclComm_t)nccl_comm, (cudaStream_t)nccl_stream));
      #else
      NCCLCHECK(ncclReduce((const void *)(p + len - left_elem * unit_len),
                           (void *)(out_p + len - left_elem * unit_len),
                           (size_t)left_elem, (ncclDataType_t)nccl_dtype,
                           (ncclRedOp_t)ncclSum, (int)nccl_root,
                           (ncclComm_t)nccl_comm, (cudaStream_t)nccl_stream));
      #endif
      // do scale finding here
      // float temp_scale = terngrad_scale((void *)(out_p + len - left_elem * unit_len), (size_t)left_elem);
      // if (!task->scale || task->scale < temp_scale) task->scale = temp_scale;
      // BPS_LOG(INFO) << "Rank: " << nccl_rank << " Tensor: " << task->tensor_name <<" Reduce ptr: " 
      //               << (void *)(out_p + nccl_rank * num_elem_per_gpu * unit_len) 
      //               << " len: " << num_elem_per_gpu << " scale: " << task->scale;
      //////////////
    }
  } else {
    if (num_elem_per_gpu) {
      /* Minghao */
      //BPS_LOG(INFO) << "!!!!!!!!ncclAllGather!!!!!!!!!!!! Rank: " << nccl_rank << " tensor_name: " << task->tensor_name << "\n";
      //////////////
      NCCLCHECK(ncclAllGather(
          (const void *)(p + nccl_rank * num_elem_per_gpu * unit_len),
          (void *)p, (size_t)num_elem_per_gpu, (ncclDataType_t)nccl_dtype,
          (ncclComm_t)nccl_comm, (cudaStream_t)nccl_stream));
    }
    if (left_elem) {
      //BPS_LOG(INFO) << "!!!!!!!!ncclBroadcast!!!!!!!!!!!!";
      NCCLCHECK(ncclBroadcast((const void *)(p + len - left_elem * unit_len),
                              (void *)(p + len - left_elem * unit_len),
                              (size_t)left_elem, (ncclDataType_t)nccl_dtype,
                              (int)nccl_root, (ncclComm_t)nccl_comm,
                              (cudaStream_t)nccl_stream));
    }
  }
}

bool RunRootNcclLoopOnce() {
  auto signal_comm = BytePSGlobal::GetNccl()->GetSignalComm();
  int root = signal_comm->getRoot();
  int rank = BytePSGlobal::GetLocalRank();
  BPS_CHECK_EQ(rank, root);

  int nccl_size = BytePSGlobal::GetNccl()->GetSize();
  QueueType nccl_ops[] = {REDUCE, BROADCAST};

  auto nccl_entry = std::make_shared<NcclGroupEntry>();
  auto &tasks = nccl_entry->tasks;
  auto &queues = nccl_entry->queues;

  NCCLCHECK(ncclGroupStart());
  for (auto this_op : nccl_ops) {
    auto q = BytePSGlobal::GetScheduledQueue(this_op);
    for (int i = 0; i < BytePSGlobal::GetNccl()->GetGroupSize(); i++) {
      auto task = q->getTask();
      if (!task) {
        break;
      }
      /* Minghao */
      //std::cout << "InRunRootNcclLoopOnce\n";
      //if (task->gpu_ptr) BPS_LOG(INFO) << "RootNccl Tensor GPU ptr: " << task->gpu_ptr << "\n";
      //if (task->cpubuff) BPS_LOG(INFO) << "RootNccl Tensor CPU buff: " << task->cpubuff << "\n";
      auto tensor = task->tensor;
      /////////////
      tasks.push_back(task);
      queues.push_back(q);

      if (nccl_size > 1) {
        // notify non-root devices
        struct BytePSCommMsg msg = {
            rank, (this_op == REDUCE) ? DO_REDUCE : DO_BROADCAST, task->key};
        signal_comm->broadcastSignal(&msg, sizeof(BytePSCommMsg));
        // Minghao
        //BPS_LOG(INFO) << "RootNcclRank: " << rank << " Task Tensor: " << task->tensor_name << "out pointer: " << task->output->data()+task->offset << "\n";
        //////////
        PostNcclCalls(task, this_op);
      }
    }
  }
  if (tasks.size()) {
    struct BytePSCommMsg msg = {rank, DO_GROUP, 0};
    signal_comm->broadcastSignal(&msg, sizeof(BytePSCommMsg));
    NCCLCHECK(ncclGroupEnd());
    nccl_entry->RecordEvents();
    BPS_LOG(TRACE) << "NCCL Group size=" << tasks.size() << " rank=" << rank;
    BytePSGlobal::GetNccl()->EnqueueGroup(nccl_entry);
  } else {
    NCCLCHECK(ncclGroupEnd());
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }

  return true;
}

bool RunNonRootNcclLoopOnce() {
  auto signal_comm = BytePSGlobal::GetNccl()->GetSignalComm();
  int root = signal_comm->getRoot();
  int rank = BytePSGlobal::GetLocalRank();
  BPS_CHECK_NE(rank, root);

  auto nccl_entry = std::make_shared<NcclGroupEntry>();
  auto &tasks = nccl_entry->tasks;
  auto &queues = nccl_entry->queues;
  struct BytePSCommMsg msg = {};

  NCCLCHECK(ncclGroupStart());
  while (1) {
    signal_comm->recvSignalFromRoot(&msg, sizeof(BytePSCommMsg));
    if (BytePSGlobal::ShouldShutdown()) return true;
    if (msg.signal == DO_GROUP) {
      break;
    }
    QueueType this_op = REDUCE;
    if (msg.signal == DO_BROADCAST) {
      this_op = BROADCAST;
    } else {
      BPS_CHECK_EQ(msg.signal, DO_REDUCE) << msg.signal << ", " << DO_REDUCE;
    }

    auto key = msg.key;

    auto q = BytePSGlobal::GetScheduledQueue(this_op);
    auto task = q->getTask(key);
    BPS_CHECK(task);
    /* Minghao */
    // if (task->gpu_ptr) BPS_LOG(INFO) << "NonRootNccl Tensor GPU ptr: " << task->gpu_ptr << "\n";
    // if (task->cpubuff) BPS_LOG(INFO) << "NonRootNccl Tensor CPU buff: " << task->cpubuff << "\n";
    // auto tensor = task->tensor;
    //////////////

    tasks.push_back(task);
    queues.push_back(q);

    // Minghao
    //BPS_LOG(INFO) << "NonRootNccl Rank: " << rank <<  " Task Tensor: " << task->tensor_name << "out pointer: " << task->output->data()+task->offset << "\n";
    ///////////
    PostNcclCalls(task, this_op);
  }
  NCCLCHECK(ncclGroupEnd());

  nccl_entry->RecordEvents();
  BytePSGlobal::GetNccl()->EnqueueGroup(nccl_entry);
  return true;
}

bool RunSyncNcclOnce() {
  auto nccl_entry = BytePSGlobal::GetNccl()->DequeueGroup();
  if (nccl_entry) {
    nccl_entry->SynchronizeEvents();
    for (size_t i = 0; i < nccl_entry->tasks.size(); i++) {
      if (nccl_entry->queues[i]->getQueueType() == REDUCE){
        auto task = nccl_entry->tasks[i];
        auto tensor =
        (BytePSGlobal::GetNccl()->GetSize() > 1) ? task->output : task->tensor;
        BPS_CHECK(tensor);
        int rank = BytePSGlobal::GetLocalRank();
        auto key = task->key;

        auto nccl = BytePSGlobal::GetNccl();
        auto nccl_root = nccl->GetRoot(key, REDUCE);
        auto nccl_size = nccl->GetSize();
        auto nccl_rank = nccl->GetRank(key, REDUCE);

        auto len = task->len;
        auto offset = task->offset;
        auto p = (char *)(tensor->data()) + offset;
        if (task->device == CPU_DEVICE_ID) {
          p = (char *)(task->gpu_ptr) + offset;
        }
        auto unit_len = tensor->size() / tensor->shape().num_elements();
        auto num_elem_per_gpu = len / nccl_size / unit_len;
        auto left_elem = (len / unit_len) - (num_elem_per_gpu * nccl_size);

        auto copy_offset = nccl_rank * num_elem_per_gpu * unit_len;
        auto copy_len = num_elem_per_gpu * unit_len;
        if (left_elem && (nccl_root == nccl_rank)) {
          copy_len += left_elem * unit_len;
        }

        if (BytePSGlobal::IsUsingReduce()) {
          copy_offset = 0;
          copy_len = (BytePSGlobal::GetReduceRootByKey(key) == nccl_rank) ? len : 0;
        }
        if (copy_len) {
          #ifdef GPU_COMPRESS
          if(tensor->dtype() == BYTEPS_FLOAT32) {
            task->scale = terngrad_scale((void *)(p + copy_offset), (size_t)copy_len / unit_len);
          }
          #endif
          // BPS_LOG(INFO) << "Rank: " << nccl_rank << " Tensor: " << task->tensor_name <<" Scale ptr: " 
          //               << (void *)(p + copy_offset)
          //               << " len: " << (size_t)copy_len / unit_len << " scale: " << task->scale;
        }
        FinishOrProceed(nccl_entry->tasks[i]);
        if(!BytePSGlobal::IsRootDevice()){
          BytePSCommSignal sig = CONTEXT_PUSH_READY;
          std::shared_ptr<BytePSComm> comm;
          comm = BytePSGlobal::GetBasicComm();
          struct BytePSCommMsg msg;
          msg = {rank, sig, key, task->scale};
          comm->sendSignalToRoot(&msg, sizeof(BytePSCommMsg));
        }
      }
      else{
        FinishOrProceed(nccl_entry->tasks[i]);
      }
      
    }
    nccl_entry->DestroyEvents();
    BPS_LOG(TRACE) << "Finished NCCL Group size=" << nccl_entry->tasks.size()
                   << " rank=" << BytePSGlobal::GetLocalRank();
    
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunContextPushLoopOnce() {
  QueueType this_op = CONTEXT_PUSH;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  
  if (task) {
    //std::cout << "context PUSH Tensor: " << task->tensor_name << " scale: " << task->scale << std::endl;
    #ifdef DEFAULT_CONTEXT_PUSHPULL
    /* Minghao */
    // do context pushing and pulling here
    auto tensor = task->tensor;
    /////////////
    BPS_CHECK(BytePSGlobal::IsRootDevice())
        << "only root device should enter PUSH loop";

    if (BytePSGlobal::IsDistributed()) {
      auto offset = task->offset;
      auto len = task->len;

      char *data;
      BPS_CHECK(task->cpubuff);
      data =
          const_cast<char *>(static_cast<const char *>(task->cpubuff) + offset);

      // get metadata
      const int dtype = task->tensor->dtype();

      /* Minghao */
      // use compressed data/len
      if (task->compressed) {
        BPS_LOG(DEBUG) << "PUSH with gradient compression. key=" << task->key;
        data = task->compressed->data;
        len = task->compressed->size;
        task->compressed = nullptr;
      }
      BPS_LOG(INFO) << "Push Task Tensor: " << task->tensor_name << " key: " << task->key << " data from: " << (task->cpubuff) + offset << " len: " << len << " scale: " << task->scale << "\n";
      ///////////////////////////

      // false means not to delete data when SArray is deleted
      ps::SArray<char> vals(data, len, false);

      int cmd = GetCommandType(RequestType::kDefaultPushPull, dtype);
      auto &pskv = BytePSGlobal::EncodeDefaultKey(task->key, len);
      BytePSGlobal::GetPS()->ZPush(pskv.keys, vals, pskv.lens, cmd,
                                   [task, q]() { FinishOrProceed(task); });
    } else {
      // This is a dummy barrier for IsCrossPcieSwitch()
      BPS_CHECK(BytePSGlobal::IsCrossPcieSwitch());
      FinishOrProceed(task);
    }
    #else
    // An "empty" push-pull that does nothing
    FinishOrProceed(task);
    #endif
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunContextPullLoopOnce() {
  QueueType this_op = CONTEXT_PULL;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  
  if (task) {
    //std::cout << "context PULL Tensor: " << task->tensor_name << " scale: " << task->scale << std::endl;
    #ifdef DEFAULT_CONTEXT_PUSHPULL
    /* Minghao */
    //std::cout << "RunPushLoopOnce\n";
    // if (task->gpu_ptr) BPS_LOG(INFO) << "Pull Tensor GPU ptr: " << task->gpu_ptr << "\n";
    // if (task->cpubuff) BPS_LOG(INFO) << "Pull Tensor CPU buff: " << task->cpubuff << "\n";
    auto tensor = task->tensor;
    /////////////
    BPS_CHECK(BytePSGlobal::IsRootDevice())
        << "only root device should enter PULL loop";
   
    auto offset = task->offset;
    auto len = task->len;

    char *data;
    BPS_CHECK(task->cpubuff);
    data =
        const_cast<char *>(static_cast<const char *>(task->cpubuff) + offset);

    // get metadata
    const int dtype = task->output->dtype();

    // Minghao
    BPS_LOG(INFO) << "Pull Task Tensor: " << task->tensor_name << " key: " << task->key << " data to: " << (task->cpubuff) + offset << " len: " << len << " scale: " << task->scale << "\n";
    ////////////

    // false means not to delete data when SArray is deleted
    auto vals = new ps::SArray<char>(data, len, false);

    int cmd = GetCommandType(RequestType::kDefaultPushPull, dtype);
    auto &pskv = BytePSGlobal::EncodeDefaultKey(task->key, len);
    // issue pull
    BytePSGlobal::GetPS()->ZPull(pskv.keys, vals, &pskv.lens, cmd,
                                 [vals, task, q]() {
                                   delete vals;
                                   FinishOrProceed(task);
                                 });
    #else
    auto key = task->key;
    int local_rank = BytePSGlobal::GetLocalRank();
    int local_size = BytePSGlobal::GetLocalSize();

    if (local_size > 1) {
      // notify non-root devices
      // when broadcasting here, also broadcast the scale
      struct BytePSCommMsg msg = {local_rank, DO_COPYD2H, key, task->scale};
      BytePSGlobal::GetBasicComm()->broadcastSignal(&msg,
                                                    sizeof(BytePSCommMsg));
    }
    // An "empty" push-pull that does nothing
    FinishOrProceed(task);
    #endif
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunCopyDevice2HostLoopOnce() {
  QueueType this_op = COPYD2H;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();

  if (task) {
    /* Minghao */
    //BPS_LOG(INFO) << "RunCopyDevice2HostLoopOnce. rank=" << BytePSGlobal::GetLocalRank();
    /////////////
    auto copy_d2h_Stream = BytePSGlobal::GetCopyDevice2HostStream();
    // If we ran NCCL reduce, we should copy from task->output
    auto tensor =
        (BytePSGlobal::GetNccl()->GetSize() > 1) ? task->output : task->tensor;
    BPS_CHECK(tensor);
    auto key = task->key;

    auto nccl = BytePSGlobal::GetNccl();
    auto nccl_root = nccl->GetRoot(key, REDUCE);
    auto nccl_size = nccl->GetSize();
    auto nccl_rank = nccl->GetRank(key, REDUCE);

    auto len = task->len;
    auto offset = task->offset;
    auto p = (char *)(tensor->data()) + offset;
    if (task->device == CPU_DEVICE_ID) {
      p = (char *)(task->gpu_ptr) + offset;
    }
    auto unit_len = tensor->size() / tensor->shape().num_elements();
    char *cpubuff;
    if (BytePSGlobal::IsCrossPcieSwitch()) {
      BPS_CHECK(task->pcie_cpubuff.size());
      cpubuff =
          (char *)(task->pcie_cpubuff[BytePSGlobal::GetPcieSwitchIndex()]) +
          offset;
    } else {
      cpubuff = (char *)(task->cpubuff) + offset;
    }

    BPS_CHECK(cpubuff) << task->tensor_name
                       << ": CPU buffer not initialized, size=" << len;

    auto num_elem_per_gpu = len / nccl_size / unit_len;
    auto left_elem = (len / unit_len) - (num_elem_per_gpu * nccl_size);

    auto copy_offset = nccl_rank * num_elem_per_gpu * unit_len;
    auto copy_len = num_elem_per_gpu * unit_len;
    if (left_elem && (nccl_root == nccl_rank)) {
      copy_len += left_elem * unit_len;
    }

    if (BytePSGlobal::IsUsingReduce()) {
      copy_offset = 0;
      copy_len = (BytePSGlobal::GetReduceRootByKey(key) == nccl_rank) ? len : 0;
    }

    if (copy_len) {
      /* Minghao */
      #ifdef GPU_COMPRESS
      
      if(tensor->dtype() == BYTEPS_FLOAT32) {
        //task->scale = terngrad_compress((void *)(p + copy_offset), (size_t)copy_len / unit_len);
        terngrad_compress((void *)(p + copy_offset), (size_t)copy_len / unit_len, task->scale);
        BPS_LOG(INFO) << "Compress Tensor: " << task->tensor_name << " start ptr: "
                      << (void *)(p + copy_offset) << " len: " << (size_t)copy_len / unit_len;
      }
      ///task->scale = terngrad_scale((void *)(p + copy_offset), (size_t)copy_len / unit_len);
      #endif

      //BPS_LOG(INFO) << "NcclRank: " << nccl_rank << " Task Tensor: " << task->tensor_name << " key: " << key << " copy dest: " << (task->cpubuff) + offset + copy_offset << " copy_offset: " << copy_offset << " len: " << len << " scale: " << task->scale <<"\n";

      /////////////
      CUDA_CALL(cudaMemcpyAsync(
          (void *)(cpubuff + copy_offset), (const void *)(p + copy_offset),
          (size_t)copy_len, (cudaMemcpyKind)cudaMemcpyDeviceToHost,
          (cudaStream_t)*copy_d2h_Stream));
      CUDA_CALL(cudaStreamSynchronize(*copy_d2h_Stream));
    }

    FinishOrProceed(task);
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunPcieReduceLoopOnce() {
  BPS_CHECK(BytePSGlobal::IsCrossPcieSwitch());
  QueueType this_op = PCIE_REDUCE;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  if (task) {
    auto reducer = BytePSGlobal::GetCpuReducer();
    if (!reducer->isRoot()) {
      // send signal to root
      int rank = BytePSGlobal::GetLocalRank();
      auto key = task->key;
      BytePSCommSignal sig = PCIE_REDUCE_READY;
      struct BytePSCommMsg msg = {rank, sig, key};
      reducer->getComm()->sendSignalToRoot(&msg, sizeof(BytePSCommMsg));
    } else {
      auto tensor = task->tensor;

      auto key = task->key;
      auto len = task->len;
      auto offset = task->offset;
      auto unit_len = tensor->size() / tensor->shape().num_elements();

      auto nccl = BytePSGlobal::GetNccl();
      auto nccl_root = nccl->GetRoot(key, REDUCE);
      auto nccl_size = nccl->GetSize();
      auto nccl_rank = nccl->GetRank(key, REDUCE);

      auto num_elem_per_gpu = len / nccl_size / unit_len;
      auto left_elem = (len / unit_len) - (num_elem_per_gpu * nccl_size);

      auto copy_len = num_elem_per_gpu * unit_len;
      if (left_elem && (nccl_root == nccl_rank)) {
        copy_len += left_elem * unit_len;
      }

      if (copy_len) {
        auto total_offset = offset + nccl_rank * num_elem_per_gpu * unit_len;

        // Below we assume there are only two PCIe switch
        // and we run reducer in the context of the second switch
        reducer->sum((void *)((char *)(task->cpubuff) + total_offset),
                     (void *)((char *)(task->pcie_cpubuff[0]) + total_offset),
                     copy_len, tensor->dtype());
      }
    }

    FinishOrProceed(task);
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunCompressLoopOnce() {
  QueueType this_op = COMPRESS;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  if (task) {
    BPS_CHECK(BytePSGlobal::IsRootDevice())
        << "only root device should enter COMPRESS loop";
    BPS_CHECK(task->compressor != nullptr);
    BPS_CHECK(task->compressed == nullptr);

    BPS_LOG(INFO) << "Compress Task Tensor: " << task->tensor_name;
#ifdef TIMING
    task->compress_start = std::chrono::system_clock::now();
#endif
    // spawn
    BytePSGlobal::GetThreadPool()->enqueue([task]() {
      char *data = const_cast<char *>(static_cast<const char *>(task->cpubuff) +
                                      task->offset);
      int len = task->len;
      int dtype = task->tensor->dtype();
      compressor::tensor_t grad(data, len, dtype);
      auto compressed = task->compressor->Compress(grad);
      BPS_CHECK_LE(compressed.size, len)
          << "Compressor Implementation Error "
          << ", key=" << task->key << ", src_len=" << len
          << ", compressed_len=" << compressed.size;

      task->compressed = std::make_shared<decltype(compressed)>(compressed);

      // restore rt
      auto &queue_list = task->queue_list;
      BytePSGlobal::GetScheduledQueue(queue_list[1])
          ->reset(task->key, BytePSGlobal::GetLocalSize() - 1);

      FinishOrProceed(task);
    });

  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }

  return true;
}

bool RunPushLoopOnce() {
  QueueType this_op = PUSH;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  if (task) {
#ifdef DEFAULT_PUSHPULL
    /* Minghao */
    //std::cout << "RunPushLoopOnce\n";
    // if (task->gpu_ptr) BPS_LOG(INFO) << "Push Tensor GPU ptr: " << task->gpu_ptr << "\n";
    // if (task->cpubuff) BPS_LOG(INFO) << "Push Tensor CPU buff: " << task->cpubuff << "\n";
    auto tensor = task->tensor;
    /////////////
    BPS_CHECK(BytePSGlobal::IsRootDevice())
        << "only root device should enter PUSH loop";

    if (BytePSGlobal::IsDistributed()) {
      auto offset = task->offset;
      auto len = task->len;

      char *data;
      BPS_CHECK(task->cpubuff);
      data = ((char*)(task->cpubuff) + offset);
      // data =
      //     const_cast<char *>(static_cast<const char *>(task->cpubuff) + offset);

      // get metadata
      int dtype = task->tensor->dtype();

#ifdef USE_INCA
      if (task->tensor_name[7] == 'G')
        // NOTE: for testing only, change back!!!
        // BytePSGlobal::GetDPDK()->DPDKPushPull(task->dpdk_key, data, len / 2, TENSOR_COMM, task->tensor_name);
        BytePSGlobal::GetDPDK()->DPDKPushPull(task->dpdk_key, data, len, TENSOR_COMM, task->tensor_name);
        // BytePSGlobal::GetDPDK()->DPDKPushPull(task->dpdk_key, data, len / sizeof(float), TENSOR_COMM, task->tensor_name);
#ifdef PS_LITE
      else{
        // false means not to delete data when SArray is deleted
        ps::SArray<char> vals(data, len, false);

        int cmd = GetCommandType(RequestType::kDefaultPushPull, dtype);
        auto &pskv = BytePSGlobal::EncodeDefaultKey(task->key, len);
        /* Minghao */
        BytePSGlobal::GetPS()->ZPush(pskv.keys, vals, pskv.lens, cmd,
                                   [task, q]() { FinishOrProceed(task); });
        
      }
#endif
#else
      /* Minghao */
      // use compressed data/len
#ifdef BYTEPS_INCA
      if (task->tensor_name[7] == 'G' && 
          (task->context->compressor_name == "oldinca" || task->context->compressor_name == "inca")){
        dtype = BYTEPS_UINT8;
        // _debugPrint_mutex.lock();
        // printf("Push tensor %s with key %d of %d elements\n", task->tensor_name.c_str(), task->key, len);
        // // printf("data ptr at %p \n", data);

        // // pack the compressed values into the first 1/4 of the cpubuff
        // for (unsigned int i = 0; i < len; ++i){
        //   // ((uint8_t*)data)[i] = (uint8_t)(((float*)data)[i]);
        //   printf("%hhu ", ((uint8_t*)data)[i]);
        // }

        // printf("\n");
        // _debugPrint_mutex.unlock();
      }
#endif
      if (task->compressed) {
        BPS_LOG(DEBUG) << "PUSH with gradient compression. key=" << task->key;
        data = task->compressed->data;
        len = task->compressed->size;
        task->compressed = nullptr;
      }
      BPS_LOG(INFO) << "Push Task Tensor: " << task->tensor_name << " key: " << task->key << " data from: " << (task->cpubuff) + offset << " len: " << len << " scale: " << task->scale << "\n";
      #ifdef TIMING
      task->communication_start = std::chrono::system_clock::now();
      #endif
      ///////////////////////////

      // false means not to delete data when SArray is deleted
      ps::SArray<char> vals(data, len, false);
      
      int cmd = GetCommandType(RequestType::kDefaultPushPull, dtype);
      if (task->context->compressor_name == "topk") 
        cmd = GetCommandType(RequestType::kTopKPushPull, dtype);
        
      auto &pskv = BytePSGlobal::EncodeDefaultKey(task->key, len);
      /* Minghao */
      BytePSGlobal::GetPS()->ZPush(pskv.keys, vals, pskv.lens, cmd,
                                   [task, q]() { FinishOrProceed(task); });
#endif //USE_INCA
    } else {
      // This is a dummy barrier for IsCrossPcieSwitch()
      BPS_CHECK(BytePSGlobal::IsCrossPcieSwitch());
      FinishOrProceed(task);
    }
#else
#ifdef BYTEPS_INCA
    if (BytePSGlobal::IsDistributed()) {
      auto offset = task->offset;
      auto len = task->len;

      char *data;
      BPS_CHECK(task->cpubuff);
      data = ((char*)(task->cpubuff) + offset);
      len = len / sizeof(float);
      _debugPrint_mutex.lock();
      // printf("Push tensor %s with key %d of %d elements\n", task->tensor_name.c_str(), task->key, len);
      // printf("data ptr at %p \n", data);
      if (task->tensor_name[7] == 'G'){
        // pack the compressed values into the first 1/4 of the cpubuff
        for (unsigned int i = 0; i < len; ++i){
          ((uint8_t*)data)[i] = (uint8_t)(((float*)data)[i]);
          // printf("%.3f -> %hhu ", ((float*)data)[i], ((uint8_t*)data)[i]);
        }
      }
      // printf("\n");
      _debugPrint_mutex.unlock();
    }
#endif // BYTEPS_INCA
    // An "empty" push-pull that does nothing
    FinishOrProceed(task);
#endif //DEFAULT_PUSHPULL
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunPullLoopOnce() {
  QueueType this_op = PULL;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  if (task) {
#ifdef DEFAULT_PUSHPULL
    /* Minghao */
    //std::cout << "RunPushLoopOnce\n";
    // if (task->gpu_ptr) BPS_LOG(INFO) << "Pull Tensor GPU ptr: " << task->gpu_ptr << "\n";
    // if (task->cpubuff) BPS_LOG(INFO) << "Pull Tensor CPU buff: " << task->cpubuff << "\n";
    auto tensor = task->tensor;
    /////////////
    BPS_CHECK(BytePSGlobal::IsRootDevice())
        << "only root device should enter PULL loop";

    auto offset = task->offset;
    auto len = task->len;

    char *data;
    BPS_CHECK(task->cpubuff);
    data = ((char*)(task->cpubuff) + offset);
    // data =
    //     const_cast<char *>(static_cast<const char *>(task->cpubuff) + offset);

#ifdef USE_INCA
    if (task->tensor_name[7] == 'G')
      FinishOrProceed(task);
#ifdef PS_LITE
    else{
      // get metadata
      int dtype = task->output->dtype();
      // false means not to delete data when SArray is deleted
      auto vals = new ps::SArray<char>(data, len, false);

      int cmd = GetCommandType(RequestType::kDefaultPushPull, dtype);
      auto &pskv = BytePSGlobal::EncodeDefaultKey(task->key, len);
      // issue pull
      BytePSGlobal::GetPS()->ZPull(pskv.keys, vals, &pskv.lens, cmd,
                                 [vals, task, q]() {
                                   delete vals;
                                   FinishOrProceed(task);
                                 });
    }
#endif // PS_LITE
#else
    // get metadata
    int dtype = task->output->dtype();

    // Minghao
    BPS_LOG(INFO) << "Pull Task Tensor: " << task->tensor_name << " key: " << task->key << " data to: " << (task->cpubuff) + offset << " len: " << len << " scale: " << task->scale << "\n";
#ifdef BYTEPS_INCA
    if (task->tensor_name[7] == 'G' && 
        (task->context->compressor_name == "oldinca" || task->context->compressor_name == "inca")){
      dtype = BYTEPS_UINT8;
    }
#endif
    ////////////

    // false means not to delete data when SArray is deleted
    auto vals = new ps::SArray<char>(data, len, false);

    int cmd = GetCommandType(RequestType::kDefaultPushPull, dtype);
    if (task->context->compressor_name == "topk") 
      cmd = GetCommandType(RequestType::kTopKPushPull, dtype);
    
    auto &pskv = BytePSGlobal::EncodeDefaultKey(task->key, len);
    // issue pull
    BytePSGlobal::GetPS()->ZPull(pskv.keys, vals, &pskv.lens, cmd,
                                 [vals, task, q]() {
                                   delete vals;
                                   FinishOrProceed(task);
                                 });
#endif // USE_INCA
#else
#ifdef USE_INCA
    BytePSGlobal::DPDKTask[task->dpdk_key] = nullptr;
#endif
    // An "empty" push-pull that does nothing
    FinishOrProceed(task);
#endif // DEFAULT_PUSHPULL
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunDecompressLoopOnce() {
  QueueType this_op = DECOMPRESS;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();
  if (task) {
    BPS_CHECK(BytePSGlobal::IsRootDevice())
        << "only root device should enter DECOMPRESS loop";
    BPS_CHECK(task->compressor != nullptr);

    BPS_LOG(INFO) << "Decompress Task Tensor: " << task->tensor_name;
#ifdef TIMING
    task->decompress_start = std::chrono::system_clock::now();
#endif
    // spawn
    BytePSGlobal::GetThreadPool()->enqueue([task]() {
      char *data = const_cast<char *>(static_cast<const char *>(task->cpubuff) +
                                      task->offset);
      auto &pskv = BytePSGlobal::EncodeDefaultKey(task->key, 0);
      auto len = pskv.lens[0];
      int dtype = task->tensor->dtype();
      compressor::tensor_t compressed(data, len, dtype);
      auto decompressed = task->compressor->Decompress(compressed);
      BPS_LOG(DEBUG) << "PULL with gradient compression. key=" << task->key;

      FinishOrProceed(task);
    });

  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }

  return true;
}

void CopyHost2Device(std::shared_ptr<byteps::common::TensorTableEntry> task) {
  auto copy_h2d_stream = BytePSGlobal::GetCopyHost2DeviceStream();
  auto tensor = task->output;
  BPS_CHECK(tensor);
  auto key = task->key;
  auto nccl = BytePSGlobal::GetNccl();
  auto nccl_root = nccl->GetRoot(key, BROADCAST);
  auto nccl_size = nccl->GetSize();
  auto nccl_rank = nccl->GetRank(key, BROADCAST);
  auto len = task->len;
  auto offset = task->offset;
  auto cpubuff = (char *)(task->cpubuff) + offset;
  BPS_CHECK(cpubuff) << task->tensor_name
                     << ": CPU buffer not initialized, size=" << len;

  auto gpu_addr = (char *)(tensor->data()) + offset;
  if (task->device == CPU_DEVICE_ID) {
    gpu_addr = (char *)(task->gpu_ptr) + offset;
  }

  auto unit_len = tensor->size() / tensor->shape().num_elements();
  auto num_elem_per_gpu = len / nccl_size / unit_len;
  auto left_elem = (len / unit_len) - (num_elem_per_gpu * nccl_size);

  auto copy_offset = nccl_rank * num_elem_per_gpu * unit_len;
  auto copy_len = num_elem_per_gpu * unit_len;
  if (left_elem && (nccl_root == nccl_rank)) {
    copy_len += left_elem * unit_len;
  }

  if (BytePSGlobal::IsUsingReduce()) {
    copy_offset = 0;
    copy_len = (BytePSGlobal::GetReduceRootByKey(key) == nccl_rank) ? len : 0;
  }

  if (copy_len) {
    CUDA_CALL(cudaMemcpyAsync(
        (void *)(gpu_addr + copy_offset), (const void *)(cpubuff + copy_offset),
        (size_t)copy_len, (cudaMemcpyKind)cudaMemcpyHostToDevice,
        (cudaStream_t)*copy_h2d_stream));
    CUDA_CALL(cudaStreamSynchronize(*copy_h2d_stream));
    /* Minghao */
    #ifdef GPU_COMPRESS
    if(tensor->dtype() == BYTEPS_FLOAT32) {
      terngrad_decompress((void *)(gpu_addr + copy_offset), task->scale, (size_t)copy_len / unit_len);
      BPS_LOG(INFO) << "Decompress Tensor: " << task->tensor_name << " start ptr: "
                    << (void *)(gpu_addr + copy_offset) << " len: " << (size_t)copy_len / unit_len
                    << " scale: " << task->scale;
    }
    //BPS_LOG(INFO) << "Host2Device NcclRank: " << nccl_rank << " Task Tensor: " << task->tensor_name << " key: " << key << " copy from: " << (const void *)(cpubuff + copy_offset) << " copy_offset: " << copy_offset << " scale: " << task->scale <<"\n";
    //BPS_LOG(INFO) << "CopyHost2Device Rank=" << BytePSGlobal::GetLocalRank();
    //BPS_LOG(INFO) << "task scale=" << task->scale;
    #endif
    /////////////
  }

  return;
}

bool RunRootCopyHost2DeviceLoopOnce() {
  QueueType this_op = COPYH2D;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();

  if (task) {
    /* Minghao */
    //BPS_LOG(INFO) << "RunRootCopyHost2DeviceLoopOnce. rank=" << BytePSGlobal::GetLocalRank();
    /////////////
    auto key = task->key;
    
    int local_rank = BytePSGlobal::GetLocalRank();
    int local_size = BytePSGlobal::GetLocalSize();

    if (local_size > 1) {
      // notify non-root devices
      // when broadcasting here, also broadcast the scale and the norm
      struct BytePSCommMsg msg = {local_rank, DO_COPYH2D, key, task->scale, task->norm};
      // printf("root broadcast norm %.6f\n", task->norm);
      BytePSGlobal::GetBasicComm()->broadcastSignal(&msg,
                                                    sizeof(BytePSCommMsg));
    }
    CopyHost2Device(task);

    FinishOrProceed(task);
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

bool RunNonRootCopyListenLoopOnce() {
  auto signal_comm = BytePSGlobal::GetBasicComm();
  int root = signal_comm->getRoot();
  int rank = BytePSGlobal::GetLocalRank();
  BPS_CHECK_NE(root, rank);

  struct BytePSCommMsg msg = {};

  signal_comm->recvSignalFromRoot(&msg, sizeof(BytePSCommMsg));
  if (BytePSGlobal::ShouldShutdown()) return true;
  //BPS_CHECK_EQ(msg.signal, DO_COPYH2D) << msg.signal;
  if(msg.signal == DO_COPYD2H){
    BytePSGlobal::GetContextCopyTable()->AddReadyCount(msg.key);
    // Minghao: update the key's scale
    BytePSGlobal::GetContextCopyTable()->SetKeyScale(msg.key, msg.scale);
  }
  if(msg.signal == DO_COPYH2D){
    BytePSGlobal::GetCopyTable()->AddReadyCount(msg.key);
    // Minghao: update the key's scale and norm
    //BytePSGlobal::GetCopyTable()->SetKeyScale(msg.key, msg.scale);
    BytePSGlobal::GetCopyTable()->SetKeyNorm(msg.key, msg.norm);
    // printf("non-root received norm %.6f\n", msg.norm);
  }
  BPS_LOG(TRACE) << "NonRootCopyListenLoop recved from root"
                 << ", signal=" << msg.signal << ", key=" << msg.key
                 << ", myrank=" << rank;
  return true;
}

bool RunNonRootCopyHost2DeviceLoopOnce() {
  QueueType this_op = COPYH2D;
  auto q = BytePSGlobal::GetScheduledQueue(this_op);
  auto task = q->getTask();

  if (task) {
    /* Minghao */
    //BPS_LOG(INFO) << "RunNonRootCopyHost2DeviceLoopOnce. rank=" << BytePSGlobal::GetLocalRank();
    /////////////
    CopyHost2Device(task);
    FinishOrProceed(task);
  } else {
    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
  }
  return true;
}

void CoordinateReduceLoop() {
  // Minghao
  std::cout << "CoordinateReduceLoop\n";
  /////////////////
  while (RunCoordinateLoopOnce(COORDINATE_REDUCE) &&
         !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void CoordinateBroadcastLoop() {
  // Minghao
  std::cout << "CoordinateBroadbastLoop\n";
  /////////////////
  while (RunCoordinateLoopOnce(COORDINATE_BROADCAST) &&
         !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void CoordinatePushLoop() {
  // Minghao
  std::cout << "CoordinatePushLoop\n";
  /////////////////
  while (RunCoordinateLoopOnce(COORDINATE_PUSH) &&
         !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void PcieReduceLoop() {
  // Minghao
  std::cout << "PcieReduceLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunPcieReduceLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void RootNcclLoop() {
  // Minghao
  std::cout << "RootNcclLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunRootNcclLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void NonRootNcclLoop() {
  // Minghao
  std::cout << "NonRootNcclLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunNonRootNcclLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void SyncNcclLoop() {
  // Minghao
  std::cout << "SyncNcclLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunSyncNcclOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void ContextPushLoop(){
  while (RunContextPushLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void ContextPullLoop(){
  while (RunContextPullLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void CopyDevice2HostLoop() {
  // Minghao
  std::cout << "CopyDevice2HostLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunCopyDevice2HostLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void CompressLoop() {
  // Minghao
  std::cout << "CompressLoop\n";
  /////////////////
  while (RunCompressLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void PushLoop() {
  // Minghao
  std::cout << "PushLoop\n";
  /////////////////
  while (RunPushLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void PullLoop() {
  // Minghao
  std::cout << "PullLoop\n";
  /////////////////
  while (RunPullLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void DecompressLoop() {
  // Minghao
  std::cout << "DecompressLoop\n";
  /////////////////
  while (RunDecompressLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void RootCopyHost2DeviceLoop() {
  // Minghao
  std::cout << "RootCopyHost2DeviceLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunRootCopyHost2DeviceLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void NonRootCopyListenLoop() {
  // Minghao
  std::cout << "NonRootCopyListenLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunNonRootCopyListenLoopOnce() && !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

void NonRootCopyHost2DeviceLoop() {
  // Minghao
  std::cout << "NonRootCopyHost2DeviceLoop\n";
  /////////////////
  CUDA_CALL(cudaSetDevice(BytePSGlobal::GetLocalRank()));
  while (RunNonRootCopyHost2DeviceLoopOnce() &&
         !BytePSGlobal::ShouldShutdown()) {
  }
  BytePSGlobal::ReportThreadFinish();
}

}  // namespace common
}  // namespace byteps
