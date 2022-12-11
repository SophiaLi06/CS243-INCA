# Copyright 2019 Bytedance Inc. All Rights Reserved.
# Copyright 2019 Uber Technologies, Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from contextlib import contextmanager

from byteps.torch.compression import Compression
from byteps.torch.ops import push_pull_async_inplace as byteps_push_pull
from byteps.torch.ops import push_pull
from byteps.torch.ops import poll, synchronize, declare, send_norm, get_num_worker, get_worker_id
from byteps.torch.ops import init, shutdown, suspend, resume
from byteps.torch.ops import size, local_size, rank, local_rank

import os
import torch
import collections
import io
import cloudpickle

import time
import numpy as np


class _DistributedOptimizer(torch.optim.Optimizer):
    def __init__(self, params, named_parameters, compression,
                 backward_passes_per_step=1):
        super(self.__class__, self).__init__(params)
        
        # use BytePS' server or not
        self._use_bps_server = True
        self._compression = compression
        if isinstance(self._compression, Compression.newinca):
            print("use INCA compression")
            self._compressor_name = "inca"
            self._use_bps_server = self._compression.use_bps_server
        elif isinstance(self._compression, Compression.inca):
            print("use old INCA compression")
            self._compressor_name = "oldinca"
            self._use_bps_server = self._compression.use_bps_server
        elif isinstance(self._compression, Compression.topk):
            print("use TopK compression")
            self._compressor_name = "topk"
            self._use_bps_server = self._compression.use_bps_server
        else:
            print("use no compression")
            self._compressor_name = "none"
 
        if named_parameters is not None:
            named_parameters = list(named_parameters)
        else:
            named_parameters = []

        self._enable_async = (int(os.getenv('BYTEPS_ENABLE_ASYNC', 0)) != 0)
        if self._enable_async:
            assert int(os.getenv('DMLC_NUM_WORKER')) > 1, \
                "Async is only valid for distributed training"
            print('BytePS: enable asynchronous training')

        # make sure that named_parameters are tuples
        if any([not isinstance(p, tuple) for p in named_parameters]):
            raise ValueError('named_parameters should be a sequence of '
                             'tuples (name, parameter), usually produced by '
                             'model.named_parameters().')

        dups = _DistributedOptimizer.find_duplicates([k for k, _ in named_parameters])
        if len(dups) > 0:
            raise ValueError('Parameter names in named_parameters must be unique. '
                             'Found duplicates: %s' % ', '.join(dups))

        if len(named_parameters) > 0:
            if isinstance(named_parameters[0][1], torch.Tensor):
                if any([not isinstance(p, torch.Tensor) for name, p in named_parameters]):
                    raise ValueError('named_parameters should consistently be a sequence of '
                                     'tuples (name, torch.Tensor)')
                self._is_tensor_instance = True
                # there is an issue when using torch.Tensor as key, so use its hash instead
                # https://github.com/pytorch/pytorch/issues/7733
                self._parameter_names = {v.__hash__(): k for k, v
                                         in sorted(named_parameters)}
                self._tensor_list = [tensor for name, tensor in named_parameters]
            else:
                self._is_tensor_instance = False
                self._parameter_names = {v: k for k, v
                                         in sorted(named_parameters)}
        else:
            self._is_tensor_instance = False
            self._parameter_names = {v: 'push_pull.noname.%s' % i
                                     for param_group in self.param_groups
                                     for i, v in enumerate(param_group['params'])}
        self.backward_passes_per_step = backward_passes_per_step
        self._push_pull_delay = {v: self.backward_passes_per_step
                                 for _, v in sorted(named_parameters)}
        self._handles = {}
        self._grad_accs = []
        self._requires_update = set()
        self._should_sync = True
        ########## Minghao: always register hooks
        self._register_hooks()
        # keep track of the next tensor to communicate, and the norm of each tensor
        self._last_insert_tensor = None
        # self._name_to_norm = {}
        self._name_to_next = {}
        self._compression_info = {}
        self.compress_overhead = 0.0
        self.decompress_overhead = 0.0
        self.compress_rotate_overhead = 0.0
        self.decompress_rotate_overhead = 0.0
        self.compress_diag_time = 0.0
        self.compress_hadamard_time = 0.0
        self.decompress_diag_time = 0.0
        self.decompress_hadamard_time = 0.0
        # if size() > 1:
        #     self._register_hooks()

        self.num_workers = get_num_worker()
        self.worker_id = get_worker_id()
        if isinstance(self._compression, Compression.newinca) \
            or isinstance(self._compression, Compression.inca):
            self.inca_tensor = (torch.zeros(self._compression.d).cuda()).to(torch.uint8)

        if isinstance(self._compression, Compression.topk):
            self.topk_tensor = torch.zeros(2*int(self._compression.d * self._compression.kp)).cuda()

        # declare tensors
        for name in sorted(self._parameter_names.values()):
            declare("Gradient."+name)
        # We use two loops for load-balancing
        for name in sorted(self._parameter_names.values()):
            declare("Parameter."+name)

    @staticmethod
    def find_duplicates(lst):
        seen = set()
        dups = set()
        for el in lst:
            if el in seen:
                dups.add(el)
            seen.add(el)
        return dups

    def set_backward_passes_per_step(self, passes):
        self.backward_passes_per_step = passes
        for p in self._push_pull_delay:
            self._push_pull_delay[p] = self.backward_passes_per_step

    def _register_hooks(self):
        for param_group in self.param_groups:
            for p in param_group['params']:
                if p.requires_grad:
                    p.grad = p.data.new(p.size()).zero_()
                    self._requires_update.add(p)
                    p_tmp = p.expand_as(p)
                    grad_acc = p_tmp.grad_fn.next_functions[0][0]
                    grad_acc.register_hook(self._make_hook(p))
                    self._grad_accs.append(grad_acc)

    def batch_push_pull_info(self, batch_grads):
        self._compression_info['max_coor'] = batch_grads.max().item()
        self._compression_info['min_coor'] = batch_grads.min().item()
        self._compression_info['max_norm'] = batch_grads.norm(2).item()
        # print(norms)
        return self._compression_info['max_norm']

    def _push_pull_info(self, param_handles):
        """
        This function should be used for communicating the norm of the first tensor 
        to aggregate for each round. 
        """
        #first directly set the field
        self._compression_info['max_coor'] = max([torch.max(p.grad).item() for p, _ in param_handles])
        self._compression_info['min_coor'] = min([torch.min(p.grad).item() for p, _ in param_handles])
        self._compression_info['max_norm'] = max([p.grad.norm(2).item() for p, _ in param_handles])
        # print(norms)
        return self._compression_info['max_norm']

    def _push_pull_max_norm(self, max_norm):
        for i in range(self.num_workers):
            self.max_info_tensor[i] = 0.0
        self.max_info_tensor[self.worker_id] = max_norm
        # we want to find the maximum value, so no averaging
        handle = byteps_push_pull(self.max_info_tensor, average=False, name="Max_info")
        return handle

    def _afterbatch_together_push_pull_grad_async(self, tensor):
        # self.inca_tensor[:] = tensor.to(torch.uint8)
        self.inca_tensor = tensor.to(torch.uint8)
        handle = byteps_push_pull(self.inca_tensor, average=True, name="Gradient.INCA_batched", \
            compressor_name=self._compressor_name)
        return handle

    def _afterbatch_push_pull_grad_async(self, p):
        if self._is_tensor_instance:
            name = self._parameter_names.get(p.__hash__())
        else:
            name = self._parameter_names.get(p)
        if self._enable_async:
            # the real handle will be created in step()
            handle = None
        else:
            tensor = p.grad.to(torch.uint8)
            handle = byteps_push_pull(tensor, average=True, name="Gradient."+name, \
                compressor_name=self._compressor_name)
        return handle

    def _topk_push_pull_grad_async(self, tensor):
        self.topk_tensor[:] = tensor[:len(self.topk_tensor)]
        # print("topk tensor size: ", len(self.topk_tensor), " topk tensor: ", self.topk_tensor)
        # TODO: need to think about this average thing and how to handle this tensor on the server side
        handle = byteps_push_pull(self.topk_tensor, average=False, name="Topk_batched", \
            compressor_name=self._compressor_name)
        return handle

    def _push_pull_grad_async(self, p):
        if self._is_tensor_instance:
            name = self._parameter_names.get(p.__hash__())
        else:
            name = self._parameter_names.get(p)
        if self._enable_async:
            # the real handle will be created in step()
            handle, ctx = None, None
        else:
            tensor = p.grad
            ######### Minghao
            # print(tensor.size())
            #########
            tensor_compressed, ctx = self._compression.compress(tensor, name, self._compression_info)
            handle = byteps_push_pull(tensor_compressed, average=True, name="Gradient."+name, \
                compressor_name=self._compressor_name)
        return handle, ctx

    def _make_hook(self, p):
        def hook(*ignore):
            if p in self._handles and self._handles[p][0] is not None:
                if self._push_pull_delay[p] <= 0:
                    raise AssertionError(
                        "Gradients were computed more than "
                        "backward_passes_per_step times before call "
                        "to step(). Increase backward_passes_per_step to "
                        "accumulate gradients locally.")
            assert not p.grad.requires_grad
            assert self._push_pull_delay[p] > 0
            self._push_pull_delay[p] -= 1
            # if p in self._handles and self._handles[p][0] is not None:
            #     return
            handle, ctx = None, None
            if self._is_tensor_instance:
                tensor_name = self._parameter_names.get(p.__hash__())
            else:
                tensor_name = self._parameter_names.get(p)

            self._handles[p] = (handle, ctx)

            if self._last_insert_tensor:
                # update the "next" record for the last inserted tensor
                self._name_to_next[self._last_insert_tensor] = tensor_name

            self._last_insert_tensor = tensor_name
        return hook

    def synchronize(self):
        missing_p = self._requires_update - set(self._handles.keys())
        for p in missing_p:
            if self._is_tensor_instance:
                tensor_name = self._parameter_names.get(p.__hash__())
            else:
                tensor_name = self._parameter_names.get(p)

            if self._last_insert_tensor:
                # update the "next" record for the last inserted tensor
                self._name_to_next[self._last_insert_tensor] = tensor_name

            self._last_insert_tensor = tensor_name
            handle, ctx = None, None
            self._handles[p] = (handle, ctx)

        param_handles = list(self._handles.items())
        param_names = []

        if isinstance(self._compression, Compression.newinca) \
            or isinstance(self._compression, Compression.inca):
        
            grads = []
            aggregated_grads = []
            # NOTE: disable the following line (216-221) for running without norm communication
            for p, value in param_handles:
                if self._is_tensor_instance:
                    tensor_name = self._parameter_names.get(p.__hash__())
                else:
                    tensor_name = self._parameter_names.get(p)
                param_names.append(tensor_name)
                grads.append(p.grad.view(-1))
        
            grads = torch.cat(grads)
            # print("length of grads: ", len(grads))
        
            self.batch_push_pull_info(grads)

            if not self._use_bps_server:
                global_max_norm = send_norm("Info.batched_max_norm", self._compression_info['max_norm'])
                # print("global_max_norm: ", global_max_norm)
                if global_max_norm:
                    self._compression_info['max_norm'] = global_max_norm

            else:
                tmp_max = self._compression_info['max_norm']
                # print("max_norm before: ", str(self._compression_info['max_norm']))

                max_info_handle = self._push_pull_max_norm(self._compression_info['max_norm'])
                max_info_output = synchronize(max_info_handle)
                self._compression_info['max_norm'] = max_info_output.max().item()
                # print("max_info_output: ", self._compression_info['max_norm'])
                if self._compression_info['max_norm'] == 0.0:
                    self._compression_info['max_norm'] = tmp_max

            if not grads.is_cuda:
                print("WARNING: grads to compress is not on CUDA")

            batch_compressed, ctx = self._compression.compress(grads, "batch_grads", self._compression_info)
            # torch.cuda.synchronize()

            ############################ batches communication
            # inca_handle = self._afterbatch_together_push_pull_grad_async(batch_compressed[:self._compression.d])
            # aggregated_grads.append(synchronize(inca_handle).view(-1))
            # if self._compression.d < len(batch_compressed):
            #     aggregated_grads.append(batch_compressed[self._compression.d:])

            ############################ Layer by layer communication
            offset = 0
            for p, _ in self._handles.items():
                slice_size = len(p.grad.view(-1))
                grad_slice = batch_compressed[offset:offset + slice_size]
                offset += slice_size

                temp = grad_slice.resize_(p.grad.shape)
                p.grad = temp.clone()

                handle = self._afterbatch_push_pull_grad_async(p)
                self._handles[p] = (handle, ctx) # NOTE: this ctx is for the batched tensor

            for p, (handle, _) in self._handles.items():
                output = synchronize(handle)
                self._push_pull_delay[p] = self.backward_passes_per_step
                aggregated_grads.append(output.view(-1))
            
            # # append the padded gradients
            aggregated_grads.append(batch_compressed[offset:])
            ############################
            aggregated_grads = torch.cat(aggregated_grads)

            if not aggregated_grads.is_cuda:
                print("WARNING: aggregated_grads to decompress is not on CUDA")

            if not self._enable_async:
                batch_decompressed = self._compression.decompress(aggregated_grads.to(torch.float32), ctx)
                # torch.cuda.synchronize()

                if ctx:
                    self.compress_overhead += ctx['compress_overhead']
                    self.decompress_overhead += ctx['decompress_overhead']
                    
                if ctx and (isinstance(self._compression, Compression.newinca) \
                    or isinstance(self._compression, Compression.inca)):
                    self.compress_rotate_overhead += ctx['compress_rotate_overhead']
                    self.decompress_rotate_overhead += ctx['decompress_rotate_overhead']
                    self.compress_diag_time += ctx['compress_diag_time']
                    self.compress_hadamard_time += ctx['compress_hadamard_time']
                    self.decompress_diag_time += ctx['decompress_diag_time']
                    self.decompress_hadamard_time += ctx['decompress_hadamard_time']

                offset = 0
                for p, _ in self._handles.items():
                    slice_size = len(p.grad.view(-1))
                    grad_slice = batch_decompressed[offset:offset + slice_size]
                    offset += slice_size

                    temp = grad_slice.resize_(p.grad.shape)
                    p.grad = temp.clone()
                    ####################
                    # after batched push_pull, we need to reset this delay
                    # self._push_pull_delay[p] = self.backward_passes_per_step
                    ####################
            if self._use_bps_server:
                self._handles["max_info_handle"] = (max_info_handle, None)
            # self._handles["inca_handle"] = (inca_handle, None)
            # if self.inca_additional:
            #     self._handles["inca_handle"] = (inca_handle, None)

        ############################################################################
        elif isinstance(self._compression, Compression.topk):
            grads = []
            aggregated_grads = []
            # NOTE: disable the following line (216-221) for running without norm communication
            for p, value in param_handles:
                if self._is_tensor_instance:
                    tensor_name = self._parameter_names.get(p.__hash__())
                else:
                    tensor_name = self._parameter_names.get(p)
                param_names.append(tensor_name)
                grads.append(p.grad.view(-1))
        
            grads = torch.cat(grads)
        
            self.batch_push_pull_info(grads)

            batch_compressed, ctx = self._compression.compress(grads, "batch_grads", self._compression_info)
            # torch.cuda.synchronize()

            topk_handle = self._topk_push_pull_grad_async(batch_compressed)
            batched_output = synchronize(topk_handle).view(-1)
            # print("batched_output: ", batched_output, "batched_output.shape: ", batched_output.shape)

            if not self._enable_async:
                batch_decompressed = self._compression.decompress(batched_output, ctx)
                # torch.cuda.synchronize()

                if ctx:
                    self.compress_overhead += ctx['compress_overhead']
                    self.decompress_overhead += ctx['decompress_overhead']

                offset = 0
                for p, _ in self._handles.items():
                    slice_size = len(p.grad.view(-1))
                    grad_slice = batch_decompressed[offset:offset + slice_size]
                    offset += slice_size

                    temp = grad_slice.resize_(p.grad.shape)
                    p.grad = temp.clone()
                    # reset the pushpull delay
                    self._push_pull_delay[p] = self.backward_passes_per_step
                    
            # self._handles["topk_handle"] = (topk_handle, None)
        
        else:
        # NOTE: use below for the original communication of all tensors at once / one by one
            for p, value in param_handles:
                if self._is_tensor_instance:
                    tensor_name = self._parameter_names.get(p.__hash__())
                else:
                    tensor_name = self._parameter_names.get(p)
                param_names.append(tensor_name)
            
            self._push_pull_info(param_handles)

            # for tensor_name in param_names:
            #     send_norm("Info."+tensor_name, self._compression_info['max_norm'])

            for i in range(len(param_handles)):
                p, value = param_handles[i]
                # time.sleep(0.05)
                handle, ctx = self._push_pull_grad_async(p)
                # print("make handle for " + ctx['name'])
                self._handles[p] = (handle, ctx)
            for p, (handle, ctx) in self._handles.items():
                output = synchronize(handle)
                self._push_pull_delay[p] = self.backward_passes_per_step
                if not self._enable_async:
                    p.grad.set_(self._compression.decompress(output, ctx))
                    if ctx:
                        self.compress_overhead += ctx['compress_overhead']
                        self.decompress_overhead += ctx['decompress_overhead']

        self._handles.clear()

    @contextmanager
    def skip_synchronize(self):
        if self._enable_async:
            raise AssertionError("skip_synchronize cannot be used in async training")
        self._should_sync = False
        try:
            yield
        finally:
            self._should_sync = True

    def step(self, closure=None):
        if self._enable_async:
            old_weight_map = {}
            # store the weights before update
            for p, _ in self._handles.items():
                old_weight_map[p] = p.data.clone().detach()
            # update
            loss = super(self.__class__, self).step(closure)

            for p, (h, _) in self._handles.items():
                # get the diff for each weight (in-place)
                p.data.sub_(old_weight_map.get(p))
                if h is None:
                    # create the handler now
                    if self._is_tensor_instance:
                        name = self._parameter_names.get(p.__hash__())
                    else:
                        name = self._parameter_names.get(p)
                    handle = byteps_push_pull(p, average=False, name="AsyncParam."+name)
                    _, ctx = self._compression.compress(p, name)
                    ############Minghao
                    # print("step " + name)
                    # print("in step after compress")
                    ###############
                    self._handles[p] = (handle, ctx)
                    tensor_name = ctx['name']
                    print("insert handle for " + tensor_name + " in step")
                    if self._last_insert_tensor:
                        # update the "next" record for the last inserted tensor
                        self._name_to_next[self._last_insert_tensor] = tensor_name
                    # self._name_to_norm[tensor_name] = 0.0
                    self._last_insert_tensor = tensor_name

            self.synchronize()
            return loss
        else:
            # skip sync if calling skip_synchronize
            if self._should_sync:
                self.synchronize()
            return super(self.__class__, self).step(closure)


