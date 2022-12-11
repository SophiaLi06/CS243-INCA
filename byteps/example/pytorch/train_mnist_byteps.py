#!/usr/bin/env python
from __future__ import print_function
import argparse
import os
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torchvision import datasets, transforms
import torch.utils.data.distributed
import byteps.torch as bps
#######Minghao
import time
import torch
#######

# Training settings
parser = argparse.ArgumentParser(description='PyTorch MNIST Example')
parser.add_argument('--batch-size', type=int, default=64, metavar='N',
                    help='input batch size for training (default: 64)')
parser.add_argument('--test-batch-size', type=int, default=1000, metavar='N',
                    help='input batch size for testing (default: 1000)')
parser.add_argument('--epochs', type=int, default=100, metavar='N',
                    help='number of epochs to train (default: 100)')
parser.add_argument('--lr', type=float, default=0.01, metavar='LR',
                    help='learning rate (default: 0.01)')
parser.add_argument('--momentum', type=float, default=0.5, metavar='M',
                    help='SGD momentum (default: 0.5)')
parser.add_argument('--no-cuda', action='store_true', default=False,
                    help='disables CUDA training')
parser.add_argument('--seed', type=int, default=42, metavar='S',
                    help='random seed (default: 42)')
parser.add_argument('--log-interval', type=int, default=10, metavar='N',
                    help='how many batches to wait before logging training status')
parser.add_argument('--fp16-pushpull', action='store_true', default=False,
                    help='use fp16 compression during pushpull')
######## Minghao
parser.add_argument('--use-bps-server', action='store_true', default=False,
                    help='Use BytePS server')
parser.add_argument('--ef', action='store_true', default=False,
                    help='use INCA compression with error feedback')
parser.add_argument('--quant-level', type=int, default=16, metavar='N',
                    help='INCA quantization levels')
### new INCA parameters
parser.add_argument('--new-inca', action='store_true', default=False,
                    help='use INCA compression during pushpull')
parser.add_argument('--new-inca-seed', type=int, default=42,
                    help='random seed for new INCA')
parser.add_argument('--overflow-freq', type=int, default=32, 
                    help='INCA overflow frequency')
parser.add_argument('--max-val', type=int, default=30,
                    help='INCA max value')
parser.add_argument('--table-dir', type=str, default='/home/byteps/byteps/torch/tables',
                    help='directory to store the tables')
### old INCA parameters
parser.add_argument('--inca', action='store_true', default=False,
                    help='use INCA compression during pushpull')
parser.add_argument('--rotation', action='store_true', default=False,
                    help='use INCA compression with rotation')
### minmax for INCA - percentile
parser.add_argument('--percentile', default=1., type=float,
                    help='the percentile to use for minmax quantization')
### the maximum number of iterations if doing a partial rotation
parser.add_argument('--partial', default=1000., type=int,
                    help='the maximum number of iterations in the partial rotation')
parser.add_argument('--norm-normal', action='store_true', default=False,
                    help='use INCA compression with norm normalization')
parser.add_argument('--overflow-prob', type=float, default=0.0001, metavar='P',
                    help='per_coordinate_overflow_prob')
parser.add_argument('--dummy', action='store_true', default=False,
                    help='use Dummy compression during pushpull')
parser.add_argument('--topk', action='store_true', default=False,
                    help='use Dummy compression during pushpull')
parser.add_argument('--kp', type=float, default=0.1,
                        help='TopK ratio. default is 0.1.')
args = parser.parse_args()
args.cuda = not args.no_cuda and torch.cuda.is_available()

# BytePS: initialize library.
bps.init()
torch.manual_seed(args.seed)

if args.cuda:
    # BytePS: pin GPU to local rank.
    torch.cuda.set_device(bps.local_rank())
    torch.cuda.manual_seed(args.seed)

# kwargs = {'num_workers': 0, 'pin_memory': False} if args.cuda else {}
kwargs = {'num_workers': 2, 'pin_memory': True, 'persistent_workers': True} if args.cuda else {}
# kwargs = {'num_workers': 1, 'pin_memory': True} if args.cuda else {}
train_dataset = \
    datasets.MNIST('data-%d' % bps.rank(), train=True, download=True,
                   transform=transforms.Compose([
                       transforms.ToTensor(),
                       transforms.Normalize((0.1307,), (0.3081,))
                   ]))

# BytePS: use DistributedSampler to partition the training data.
train_sampler = torch.utils.data.distributed.DistributedSampler(
    train_dataset, num_replicas=bps.size(), rank=bps.rank())
train_loader = torch.utils.data.DataLoader(
    train_dataset, batch_size=args.batch_size, sampler=train_sampler, **kwargs)
print("train_loader initialized")

test_dataset = \
    datasets.MNIST('data-%d' % bps.rank(), train=False, transform=transforms.Compose([
        transforms.ToTensor(),
        transforms.Normalize((0.1307,), (0.3081,))
    ]))
