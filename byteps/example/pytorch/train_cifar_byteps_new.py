#!/usr/bin/env python
from __future__ import print_function
import argparse
import os
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torchvision import datasets, transforms, models
import torch.utils.data.distributed
import byteps.torch as bps
#######Minghao
import time
#######

# Training settings
parser = argparse.ArgumentParser(description='PyTorch MNIST Example')
parser.add_argument('--batch-size', type=int, default=128, metavar='N',
                    help='input batch size for training (default: 64)')
parser.add_argument('--test-batch-size', type=int, default=1000, metavar='N',
                    help='input batch size for testing (default: 1000)')
parser.add_argument('--epochs', type=int, default=100, metavar='N',
                    help='number of epochs to train (default: 100)')
parser.add_argument('--lr', type=float, default=0.1, metavar='LR',
                    help='learning rate (default: 0.01)')
parser.add_argument('--momentum', type=float, default=0.9, metavar='M',
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
parser.add_argument('--inca', action='store_true', default=False,
                    help='use INCA compression during pushpull')
parser.add_argument('--ef', action='store_true', default=False,
                    help='use INCA compression with error feedback')
parser.add_argument('--rotation', action='store_true', default=False,
                    help='use INCA compression with rotation')
parser.add_argument('--quant-level', type=int, default=16, metavar='N',
                    help='INCA quantization levels')
parser.add_argument('--norm-normal', action='store_true', default=False,
                    help='use INCA compression with norm normalization')
parser.add_argument('--overflow-prob', type=float, default=0.0001, metavar='P',
                    help='per_coordinate_overflow_prob')
parser.add_argument('--dummy', action='store_true', default=False,
                    help='use Dummy compression during pushpull')
######## Kevin
parser.add_argument('--dataset', default='CIFAR100', choices=['CIFAR10', 'CIFAR100'],
                    help='dataset')
parser.add_argument('--net', default='ResNet18', choices=['ResNet18', 'ResNet50', 'VGG16', 'VGG19'],
                    help='model architecture')

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
kwargs = {'num_workers': 1, 'pin_memory': True, 'persistent_workers': True} if args.cuda else {}
# kwargs = {'num_workers': 1, 'pin_memory': True} if args.cuda else {}
if args.dataset == 'CIFAR10':
	dataset = datasets.CIFAR10
	num_classes = 10
elif args.dataset == 'CIFAR100':
	dataset = datasets.CIFAR100
	num_classes = 100
train_dataset = \
    dataset('data-%d' % bps.rank(), train=True, download=True,
			transform=transforms.Compose([
				transforms.RandomCrop(32, padding=4),
				transforms.ToTensor(),
				transforms.Normalize((0.4914, 0.4822, 0.4465), (0.2023, 0.1994, 0.2010)),
			]))

# BytePS: use DistributedSampler to partition the training data.
train_sampler = torch.utils.data.distributed.DistributedSampler(
    train_dataset, num_replicas=bps.size(), rank=bps.rank())
train_loader = torch.utils.data.DataLoader(
    train_dataset, batch_size=args.batch_size, sampler=train_sampler, **kwargs)
print("train_loader initialized")

test_dataset = \
    dataset('data-%d' % bps.rank(), train=False, transform=transforms.Compose([
        transforms.RandomCrop(32, padding=4),
		transforms.ToTensor(),
		transforms.Normalize((0.4914, 0.4822, 0.4465), (0.2023, 0.1994, 0.2010)),
    ]))
# BytePS: use DistributedSampler to partition the test data.
test_sampler = torch.utils.data.distributed.DistributedSampler(
    test_dataset, num_replicas=bps.size(), rank=bps.rank())
test_loader = torch.utils.data.DataLoader(test_dataset, batch_size=args.test_batch_size,
                                          sampler=test_sampler, **kwargs)
print("test_loader initialized")

affinity = os.sched_getaffinity(0)  
# Print the result
print("Process is eligible to run on:", affinity)

if args.net == 'ResNet18':
	net = models.resnet18
elif args.net == 'ResNet50':
	net = models.resnet50
elif args.net == 'VGG16':
    net = models.vgg16
elif args.net == 'VGG19':
    net = models.vgg19

model = net(num_classes=num_classes)

if args.cuda:
    # Move model to GPU.
    model.cuda()

# BytePS: scale learning rate by the number of GPUs.
optimizer = optim.SGD(model.parameters(), lr=args.lr,
                      momentum=args.momentum, weight_decay=5e-4)

# Find the total number of trainable parameters of the model
pytorch_total_params = sum(p.numel() for p in model.parameters())
pytorch_total_params_trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
if (pytorch_total_params != pytorch_total_params_trainable):
    raise Exception("pytorch_total_params != pytorch_total_params_trainable")
print("Total number of trainable parameters: " + str(pytorch_total_params_trainable))

# BytePS: (optional) compression algorithm.
if args.inca:
    compression = bps.Compression.inca(params={'nclients': 1, 'd': pytorch_total_params_trainable, \
        'ef': args.ef, 'rotation': args.rotation, 'quantization_levels': args.quant_level, \
        'norm_normalization': args.norm_normal, 'per_coordinate_overflow_prob': args.overflow_prob})
elif args.dummy:
    compression = bps.Compression.dummy(params={'modify_idx': -1})
else:
    compression = bps.Compression.fp16 if args.fp16_pushpull else bps.Compression.none

# BytePS: wrap optimizer with DistributedOptimizer.
optimizer = bps.DistributedOptimizer(optimizer,
                                     named_parameters=model.named_parameters(),
                                     compression=compression)


# BytePS: broadcast parameters.
bps.broadcast_parameters(model.state_dict(), root_rank=0)
bps.broadcast_optimizer_state(optimizer, root_rank=0)


def train(epoch):
    model.train()
    # BytePS: set epoch to sampler for shuffling.
    train_sampler.set_epoch(epoch)
    print("train sampler set epoch")
    for batch_idx, (data, target) in enumerate(train_loader):
        if args.cuda:
            data, target = data.cuda(), target.cuda()
        optimizer.zero_grad()
        output = model(data)
        loss = nn.CrossEntropyLoss(output, target)
        loss.backward()
        optimizer.step()
        if batch_idx % args.log_interval == 0:
            # BytePS: use train_sampler to determine the number of examples in
            # this worker's partition.
            print('Train Epoch: {} [{}/{} ({:.0f}%)]\tLoss: {:.6f}'.format(
                epoch, batch_idx * len(data), len(train_sampler),
                100. * batch_idx / len(train_loader), loss.item()))


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
#####
for epoch in range(1, args.epochs + 1):
    train(epoch)
    test()
#####Minghao
if args.inca or args.dummy:
    print("compress and decompress overhead ", optimizer.compress_overhead / 1000, optimizer.decompress_overhead / 1000)
    print("compress and decompress rotation overhead ", optimizer.compress_rotate_overhead / 1000, optimizer.decompress_rotate_overhead / 1000)
total_time = time.time() - start_time
print("Total Time: " + str(total_time))
#####