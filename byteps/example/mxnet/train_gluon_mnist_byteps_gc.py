#!/usr/bin/env python

# Copyright 2019 Bytedance Inc. or its affiliates. All Rights Reserved.
# Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
# This file is modified from horovod/examples/mxnet_mnist.py, using gluon
# style MNIST dataset and data_loader.

import argparse
import logging
import subprocess
import time

import mxnet as mx
from mxnet import autograd, gluon, nd
from mxnet.gluon.data.vision import MNIST

import byteps.mxnet as bps

# Higher download speed for chinese users
# os.environ['MXNET_GLUON_REPO'] =
# 'https://apache-mxnet.s3.cn-north-1.amazonaws.com.cn/'

# Training settings
parser = argparse.ArgumentParser(description='MXNet MNIST Example')

parser.add_argument('--batch-size', type=int, default=64,
                    help='training batch size (default: 64)')
parser.add_argument('--dtype', type=str, default='float32',
                    help='training data type (default: float32)')
parser.add_argument('--epochs', type=int, default=5,
                    help='number of training epochs (default: 5)')
parser.add_argument('--j', type=int, default=2,
                    help='number of cpu processes for dataloader')
parser.add_argument('--lr', type=float, default=0.01,
                    help='learning rate (default: 0.01)')
parser.add_argument('--wd', type=float, default=0.0001,
                    help='weight decay rate. default is 0.0001.')
parser.add_argument('--momentum', type=float, default=0.9,
                    help='SGD momentum (default: 0.9)')
parser.add_argument('--no-cuda', action='store_true', default=False,
                    help='disable training on GPU (default: False)')
parser.add_argument('--compressor', type=str, default='',
                    help='which compressor')
parser.add_argument('--ef', type=str, default='',
                    help='which error feedback')
parser.add_argument('--compress-momentum', type=str, default='',
                    help='which compress momentum')
parser.add_argument('--scaling', action='store_true', default=False,
                    help='enable scaling for onebit compressor')
parser.add_argument('--k', type=int, default=1,
                    help='topk or randomk')
#####Minghao
parser.add_argument('--seed', type=int, default=None,
                    help='DRIVE or randomk')
#####
parser.add_argument('--fp16-pushpull', action='store_true', default=False,
                    help='use fp16 compression during pushpull')
parser.add_argument('--logging-file', type=str, default='baseline',
                    help='name of training log file')
args = parser.parse_args()


if not args.no_cuda:
    # Disable CUDA if there are no GPUs.
    if mx.context.num_gpus() == 0:
        args.no_cuda = True

# Initialize BytePS
bps.init()

gpu_name = subprocess.check_output(
    ['nvidia-smi', '--query-gpu=gpu_name', '--format=csv'])
gpu_name = gpu_name.decode('utf8').split('\n')[-2]
gpu_name = '-'.join(gpu_name.split())
filename = "mnist-%d-%s-%s.log" % (bps.size(), gpu_name, args.logging_file)
filehandler = logging.FileHandler(filename)
streamhandler = logging.StreamHandler()

logger = logging.getLogger('')
logger.setLevel(level=logging.INFO)
logger.addHandler(filehandler)
logger.addHandler(streamhandler)
logger.info(args)


def dummy_transform(data, label):
    im = data.astype(args.dtype, copy=False) / 255 - 0.5
    im = nd.transpose(im, (2, 0, 1))
    return im, label


# Function to get mnist iterator
def get_mnist_iterator():
    train_set = MNIST(train=True, transform=dummy_transform)
    train_iter = gluon.data.DataLoader(
        train_set, args.batch_size, True, num_workers=args.j,
        last_batch='discard')
    val_set = MNIST(train=False, transform=dummy_transform)
    val_iter = gluon.data.DataLoader(
        val_set, args.batch_size, False, num_workers=args.j)

    return train_iter, val_iter, len(train_set)


# Function to define neural network
def conv_nets():
    net = gluon.nn.HybridSequential()
    with net.name_scope():
        net.add(gluon.nn.Conv2D(channels=20, kernel_size=5, activation='relu'))
        net.add(gluon.nn.MaxPool2D(pool_size=2, strides=2))
        net.add(gluon.nn.Conv2D(channels=50, kernel_size=5, activation='relu'))
        net.add(gluon.nn.MaxPool2D(pool_size=2, strides=2))
        net.add(gluon.nn.Flatten())
        net.add(gluon.nn.Dense(512, activation="relu"))
        net.add(gluon.nn.Dense(10))
    return net


# Function to evaluate accuracy for a model
def evaluate(model, data_iter, context):
    metric = mx.metric.Accuracy()
    for _, batch in enumerate(data_iter):
        data = batch[0].as_in_context(context)
        label = batch[1].as_in_context(context)
        output = model(data)
        metric.update([label], [output])

    return metric.get()


# Load training and validation data
train_data, val_data, train_size = get_mnist_iterator()

# BytePS: pin context to local rank
context = mx.cpu(bps.local_rank()) if args.no_cuda else mx.gpu(
    bps.local_rank())
num_workers = bps.size()

# Build model
model = conv_nets()
model.cast(args.dtype)

# Initialize parameters
model.initialize(mx.init.MSRAPrelu(), ctx=context)
# if bps.rank() == 0:
model.summary(nd.ones((1, 1, 28, 28), ctx=mx.gpu(bps.local_rank())))
model.hybridize()

############### Minghao
#print("init_params")
#params_init_filename = "mnist-{compressor}-{k}-init-{rank}.params".format(compressor=args.compressor, k=args.k, rank=bps.rank())
#model.save_parameters(params_init_filename)
##############
params = model.collect_params()