# BytePS: use DistributedSampler to partition the test data.
test_sampler = torch.utils.data.distributed.DistributedSampler(
    test_dataset, num_replicas=bps.size(), rank=bps.rank())
test_loader = torch.utils.data.DataLoader(test_dataset, batch_size=args.test_batch_size,
                                          sampler=test_sampler, **kwargs)
print("test_loader initialized")

for data, target in train_loader:
    pass

for data, target in test_loader:
    pass

affinity = os.sched_getaffinity(0)  
# Print the result
print("Process is eligible to run on:", affinity)
time.sleep(10)

class Net(nn.Module):
    def __init__(self):
        super(Net, self).__init__()
        self.conv1 = nn.Conv2d(1, 10, kernel_size=5)
        self.conv2 = nn.Conv2d(10, 20, kernel_size=5)
        self.conv2_drop = nn.Dropout2d()
        self.fc1 = nn.Linear(320, 50)
        self.fc2 = nn.Linear(50, 10)

    def forward(self, x):
        x = F.relu(F.max_pool2d(self.conv1(x), 2))
        x = F.relu(F.max_pool2d(self.conv2_drop(self.conv2(x)), 2))
        x = x.view(-1, 320)
        x = F.relu(self.fc1(x))
        x = F.dropout(x, training=self.training)
        x = self.fc2(x)
        return F.log_softmax(x)


model = Net()

if args.cuda:
    # Move model to GPU.
    model.cuda()

# BytePS: scale learning rate by the number of GPUs.
optimizer = optim.SGD(model.parameters(), lr=args.lr * bps.size(),
                      momentum=args.momentum)

# Find the total number of trainable parameters of the model
pytorch_total_params = sum(p.numel() for p in model.parameters())
pytorch_total_params_trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
if (pytorch_total_params != pytorch_total_params_trainable):
    raise Exception("pytorch_total_params != pytorch_total_params_trainable")
print("Total number of trainable parameters: " + str(pytorch_total_params_trainable))
quantization_levels = {}
for param_name, _ in model.named_parameters():
    quantization_levels[param_name] = args.quant_level
# NOTE: this is for compressing the batched gradient
quantization_levels['batch_grads'] = args.quant_level

# BytePS: (optional) compression algorithm.
if args.new_inca:
    # NOTE: for now, new INCA doesn't support specifying quantization levels per layer
    compression = bps.Compression.newinca(params={'nclients': 1, 'd': pytorch_total_params_trainable, \
        'ef': args.ef, 'quantization_levels': args.quant_level, 'seed': args.new_inca_seed, \
        'overflow_frequency': args.overflow_freq, 'max_val': args.max_val, 'table_dir': args.table_dir, \
        'use_bps_server': args.use_bps_server})
elif args.inca:
    compression = bps.Compression.inca(params={'nclients': 1, 'd': pytorch_total_params_trainable, \
        'ef': args.ef, 'rotation': args.rotation, 'quantization_levels': quantization_levels, \
        'partial_rotation_times': args.partial, 'percentile': args.percentile, \
        'norm_normalization': args.norm_normal, 'per_coordinate_overflow_prob': args.overflow_prob, \
        'use_bps_server': args.use_bps_server})
elif args.dummy:
    compression = bps.Compression.dummy(params={'modify_idx': -1})
elif args.topk:
    compression = bps.Compression.topk(params={'d': pytorch_total_params_trainable, \
        'ef': args.ef, 'kp': args.kp, 'use_bps_server': args.use_bps_server})
else:
    compression = bps.Compression.fp16 if args.fp16_pushpull else bps.Compression.none

# BytePS: wrap optimizer with DistributedOptimizer.
optimizer = bps.DistributedOptimizer(optimizer,
                                     named_parameters=model.named_parameters(),
                                     compression=compression)


# BytePS: broadcast parameters.
bps.broadcast_parameters(model.state_dict(), root_rank=0)
bps.broadcast_optimizer_state(optimizer, root_rank=0)

