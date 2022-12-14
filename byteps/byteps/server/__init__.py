# Copyright 2019 Bytedance Inc. or its affiliates. All Rights Reserved.
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

import ctypes
import os
from byteps.common import get_ext_suffix


def run():
    dll_path = os.path.join(os.path.dirname(__file__),
                            'c_lib' + get_ext_suffix())
    print(dll_path)
    SERVER_LIB_CTYPES = ctypes.CDLL(dll_path, ctypes.RTLD_GLOBAL)
    SERVER_LIB_CTYPES.byteps_server()

run()