# BytePS: create DistributedTrainer, a subclass of gluon.Trainer
optimizer_params = {'momentum': args.momentum, 'wd': args.wd,
                    'learning_rate': args.lr * num_workers}
#optimizer_params = {'wd': args.wd, 'learning_rate': args.lr * num_workers}

compression_params = {
    "compressor": args.compressor,
    "ef": args.ef,
    "momentum": args.compress_momentum,
    "scaling": args.scaling,
    "k": args.k,
    "fp16": args.fp16_pushpull,
    #####Minghao
    "seed": args.seed
}

trainer = bps.DistributedTrainer(
    params, "sgd", optimizer_params, compression_params=compression_params)

# Create loss function and train metric
loss_fn = gluon.loss.SoftmaxCrossEntropyLoss()
metric = mx.metric.Accuracy()

total_time = 0
# Minghao
forward_total = 0
backward_total = 0
forw_back_total = 0
step_total = 0
update_total = 0
##########
# Train model
bps.byteps_declare_tensor("acc")
for epoch in range(args.epochs):
    tic = time.time()
    metric.reset()
    for i, batch in enumerate(train_data):
        data = batch[0].as_in_context(context)
        label = batch[1].as_in_context(context)

        with autograd.record():
            # Minghao
            ford_tic = time.time()
            output = model(data)
            loss = loss_fn(output, label)
            # Minghao
            # mx.nd.waitall()
            # forward_total += (time.time() - ford_tic)
        # Minghao
        # back_tic = time.time()
        loss.backward()
        # Minghao
        mx.nd.waitall()
        # backward_total += (time.time() - back_tic)
        forw_back_total += (time.time() - ford_tic)
        #Minghao
        step_tic = time.time()
        trainer.step(args.batch_size)
        # Minghao
        # mx.nd.waitall()
        # step_total += (time.time() - step_tic)
        #Minghao
        update_tic = time.time()
        metric.update([label], [output])
        # Minghao
        # mx.nd.waitall()
        # update_total += (time.time() - update_tic)

        if i % 100 == 0:
            name, acc = metric.get()
            logger.info('[Epoch %d Batch %d] Training: %s=%f' %
                        (epoch, i, name, acc))

        #if bps.rank() == 0 and epoch == 0 and i == 0:
            ############### Minghao
            #print("params_0_0")
            #params_0_0_filename = "mnist-{compressor}-{k}-e0i0-{rank}.params".format(compressor=args.compressor, k=args.k, rank=bps.rank())
            #model.save_parameters(params_0_0_filename)
            ##############

        #if bps.rank() == 0 and epoch == 0 and i == 1:
            ############### Minghao
            #print("params_0_1")
            #params_0_1_filename = "mnist-{compressor}-{k}-e0i1-{rank}.params".format(compressor=args.compressor, k=args.k, rank=bps.rank())
            #model.save_parameters(params_0_1_filename)
            ##############

        #if bps.rank() == 0 and epoch == (args.epochs - 1) and i == (len(train_data) - 3):
            ############### Minghao
            #print("params_0_1")
            #params_f_f2_filename = "mnist-{compressor}-{k}-efif2-{rank}.params".format(compressor=args.compressor, k=args.k, rank=bps.rank())
            #model.save_parameters(params_f_f2_filename)
            ##############

        #if bps.rank() == 0 and epoch == (args.epochs - 1) and i == (len(train_data) - 2):
            ############### Minghao
            #print("params_0_1")
            #params_f_f1_filename = "mnist-{compressor}-{k}-efif1-{rank}.params".format(compressor=args.compressor, k=args.k, rank=bps.rank())
            #model.save_parameters(params_f_f1_filename)
            ##############

    elapsed = time.time() - tic
    total_time += elapsed
    speed = train_size * num_workers / elapsed
    logger.info('Epoch[%d]\tSpeed=%.2f samples/s\tTime cost=%f',
                epoch, speed, elapsed)

    # Evaluate model accuracy
    _, train_acc = metric.get()
    name, val_acc = evaluate(model, val_data, context)
    acc = mx.nd.array([train_acc, val_acc], ctx=context)
    ###########################
    # Minghao:
    if bps.rank() == 0:
        logger.info('Before push-pull Epoch[%d]\tTrain: %s=%f\tValidation: %s=%f', epoch, name,
                    train_acc, name, val_acc)
    ###########################
    bps.byteps_push_pull(acc, name="acc", is_average=False)
    #Minghao acc /= bps.size()
    train_acc, val_acc = acc[0].asscalar(), acc[1].asscalar()
    ###########################
    # Minghao: only print train and validation if rank is 0 (i.e., root)
    #the process from which parameters will be broadcasted to all other processes.
    if bps.rank() == 0:
        logger.info('Epoch[%d]\tTrain: %s=%f\tValidation: %s=%f', epoch, name,
                    train_acc, name, val_acc)
        #if epoch == 0:
            ############### Minghao
            #print("params_e0")
            #params_e0_filename = "mnist-{compressor}-{k}-e0-0.params".format(compressor=args.compressor, k=args.k)
            #model.save_parameters(params_e0_filename)
            ##############


if bps.rank() == 0 and epoch == args.epochs - 1:
    assert val_acc > 0.96, "Achieved accuracy (%f) is lower than expected\
                            (0.96)" % val_acc

############### Minghao
#print("end_params_save")
#params_filename = "mnist-{compressor}-{k}-{rank}.params".format(compressor=args.compressor, k=args.k, rank = bps.rank())
#model.save_parameters(params_filename)
##############

logger.info("total time=%.2f", total_time)
logger.info("total forward_backward=%.5f", forw_back_total)
# logger.info("total forward=%.5f", forward_total)
# logger.info("total backward=%.5f", backward_total)
# logger.info("total step=%.5f", step_total)
# logger.info("total update=%.5f", update_total)