computation_time = 0.0
data_move_time = 0.0
step_time = 0.0
prep_time = 0.0
batches_time = 0.0
load_time = 0.0
def train(epoch):
    global computation_time
    global step_time
    global data_move_time
    global prep_time
    global batches_time
    global load_time

    prep_start = torch.cuda.Event(enable_timing=True)
    prep_end = torch.cuda.Event(enable_timing=True)
    prep_start.record()

    model.train()
    # BytePS: set epoch to sampler for shuffling.
    train_sampler.set_epoch(epoch)
    print("train sampler set epoch")

    prep_end.record()
    torch.cuda.synchronize()
    prep_time += (prep_start.elapsed_time(prep_end))
    
    load_start = time.time()
    for batch_idx, (data, target) in enumerate(train_loader):
        batch_start = time.time()
        load_time += time.time() - load_start
        
        # time the computation time (except step function time)
        move_start = torch.cuda.Event(enable_timing=True)
        move_end = torch.cuda.Event(enable_timing=True)
        move_start.record()
        if args.cuda:
            data, target = data.cuda(), target.cuda()
        move_end.record()
        torch.cuda.synchronize()
        data_move_time += (move_start.elapsed_time(move_end))

        # time the computation time (except step function time)
        if epoch > 10:
            train_start = torch.cuda.Event(enable_timing=True)
            train_end = torch.cuda.Event(enable_timing=True)
            train_start.record()
        
        optimizer.zero_grad()
        output = model(data)
        loss = F.nll_loss(output, target)
        loss.backward()

        if epoch > 10:
            train_end.record()
            torch.cuda.synchronize()
            computation_time += (train_start.elapsed_time(train_end))

        if epoch > 10:
            step_start = torch.cuda.Event(enable_timing=True)
            step_end = torch.cuda.Event(enable_timing=True)
            step_start.record()

        optimizer.step()

        if epoch > 10:
            step_end.record()
            torch.cuda.synchronize()
            step_time += (step_start.elapsed_time(step_end))


        if batch_idx % args.log_interval == 0:
            # BytePS: use train_sampler to determine the number of examples in
            # this worker's partition.
            print('Train Epoch: {} [{}/{} ({:.0f}%)]\tLoss: {:.6f}'.format(
                epoch, batch_idx * len(data), len(train_sampler),
                100. * batch_idx / len(train_loader), loss.item()))

        load_start = time.time()
        
        batches_time += time.time() - batch_start
    


def metric_average(val, name):
    tensor = torch.tensor(val)
    if args.cuda:
        tensor = tensor.cuda()
    avg_tensor = bps.push_pull(tensor, name=name)
    return avg_tensor.item()


def test():
    model.eval()
    test_loss = 0.
    test_accuracy = 0.
    for data, target in test_loader:
        if args.cuda:
            data, target = data.cuda(), target.cuda()
        output = model(data)
        # sum up batch loss
        test_loss += F.nll_loss(output, target, size_average=False).item()
        # get the index of the max log-probability
        pred = output.data.max(1, keepdim=True)[1]
        test_accuracy += pred.eq(target.data.view_as(pred)).cpu().float().sum()

    # BytePS: use test_sampler to determine the number of examples in
    # this worker's partition.
    test_loss /= len(test_sampler)
    test_accuracy /= len(test_sampler)

    # BytePS: average metric values across workers.
    # test_loss = metric_average(test_loss, 'avg_loss')
    # test_accuracy = metric_average(test_accuracy, 'avg_accuracy')

    # BytePS: print output only on first rank.
    if bps.rank() == 0:
        print('\nTest set: Average loss: {:.4f}, Accuracy: {:.2f}%\n'.format(
            test_loss, 100. * test_accuracy))

#####Minghao
start_time = time.time()
test_time = 0.0
train_time = 0.0
train_start = torch.cuda.Event(enable_timing=True)
train_end = torch.cuda.Event(enable_timing=True)
test_start = torch.cuda.Event(enable_timing=True)
test_end = torch.cuda.Event(enable_timing=True)
#####
for epoch in range(1, args.epochs + 1):
    if epoch > 10:
        train_start = time.time()
    train(epoch)
    if epoch > 10:
        train_time += time.time() - train_start
    if epoch > 10:
        test_start = time.time()
    test()
    if epoch > 10:
        test_time += time.time() - test_start
#####Minghao
if args.inca:
    print("compress and decompress overhead ", optimizer.compress_overhead / 1000, optimizer.decompress_overhead / 1000)
    print("compress and decompress rotation overhead ", optimizer.compress_rotate_overhead / 1000, optimizer.decompress_rotate_overhead / 1000)
    print("compress and decompress diagonal overhead ", optimizer.compress_diag_time / 1000, optimizer.decompress_diag_time / 1000)
    print("compress and decompress hadamard overhead ", optimizer.compress_hadamard_time / 1000, optimizer.decompress_hadamard_time / 1000)
if args.topk or args.dummy:
    print("compress and decompress overhead ", optimizer.compress_overhead / 1000, optimizer.decompress_overhead / 1000)
total_time = time.time() - start_time
print("Total Time: " + str(total_time))
print("Total computation time (exclude first 10 epochs): " + str(computation_time / 1000))
print("Total time for step function: " + str(step_time / 1000))
print("Total time for data_movement: " + str(data_move_time / 1000))
print("Total time for setting model and epochs: " + str(prep_time / 1000))
print("Total data loading time: " + str(load_time))
print("Total Training Batches Time: " + str(batches_time))
print("Total Training Time (exclude first 10 epochs): " + str(train_time))
print("Total Testing Time (exclude first 10 epochs): " + str(test_time))
#####