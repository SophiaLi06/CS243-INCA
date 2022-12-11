# Based on p4example l2.py and Vic's control.py
import sys

import pd_base_tests
from ptf.testutils import *
from ptf.thriftutils import *
import pal_rpc
import bfrt_grpc.client as gc

from pal_rpc.ttypes import *
from res_pd_rpc.ttypes import *
from ptf_port import *

import time

# Enable ports on the Tofino hardware switch 
class SlimAtpTest(pd_base_tests.ThriftInterface):
    def __init__(self):
        pd_base_tests.ThriftInterface.__init__(self, [""], ["pgrs"])
        pd_base_tests.ThriftInterface.setUp(self)

    def setup_grpc(self, p4program="p4ml_slim"):
        grpc_addr = "localhost:50052"
        self.interface = gc.ClientInterface(grpc_addr, client_id=0,
                    device_id=0)
        self.interface.bind_pipeline_config(p4program)
        # Get bfrt_info and set it as part of the test
        self.bfrt_info = self.interface.bfrt_info_get(p4program)

    def stop_grpc(self):
        self.interface.tear_down_stream()

    def enable_ports(self):
        # Tofino 2
        # 168 is PORT 5/0 (LAMBDA: ConnectX5 100G ens8f0)
        # 184 is PORT 7/0  (TIMI: ConnectX5 100G ens2f1)
        # 192 is PORT 8/0 (RUMI: ConnectX5 100G enp132s0f1)

        # Tofino 1
        # 160 is PORT 5/0 (LAMBDA: ConnectX5 100G ens8f0)
        # 176 is PORT 7/0  (TIMI: ConnectX5 100G ens2f1)
        # 184 is PORT 8/0 (RUMI: ConnectX5 100G enp132s0f1)

        dev_port_list = [168, 184, 192]
        # dev_port_list = [160, 176, 184]
        for port in dev_port_list:
            self.pal.pal_port_add(0, port, pal_port_speed_t.BF_SPEED_100G, pal_fec_type_t.BF_FEC_TYP_NONE)
            # self.pal.pal_port_add(0, port, pal_port_speed_t.BF_SPEED_40G, pal_fec_type_t.BF_FEC_TYP_NONE) 
            # self.pal.pal_port_add(0, port, pal_port_speed_t.BF_SPEED_10G, pal_fec_type_t.BF_FEC_TYP_NONE) 
            self.pal.pal_port_an_set(0, port, pal_autoneg_policy_t.BF_AN_FORCE_DISABLE)
            self.pal.pal_port_enable(0, port)

    def loopback_ports(self):
        port_table = self.bfrt_info.table_get("$PORT")
        target = gc.Target(device_id=0, pipe_id=0xffff)
        recirc_port_list = [168] #(6/0) #Tofino2 (5/0)
        # recirc_port_list = [24] #(20/0)
        # recirc_port_list = [8, 16, 24] #(18/0), (19/0), (20/0)
        for port in recirc_port_list:
            port_table.entry_add(
                target,
                [port_table.make_key([gc.KeyTuple('$DEV_PORT', port)])],
                [port_table.make_data([
                    gc.DataTuple('$SPEED', str_val='BF_SPEED_100G'),
                    gc.DataTuple('$FEC', str_val='BF_FEC_TYP_NONE'),
                    gc.DataTuple('$LOOPBACK_MODE', str_val='BF_LPBK_MAC_NEAR'),
                    gc.DataTuple('$PORT_ENABLE', bool_val=True)
                    ])
                ]
            )
        
    def set_multicast(self, p4program="p4ml_slim"):
        node_table = self.bfrt_info.table_get("$pre.node")
        target = gc.Target(device_id=0, pipe_id=0xffff)
        node_table.entry_add(
            target,
            [node_table.make_key([gc.KeyTuple('$MULTICAST_NODE_ID', 1)])],
            [node_table.make_data([
                gc.DataTuple('$MULTICAST_RID', 0),
                gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
                gc.DataTuple('$DEV_PORT', int_arr_val=[168, 184])
                # gc.DataTuple('$DEV_PORT', int_arr_val=[160, 176])
                ])
            ]
        )

        mgid_table = self.bfrt_info.table_get("$pre.mgid")
        mgid_table.entry_add(
            target,
            [mgid_table.make_key([gc.KeyTuple('$MGID', 1)])],
            [mgid_table.make_data([
                gc.DataTuple('$MULTICAST_NODE_ID', int_arr_val=[1]),
                gc.DataTuple('$MULTICAST_NODE_L1_XID_VALID', bool_arr_val=[0]),
                gc.DataTuple('$MULTICAST_NODE_L1_XID', int_arr_val=[0])])]
        )

    def set_dmac_forward(self, p4program="p4ml_slim"):
        # for safety reason, we remove all damc entries here
        pass

    def sec_recirc_ports(self, p4program="p4ml_slim"):
        target = gc.Target(device_id=0, pipe_id=0xffff)
        recirc_ports_table = self.bfrt_info.table_get("SwitchIngress.appid_seq.recirc_port")
        recirc_ports_table.entry_add(
            target,
            [recirc_ports_table.make_key([gc.KeyTuple('p4ml.recirc_time', 1)])],
            [recirc_ports_table.make_data([gc.DataTuple('recirc_port', 128)], "SwitchIngress.appid_seq.set_recirc_port")]
        )
        recirc_ports_table.entry_add(
            target,
            [recirc_ports_table.make_key([gc.KeyTuple('p4ml.recirc_time', 2)])],
            [recirc_ports_table.make_data([gc.DataTuple('recirc_port', 128)], "SwitchIngress.appid_seq.set_recirc_port")]
        )
        recirc_ports_table.entry_add(
            target,
            [recirc_ports_table.make_key([gc.KeyTuple('p4ml.recirc_time', 3)])],
            [recirc_ports_table.make_data([gc.DataTuple('recirc_port', 128)], "SwitchIngress.appid_seq.set_recirc_port")]
        )
        recirc_ports_table.entry_add(
            target,
            [recirc_ports_table.make_key([gc.KeyTuple('p4ml.recirc_time', 4)])],
            [recirc_ports_table.make_data([gc.DataTuple('recirc_port', 128)], "SwitchIngress.appid_seq.set_recirc_port")]
        )
        recirc_ports_table.entry_add(
            target,
            [recirc_ports_table.make_key([gc.KeyTuple('p4ml.recirc_time', 5)])],
            [recirc_ports_table.make_data([gc.DataTuple('recirc_port', 128)], "SwitchIngress.appid_seq.set_recirc_port")]
        )
        recirc_ports_table.entry_add(
            target,
            [recirc_ports_table.make_key([gc.KeyTuple('p4ml.recirc_time', 6)])],
            [recirc_ports_table.make_data([gc.DataTuple('recirc_port', 128)], "SwitchIngress.appid_seq.set_recirc_port")]
        )
        recirc_ports_table.entry_add(
            target,
            [recirc_ports_table.make_key([gc.KeyTuple('p4ml.recirc_time', 7)])],
            [recirc_ports_table.make_data([gc.DataTuple('recirc_port', 128)], "SwitchIngress.appid_seq.set_recirc_port")]
        )


if __name__ == "__main__":

    config["log_dir"] = "log"
    # parse p4program name
    n = len(sys.argv)
    if(n > 1):
        p4program = str(sys.argv[1])
    test = SlimAtpTest()
    test.setup_grpc(p4program)

    # Enable ports connected (100G / NONE)
    test.enable_ports()
    # test.loopback_ports()

    # setup multicast
    test.set_multicast(p4program)

    # setup dmac forward tables
    test.set_dmac_forward(p4program)

    # setup recric_port table
    test.sec_recirc_ports(p4program)

    test.stop_grpc()