def DistributedOptimizer(optimizer, named_parameters=None,
                         compression=Compression.none,
                         backward_passes_per_step=1):
    """
    An optimizer that wraps another torch.optim.Optimizer, using an push_pull to
    average gradient values before applying gradients to model weights.
    push_pull operations are executed after each gradient is computed by `loss.backward()`
    in parallel with each other. The `step()` method ensures that all push_pull operations are
    finished before applying gradients to the model.
    DistributedOptimizer exposes the `synchronize()` method, which forces push_pull operations
    to finish before continuing the execution. It's useful in conjunction with gradient
    clipping, or other operations that modify gradients in place before `step()` is executed.
    Example of gradient clipping:
    ```
    output = model(data)
    loss = F.nll_loss(output, target)
    loss.backward()
    optimizer.synchronize()
    torch.nn.utils.clip_grad_norm(model.parameters(), args.clip)
    optimizer.step()
    ```
    Arguments:
        optimizer: Optimizer to use for computing gradients and applying updates.
        named_parameters: A mapping between parameter names and values. Used for naming of
                          push_pull operations. Typically just `model.named_parameters()`.
        compression: Compression algorithm used during push_pull to reduce the amount
                     of data sent during the each parameter update step.  Defaults to
                     not using compression.
        backward_passes_per_step: Number of expected backward passes to perform
                                  before calling step()/synchronize(). This
                                  allows accumulating gradients over multiple
                                  mini-batches before executing averaging and
                                  applying them.
    """
    # We dynamically create a new class that inherits from the optimizer that was passed in.
    # The goal is to override the `step()` method with an push_pull implementation.
    cls = type(optimizer.__class__.__name__, (optimizer.__class__,),
               dict(_DistributedOptimizer.__dict__))
    return cls(optimizer.param_groups, named_parameters,
               compression, backward_passes_per_step)


