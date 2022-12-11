"""VGG network in MXNet
    Adapted from: https://github.com/danhdoan/cifar10-end2end-mxnet/blob/master/altusi/models/vgg.py
"""

from mxnet.gluon import nn

def _make_layers(arch):
    features = nn.HybridSequential()

    for nlayers, nchannels in zip(*arch):
        for _ in range(nlayers):
            features.add(nn.Conv2D(channels=nchannels,
                                   kernel_size=3,
                                   padding=1))
            features.add(nn.BatchNorm())
            features.add(nn.Activation('relu'))
        features.add(nn.MaxPool2D(pool_size=2, strides=2))

    return features


class VGG(nn.HybridBlock):
    def __init__(self, arch, nclasses=100, **kwargs):
        super(VGG, self).__init__(**kwargs)

        self.features = _make_layers(arch)
        self.features.add(nn.Dense(2048, activation='relu'))
        self.features.add(nn.Dropout(rate=0.5))
        self.features.add(nn.Dense(2048, activation='relu'))
        self.features.add(nn.Dropout(rate=0.5))

        self.output = nn.Dense(nclasses)


    def hybrid_forward(self, F, X):
        y = self.features(X)
        y = self.output(y)

        return y


cfg = {
    11: ([1, 1, 2, 2, 2], [64, 128, 256, 512, 512]),
    13: ([2, 2, 2, 2, 2], [64, 128, 256, 512, 512]),
    16: ([2, 2, 3, 3, 3], [64, 128, 256, 512, 512]),
    19: ([2, 2, 4, 4, 4], [64, 128, 256, 512, 512]),
}


def _make_vgg(nlayers, nclasses=100):
    return VGG(cfg[nlayers], nclasses)


def VGG11(nclasses=100):
    return _make_vgg(11, nclasses)


def VGG13(nclasses=100):
    return _make_vgg(13, nclasses)


def VGG16(nclasses=100):
    return _make_vgg(16, nclasses)


def VGG19(nclasses=100):
    return _make_vgg(19, nclasses)