# Copyright 2019 Bytedance Inc. All Rights Reserved.
# Copyright 2018 Uber Technologies, Inc. All Rights Reserved.
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
"""Gradient compression algorithms."""

import torch
import numpy as np
import warnings
import hadamard_cuda
import time

from scipy.stats import norm


class Compressor(object):
    """Interface for compressing and decompressing a given tensor."""
    @staticmethod
    def compress(tensor, name=None, info=None):
        """Compresses a tensor and returns it with the context needed to decompress it."""
        pass

    @staticmethod
    def decompress(tensor, ctx):
        """Decompress the tensor with the given context."""
        pass

################################################################################
################################################################################

class NoneCompressor(Compressor):
    """Default no-op compression."""
    @staticmethod
    def compress(tensor, name=None, info=None):
        """Returns the tensor unmodified."""
        return tensor, None

    @staticmethod
    def decompress(tensor, ctx):
        """Returns the tensor unmodified."""
        return tensor

################################################################################
################################################################################

class TopKCompressor(Compressor):
    def __init__(self, params):
        self.kp = params['kp']
        self.ef = params.get('ef', True)
        self.d = params['d']
        self.use_bps_server = params.get('use_bps_server', True)
        
        if self.ef:
            
            self.errors = {}

    """
    This function returns a tensor of tuples (index, value), i.e.,
    the 2*i-th element is the index of the top i-th coordinate of the input tensor
    and the (2*i+1)-th element if the value of the top i-th coordinate.
    """
    def compress(self, tensor, name, info=None):
        all_start = torch.cuda.Event(enable_timing=True)
        all_end = torch.cuda.Event(enable_timing=True)
        all_start.record()

        # print("compress", name)
        # print(tensor)

        orig_size = tensor.size()
        tensor = tensor.view(-1)
        d = tensor.numel()
        compressed_num = int(self.kp * d)
        if d < 0:
            res = tensor.detach().clone()
        else:
            res = torch.zeros(size=(d,), device=tensor.device)

            if self.ef:
                self.errors[name] = tensor + self.errors.get(name, 0)
               
                sort, idx = self.errors[name].abs().sort(descending=True)

                res[:compressed_num] = idx[:compressed_num]
                res[compressed_num : 2*compressed_num] = self.errors[name][idx[:compressed_num]]

                self.errors[name][idx[:compressed_num]] = 0

            else:
               
                sort, idx = tensor.abs().sort(descending=True)

                res[:compressed_num] = idx[:compressed_num]
                res[compressed_num : 2*compressed_num] = tensor[idx[:compressed_num]]
        
        all_end.record()
        torch.cuda.synchronize()

        return res, {'size': orig_size, 'compress_overhead': all_start.elapsed_time(all_end), 'name': name}

    def decompress(self, tensor, ctx):
        ## use the code below to do actually decompression
        all_start = torch.cuda.Event(enable_timing=True)
        all_end = torch.cuda.Event(enable_timing=True)
        all_start.record()


        # # Find the number of elements in the original tensor
        d = 1
        for width in ctx['size']:
            d *= width
        compressed_num = int(self.kp * d)

        if d < 0:
            res = tensor.detach().clone()
        else:
            res = torch.zeros(size=(d,), device=tensor.device)
            res.index_copy_(0, (tensor[:compressed_num]).long().clamp(max=d-1), tensor[compressed_num:2*compressed_num])
                

        all_end.record()
        torch.cuda.synchronize()
        ctx['decompress_overhead'] = all_start.elapsed_time(all_end)

        return res.view(ctx['size'])


################################################################################
################################################################################

class DummyCompressor(Compressor):
    def __init__(self, params):
        self.idx = params.get('modify_idx')
        self.temps = {}

    def dummy_diagonal(self, tensor):
        if not tensor.is_cuda:
            print("dummy_diagonal not GPU tensor!")
        return tensor
    
    def dummy_hadamard(self, tensor):
        if not tensor.is_cuda:
            print("dummy_hadamard not GPU tensor!")
        return tensor

    def dummy_rotation(self, tensor):
        tensor = self.dummy_diagonal(tensor)
        tensor = self.dummy_hadamard(tensor)
        return tensor

    def dummy_inverse_rotation(self, tensor):
        tensor = self.dummy_hadamard(tensor)
        tensor = self.dummy_diagonal(tensor)
        return tensor

    def dummy_sq_func(self, tensor):
        cloned_vec = tensor.clone()
        return cloned_vec

    def compress(self, tensor, name=None, info=None):


        sq_func = self.dummy_sq_func
        orig_size = tensor.size()
        tensor = tensor.view(-1)
        tensor[self.idx] = -tensor[self.idx]

        tensor = self.dummy_rotation(tensor)
        tensor = sq_func(tensor)
        self.temps[name] = tensor + self.temps.get(name, 0)
        self.temps[name] -= self.dummy_inverse_rotation(tensor)

      
        return tensor, {'size': orig_size, \
            'compress_overhead': 0, 'compress_rotate_overhead': 0,\
            'compress_diag_time': 0, 'compress_hadamard_time': 0}

    def decompress(self, tensor, ctx):
        

        tensor = tensor.view(-1)
        tensor[self.idx] = -tensor[self.idx]
        tensor = self.dummy_inverse_rotation(tensor)

        ctx['decompress_overhead'] = 0
        ctx['decompress_rotate_overhead'] = 0
        ctx['decompress_diag_time'] = 0
        ctx['decompress_hadamard_time'] = 0
        return tensor.view(ctx['size'])

################################################################################
################################################################################

# Becasue INCA is a part of an on-going research project, we are not able to
# release the code publicly at this time. Please contact Minghao Li if you
# are interested in knowing more details of INCA.

################################################################################
################################################################################

class FP16Compressor(Compressor):
    """Compress all floating point gradients to 16-bit."""
    @staticmethod
    def compress(tensor, name=None, max_norm=0.0):
        """Downcasts the tensor to 16-bit."""
        tensor_compressed = tensor
        if tensor.dtype.is_floating_point:
            # Only allow compression from other floating point types
            tensor_compressed = tensor.type(torch.float16)
        #print("torch fp16 compress")
        return tensor_compressed, tensor.dtype

    @staticmethod
    def decompress(tensor, ctx):
        """Upcasts the tensor to the initialization dtype."""
        tensor_decompressed = tensor
        dtype = ctx
        if dtype.is_floating_point:
            tensor_decompressed = tensor.type(dtype)
        #print("torch fp16 decompress")
        return tensor_decompressed


class Compression(object):
    """Optional gradient compression algorithm used during push_pull."""

    """Do not compress the gradients. This is the default."""
    none = NoneCompressor

    """Compress all gradients using new INCA."""
    newinca = NewINCACompressor

    """Compress all gradients using INCA."""
    inca = INCACompressor

    """Compress all floating point gradients to 16-bit."""
    fp16 = FP16Compressor

    """Compress all gradients using the dummy compressor."""
    dummy = DummyCompressor

    """Compress all gradients using the topk compressor."""
    topk = TopKCompressor