def broadcast_parameters(params, root_rank, prefix="Parameter."):
    """
    Broadcasts the parameters from root rank to all other processes.
    Typical usage is to broadcast the `model.state_dict()`,
    `model.named_parameters()`, or `model.parameters()`.
    Arguments:
        params: One of the following:
            - list of parameters to broadcast
            - dict of parameters to broadcast
        root_rank: The rank of the process from which parameters will be
                   broadcasted to all other processes.
    """
    if isinstance(params, dict):
        params = sorted(params.items())
    elif isinstance(params, list):
        # support both named_parameters() and regular parameters()
        params = [p if isinstance(p, tuple) else (None, p) for p in params]
    else:
        raise ValueError('invalid params of type: %s' % type(params))

    # Run synchronous broadcasts.
    for name, p in params:
        # Broadcast is implemented as push + pull in BytePS
        # To make it a real broadcast, we set the non-root tensors all 0.
        if rank() != root_rank:
            p.fill_(0)
        # Remember to disable averaging because we are doing broadcast
        if name:
            handle = byteps_push_pull(p, average=False, name=prefix+name)
        else:
            handle = byteps_push_pull(p, average=False)
        synchronize(handle)


def broadcast_optimizer_state(optimizer, root_rank, prefix="Parameter."):
    """
    Broadcasts an optimizer state from root rank to all other processes.
    Arguments:
        optimizer: An optimizer.
        root_rank: The rank of the process from which the optimizer will be
                   broadcasted to all other processes.
    """
    if isinstance(optimizer, torch.optim.LBFGS):
        # TODO(travis): L-BFGS cannot be easily supported without serializing
        # the entire state_dict, as its structure is deeply nested and contains
        # None type parameter values
        raise ValueError('cannot broadcast torch.optim.LBFGS state')

    state_dict = optimizer.state_dict()

    # Newly created optimizers will not have their state initialized, so
    # do that initialization here
    if len(state_dict['state']) == 0:
        for group in optimizer.param_groups:
            for p in group['params']:
                p.grad = p.data.new(p.size()).zero_()
        # This function accepts a torch.optim.Optimizer or a DistributedOptimizer
        # wrapped around a torch optimizer. Calling step() with a DistributedOptimizer
        # forces push_pull on all model parameters, which will result in deadlock
        # unless every rank calls step(). Therefore, to finish state initialization
        # only call optimizer.step() with a torch.optim.Optimizer.
        if optimizer.__module__ == DistributedOptimizer.__module__:
            super(optimizer.__class__, optimizer).step()
        else:
            optimizer.step()
        state_dict = optimizer.state_dict()

    # If the state_dict is still empty after initialization, then
    # the optimizer is stateless, and there is nothing to broadcast.
    # Furthermore, attempting to access the state dict would result in
    # an error.
    if len(state_dict['state']) == 0:
        return

    params = []
    scalars = {}
    callbacks = {}
    occurrences = collections.defaultdict(int)

    # Returns the full type structure of the possibly nested objects for recursive casting back
    def _get_types(x):
        if isinstance(x, collections.Iterable):
            return type(x), [_get_types(xi) for xi in x]
        else:
            return type(x)

    # Casts an object encoded in a tensor back into its original type and subtypes
    def _recursive_cast(x, dtype):
        if isinstance(dtype, tuple):
            t, dtypes = dtype
            x = t(x)
            return t([_recursive_cast(x[i], dtypes[i]) for i in range(len(x))])
        else:
            return dtype(x)

    # Some optimizer parameters may be represented as scalars instead of
    # tensors.  In such cases, we place the scalars into a single dict,
    # then pickle and broadcast with broadcast_object (under the assumption
    # that there are not many scalars, and so the overhead of pickling will
    # be relatively low). Because broadcast_object is performed out-of-place,
    # we then use a callback to assign the new value to the correct element
    # of the optimizer state.
    def _create_state_callback(pid, name):
        def _assign_state(v):
            state_dict['state'][pid][name] = v
        return _assign_state

    def _create_option_callback(index, option_key):
        def _assign_option(v):
            optimizer.param_groups[index][option_key] = v
        return _assign_option

    # Param groups are an ordered list, normally there is only one per model,
    # but users can add additional param groups for example to train
    # previously frozen layers
    for index, group in enumerate(state_dict['param_groups']):
        # Broadcast options like learning rate
        for option_key, option_value in group.items():
            if option_key == 'params':
                continue

            # Options like the learning rate are scalar, and need to be broadcast separately
            key = '%s.%d' % (option_key, index)
            dtypes = _get_types(option_value)
            option_tensor = torch.Tensor([option_value]).cuda()
            scalars[key] = option_value
            callbacks[key] = _create_option_callback(index, option_key)

        # The params list here is ordered by the layers in the model
        for pid in group['params']:
            if pid not in state_dict['state']:
                # The param has not set requires_grad, so skip broadcast
                continue

            param_state = state_dict['state'][pid]
            for name, p in param_state.items():
                # Some parameter names may appear more than once, in which
                # case we ensure they have a unique identifier defined by
                # their order
                occurrences[name] += 1
                key = '%s.%d' % (str(name), occurrences[name])

                if torch.is_tensor(p):
                    # Tensor -> use broadcast_parameters
                    params.append((key, p))
                else:
                    # Scalar -> use broadcast_object
                    scalars[key] = p
                    callbacks[key] = _create_state_callback(pid, name)

    # Synchronized broadcast of all parameters
    broadcast_parameters(params, root_rank, prefix)

    # Broadcast and cleanup for non-tensor parameters
    scalars = broadcast_object(scalars, root_rank)
    for key, p in scalars.items():
        callbacks[key](p)

def broadcast_object(obj, root_rank=0, name=None):
    """
    Serializes and broadcasts an object from root rank to all other processes.
    Typical usage is to broadcast the `optimizer.state_dict()`, for example:

    .. code-block:: python

        state_dict = broadcast_object(optimizer.state_dict(), 0)
        if bps.rank() > 0:
            optimizer.load_state_dict(state_dict)

    Arguments:
        obj: An object capable of being serialized without losing any context.
        root_rank: The rank of the process from which parameters will be
                   broadcasted to all other processes.
        name: Optional name to use during broadcast, will default to the class
              type.
    Returns:
        The object that was broadcast from the `root_rank`.
    """
    if name is None:
        name = type(obj).__name__

    if rank() == root_rank:
        b = io.BytesIO()
        cloudpickle.dump(obj, b)
        t = torch.ByteTensor(bytearray(b.getvalue()))
        sz = torch.IntTensor([t.shape[0]])
        broadcast_parameters([(name + '.sz', sz)], root_rank, prefix="Size.")
    else:
        sz = torch.IntTensor([0])
        broadcast_parameters([(name + '.sz', sz)], root_rank, prefix="Size.")
        t = torch.ByteTensor(sz.tolist()[0])

    broadcast_parameters([(name + '.t', t)], root_rank, prefix="Parameter.")

    if rank() != root_rank:
        buf = io.BytesIO(t.numpy().tobytes())
        obj = cloudpickle.load(buf)

    return obj
    