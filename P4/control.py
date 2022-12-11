import sys
import pd_base_tests

from ptf import config
from ptf import packet
from ptf.testutils import *
from ptf.thriftutils import *
import bfrt_grpc.client as gc
import ptf.testutils as testutils
import time

from pal_rpc.ttypes import *
from conn_mgr_pd_rpc.ttypes import *
from mc_pd_rpc.ttypes import *
from res_pd_rpc.ttypes import *
from devport_mgr_pd_rpc.ttypes import *
from ptf_port import *
from pgrs.p4_pd_rpc.ttypes import *
import importlib
import time

dev_id = 0
def make_port(pipe, local_port):
    assert pipe >= 0 and pipe < 4
    assert local_port >= 0 and local_port < 72
    return pipe << 7 | local_port
def port_to_pipe(port):
    return port >> 7


def portToPipe(port):
    return port >> 7

def portToPipeLocalId(port):
    return port & 0x7F

def portToBitIdx(port):
    pipe = portToPipe(port)
    index = portToPipeLocalId(port)
    return 72 * pipe + index

def set_port_map(indicies):
    bit_map = [0] * ((288+7)//8)
    for i in indicies:
        index = portToBitIdx(i)
        bit_map[index//8] = (bit_map[index//8] | (1 << (index%8))) & 0xFF
    return bytes_to_string(bit_map)

def set_lag_map(indicies):
    bit_map = [0] * ((256+7)//8)
    for i in indicies:
        bit_map[i//8] = (bit_map[i//8] | (1 << (i%8))) & 0xFF
    return bytes_to_string(bit_map)


# Prepare the packet that will be used to read hash table contents
def rd_hash_pkt(
    pktlen=60, eth_dst="00:01:02:03:04:05", eth_src="00:06:07:08:09:0a", eth_type=0x88CC
):
    if MINSIZE > pktlen:
        pktlen = MINSIZE
    pkt = packet.Ether(dst=eth_dst, src=eth_src, type=eth_type)
    pkt = pkt / ("\0" * (pktlen - len(pkt)))
    return pkt


class TestKSet(pd_base_tests.ThriftInterface):
    def __init__(self):
        pd_base_tests.ThriftInterface.__init__(self, [""], ["pgrs"])
        pd_base_tests.ThriftInterface.setUp(self)


    def setup_grpc(self):
        grpc_addr = "localhost:50052"
        self.interface = gc.ClientInterface(grpc_addr, client_id=0,
                    device_id=0)
        self.interface.bind_pipeline_config("p4ml")

    def stop_grpc(self):
        self.interface.tear_down_stream()

    # Read register contents from the hardware
    def read_registers(self, table, target, reg_index):
        resp = table.entry_get(
                target,
                [table.make_key([gc.KeyTuple('$REGISTER_INDEX', reg_index)])],
                {"from_hw": True})
        data, _ = next(resp)
        data_dict = data.to_dict()
        return data_dict

    # Write register contents from the hardware
    def write_registers(self, table, name, target, reg_index, reg_val):
        table.entry_add(
            target,
            [table.make_key([gc.KeyTuple('$REGISTER_INDEX', reg_index)])],
            [table.make_data(
                [gc.DataTuple("{}.f1".format(name), reg_val)])])


    def set_hash_tables(self):
        brft_info = self.interface.bfrt_info_get("kset_caching")
        key_0_32_hash_table0 = brft_info.table_get("key_0_32_hash_table0")
        val_hash_table0 = brft_info.table_get("val_hash_table0")
        # Read the contents from pipe0        
        target = gc.Target(device_id=0, pipe_id=0xffff)

        for reg_index in range(2048):
            self.write_registers(key_0_32_hash_table0, "key_0_32_hash_table0", target, reg_index, reg_index+1)
            self.write_registers(val_hash_table0, "val_hash_table0", target, reg_index, reg_index+100)

    def set_collision_buffer(self):
        brft_info = self.interface.bfrt_info_get("kset_caching")
        key_0_32_ring = brft_info.table_get("key_0_32_ring")
        wr_ptr_ring0 = brft_info.table_get("wr_ptr_ring0")
        # Read the contents from pipe0        
        target = gc.Target(device_id=0, pipe_id=0xffff)

        # There are total 10 elements in the collision buffer
        self.write_registers(wr_ptr_ring0, "wr_ptr_ring0", target, 0, 10)
        self.write_registers(wr_ptr_ring0, "wr_ptr_ring0", target, 1, 10)

        for reg_index in range(10):
            self.write_registers(key_0_32_ring, "key_0_32_ring", target, reg_index, reg_index+1)
            self.write_registers(key_0_32_ring, "key_0_32_ring", target, 512 + reg_index, reg_index+1)


    def get_collision_buffers(self):
        brft_info = self.interface.bfrt_info_get("kset_caching")
        key_0_32_ring = brft_info.table_get("key_0_32_ring")
        val_ring = brft_info.table_get("val_ring")
        wr_ptr_ring0 = brft_info.table_get("wr_ptr_ring0")
        # Read the contents from pipe0        
        target = gc.Target(device_id=0, pipe_id=0xffff)
        print(self.read_registers(key_0_32_ring, target, 0))
        print(self.read_registers(val_ring, target, 0))
        print(self.read_registers(key_0_32_ring, target, 512))
        print(self.read_registers(val_ring, target, 0))
        print(self.read_registers(wr_ptr_ring0, target, 0))
        print(self.read_registers(wr_ptr_ring0, target, 1))

    def get_hash_tables(self):
        brft_info = self.interface.bfrt_info_get("kset_caching")
        tbl1 = brft_info.table_get("pipe0.UpstreamIngress.val_hash_table")
        tbl2 = brft_info.table_get("pipe0.UpstreamIngress.key_64_80_hash_table")
        tbl5 = brft_info.table_get("pipe0.UpstreamIngress.key_80_96_hash_table")
        tbl3 = brft_info.table_get("pipe0.UpstreamIngress.key_0_32_hash_table")
        tbl4 = brft_info.table_get("pipe0.UpstreamIngress.key_32_64_hash_table")
        # Read the contents from pipe0        
        target = gc.Target(device_id=0, pipe_id=0xffff)
        print("--------------For upstream---------------------")
        for reg_index in range(2048):
            src_ip = self.read_registers(tbl3, target, reg_index)["UpstreamIngress.key_0_32_hash_table.f1"][0]
            dst_ip = self.read_registers(tbl4, target, reg_index)["UpstreamIngress.key_32_64_hash_table.f1"][0]
            value = self.read_registers(tbl1, target, reg_index)["UpstreamIngress.val_hash_table.f1"][0]
            src_port = self.read_registers(tbl2, target, reg_index)["UpstreamIngress.key_64_80_hash_table.f1"][0]
            dst_port = self.read_registers(tbl5, target, reg_index)["UpstreamIngress.key_80_96_hash_table.f1"][0]
            if value > 0:
                print(reg_index, value, src_port, dst_port ,hex(src_ip), hex(dst_ip))

        print("--------------For downstream---------------------")
        tbl1 = brft_info.table_get("pipe1.DownstreamIngress.val_hash_table")
        tbl2 = brft_info.table_get("pipe1.DownstreamIngress.key_64_80_hash_table")
        tbl5 = brft_info.table_get("pipe1.DownstreamIngress.key_80_96_hash_table")
        tbl3 = brft_info.table_get("pipe1.DownstreamIngress.key_0_32_hash_table")
        tbl4 = brft_info.table_get("pipe1.DownstreamIngress.key_32_64_hash_table")
        for reg_index in range(2048):
            src_ip = self.read_registers(tbl3, target, reg_index)["DownstreamIngress.key_0_32_hash_table.f1"][0]
            dst_ip = self.read_registers(tbl4, target, reg_index)["DownstreamIngress.key_32_64_hash_table.f1"][0]
            value = self.read_registers(tbl1, target, reg_index)["DownstreamIngress.val_hash_table.f1"][0]
            src_port = self.read_registers(tbl2, target, reg_index)["DownstreamIngress.key_64_80_hash_table.f1"][0]
            dst_port = self.read_registers(tbl5, target, reg_index)["DownstreamIngress.key_80_96_hash_table.f1"][0]
            if value > 0:
                print(reg_index, value, src_port, dst_port ,hex(src_ip), hex(dst_ip))


    def get_stack(self):
        brft_info = self.interface.bfrt_info_get("kset_caching")
        tbl1 = brft_info.table_get("pipe0.UpstreamIngress.wr_ptr_stack")
        tbl2 = brft_info.table_get("pipe0.UpstreamIngress.key_96_128_stack")
        target = gc.Target(device_id=0, pipe_id=0xffff)
        print(self.read_registers(tbl1, target, 0))
        print(self.read_registers(tbl2, target, 0))

    def get_rerouting_table(self):
        brft_info = self.interface.bfrt_info_get("kset_caching")
        tbl1 = brft_info.table_get("pipe0.UpstreamIngress.key_0_32_rerouting_table")
        tbl2 = brft_info.table_get("pipe0.UpstreamIngress.key_32_64_rerouting_table")
        tbl3 = brft_info.table_get("pipe0.UpstreamIngress.key_64_96_rerouting_table")
        tbl4 = brft_info.table_get("pipe0.UpstreamIngress.key_96_112_rerouting_table")

        # Read the contents from pipe0        
        target = gc.Target(device_id=0, pipe_id=0xffff)
        for reg_index in range(8):
            print(reg_index, self.read_registers(tbl1, target, reg_index))
            print(reg_index, self.read_registers(tbl2, target, reg_index))
            print(reg_index, self.read_registers(tbl3, target, reg_index))
            print(reg_index, self.read_registers(tbl4, target, reg_index))

    def enable_pkt_gen(self):
        print("------------------------------------")
        print("       Enable packet generator")
        print("------------------------------------")
        FPGA_MAC_ADDRESS   = "11:22:33:44:55:66"
        SWITCH_MAC_ADDRESS = "aa:bb:cc:dd:ee:ff"

        sess_hdl = self.conn_mgr.client_init()
        # Our TOFINO only has two pipes
        num_pipes = 2
        pipe_list = [x for x in range(num_pipes)]
        pipe_list_len = len(pipe_list)
        dev_tgt_ = []
        for pipe in range(pipe_list_len):
            dev_tgt_.append(DevTarget_t(dev_id, hex_to_i16(pipe)))

        # Add CRC by Tofino then will be 68 bytes
        pktlen = 128

        # Enable all pktgen ports
        for pipe in pipe_list:
            self.conn_mgr.pktgen_enable(sess_hdl, dev_id,  make_port(pipe,68))


        # Every (1 << 14) = 16384 ns, there will be a timer packet
        app_cfg1 = PktGenAppCfg_t(trigger_type=PktGenTriggerType_t.TIMER_PERIODIC,
                                    batch_count=0,
                                    pkt_count=0,
                                    timer= 1 << 14,
                                    ibg=0,
                                    ipg=0,
                                    src_port=68,
                                    src_port_inc=0,
                                    buffer_offset=144,
                                    length=pktlen)
        app_id1 = 1
        
        # When the port 68 detects special pattern, it will send packets that 
        # are aimed to read the hash table contents 
        # The inter packet gap is  (1 << 12) ns = 4096ns
        read_hash_pkt_cnt = 1024 // 64
        app_cfg2 = PktGenAppCfg_t(trigger_type=PktGenTriggerType_t.RECIRC_PATTERN,
                                  batch_count=0,
                                  pkt_count=read_hash_pkt_cnt-1,
                                  pattern_key=hex_to_i32(0x05000000),
                                  pattern_msk=hex_to_i32(0xF7FFFFFF),
                                  timer=hex_to_i32(0),
                                  ibg=0,
                                  ipg=1 << 10,
                                  src_port=68,
                                  src_port_inc=0,
                                  buffer_offset=544,
                                  length=pktlen)

        app_id2 = 2

        
        # Configure two different applications on pipe0
        self.conn_mgr.pktgen_cfg_app(sess_hdl, dev_tgt_[0], app_id1, app_cfg1)
        self.conn_mgr.pktgen_cfg_app(sess_hdl, dev_tgt_[0], app_id2, app_cfg2)

        # Configure two different applications on pipe1
        self.conn_mgr.pktgen_cfg_app(sess_hdl, dev_tgt_[1], app_id1, app_cfg1)
        self.conn_mgr.pktgen_cfg_app(sess_hdl, dev_tgt_[1], app_id2, app_cfg2)

        # Write template packet into packet buffer
        p = rd_hash_pkt(pktlen=pktlen, eth_dst=FPGA_MAC_ADDRESS, eth_src=SWITCH_MAC_ADDRESS, eth_type=0x1122)
        bytes_p_hash = bytes(p)
        p = rd_hash_pkt(pktlen=pktlen, eth_dst=FPGA_MAC_ADDRESS, eth_src=SWITCH_MAC_ADDRESS, eth_type=0x2233)
        bytes_p_collison = bytes(p)
        ########## Pipe 0 ##################
        self.conn_mgr.pktgen_write_pkt_buffer(sess_hdl, dev_tgt_[0], 144, pktlen, bytes_p_collison)
        self.conn_mgr.pktgen_write_pkt_buffer(sess_hdl, dev_tgt_[0], 544, pktlen, bytes_p_hash)
        ########## Pipe 1 ##################
        self.conn_mgr.pktgen_write_pkt_buffer(sess_hdl, dev_tgt_[1], 144, pktlen, bytes_p_collison)
        self.conn_mgr.pktgen_write_pkt_buffer(sess_hdl, dev_tgt_[1], 544, pktlen, bytes_p_hash)

        # Enable pattern matching on pipe0 and pipe1
        self.conn_mgr.pktgen_enable_recirc_pattern_matching(sess_hdl, dev_id, 68) 
        self.conn_mgr.pktgen_enable_recirc_pattern_matching(sess_hdl, dev_id, 196)

        # Enable two different applications on pipe0 & pipe 1
        self.conn_mgr.pktgen_app_enable(sess_hdl, dev_tgt_[0], app_id1)
        self.conn_mgr.pktgen_app_enable(sess_hdl, dev_tgt_[1], app_id1)

        self.conn_mgr.pktgen_app_enable(sess_hdl, dev_tgt_[0], app_id2)
        self.conn_mgr.pktgen_app_enable(sess_hdl, dev_tgt_[1], app_id2)

        # Complete these operations 
        self.conn_mgr.complete_operations(sess_hdl)
        sess_hdl = self.conn_mgr.client_cleanup(sess_hdl)

    def disable_pkt_gen(self):
        print("------------------------------------")
        print("       Disable packet generator")
        print("------------------------------------")
        app_id1 = 1
        app_id2 = 2

        sess_hdl = self.conn_mgr.client_init()
        # Our TOFINO only has two pipes
        num_pipes = 2
        pipe_list = [x for x in range(num_pipes)]
        pipe_list_len = len(pipe_list)
        dev_tgt_ = []
        for pipe in range(pipe_list_len):
            dev_tgt_.append(DevTarget_t(dev_id, hex_to_i16(pipe)))

        sess_hdl = self.conn_mgr.client_init()
        # Disable two different applications on pipe0
        self.conn_mgr.pktgen_app_disable(sess_hdl, dev_tgt_[0], app_id1)
        self.conn_mgr.pktgen_app_disable(sess_hdl, dev_tgt_[0], app_id2)
        # Disable two different applications on pipe1
        self.conn_mgr.pktgen_app_disable(sess_hdl, dev_tgt_[1], app_id1)
        self.conn_mgr.pktgen_app_disable(sess_hdl, dev_tgt_[1], app_id2)

        # Disable all pktgen ports
        for pipe in pipe_list:
            self.conn_mgr.pktgen_disable(sess_hdl, dev_id,  make_port(pipe,68))

        sess_hdl = self.conn_mgr.client_cleanup(sess_hdl)

    # Enable ports on the Tofino hardware switch 
    def enable_ports(self):
        # 60 is PORT 9/0  (TIMI: Bluefield ens2f0)
        # 44 is PORT 11/0 (TIMI: Bluefield ens2f1)
        # 28 is PORT 13/0 (RUMI: FPGA enp130s0f0)
        # 20 is PORT 14/0 (RUMI: FPGA enp130s0f1)


        # 0   is PORT 17/0 (Connect to 31/0)
        # 8   is PORT 18/0 (Connect to 32/0)
        # 16  is PORT 19/0 (Connect to 29/0)
        # 24  is PORT 20/0 (Connect to 30/0)
        # 156 is PORT 30/0 
        # 148 is PORT 29/0
        # 132 is PORT 31/0 
        # 140 is PORT 32/0 
        # dev_port_list = [60, 44, 28, 20, 0, 8, 132, 140, 16, 148, 24, 156]
        inner_connect_port = [0, 8, 16, 24, 148, 156, 132, 140]
        dev_port_list = [20, 28] + inner_connect_port
        for port in dev_port_list:
            self.pal.pal_port_add(0, port, 
                                    pal_port_speed_t.BF_SPEED_100G,
                                    pal_fec_type_t.BF_FEC_TYP_NONE) 
            self.pal.pal_port_an_set(0, port, pal_autoneg_policy_t.BF_AN_FORCE_DISABLE)
            self.pal.pal_port_enable(0, port)
        
        # Following port is 40G port
        # 52 is PORT 10/0 (Zu: enp5s0)
        #  4 is PORT 16/0 (Pi: enp4s0)
        # 32 is PORT 21/0 (DORI: enp129s0d1)
        # 40 is PORT 22/0 (DORI: enp129s0d1)
        # 48 is PORT 23/0 (DORI: enp129s0d1)
        # 56 is PORT 24/0 (DORI: enp129s0d1)
        # dev_port_list = [52, 4, 32, 40, 48, 56]
        dori_port = [48]
        dev_port_list = [52, 128] + dori_port
        for port in dev_port_list:
            self.pal.pal_port_add(0, port, 
                                    pal_port_speed_t.BF_SPEED_40G,
                                    pal_fec_type_t.BF_FEC_TYP_NONE) 
            self.pal.pal_port_an_set(0, port, pal_autoneg_policy_t.BF_AN_FORCE_DISABLE)
            self.pal.pal_port_enable(0, port)

    
    def inject_loss(self):
        bfrt_info = self.interface.bfrt_info_get("kset_caching")
        inject_loss_tbl = bfrt_info.table_get("pipe0.UpstreamEgress.inject_loss")
        # Create a key
        key = inject_loss_tbl.make_key([gc.KeyTuple('hdr.info_hdr.path_id', 1),
                                        gc.KeyTuple('hdr.ipv4.src_addr', 0xc0a8010a),
                                        gc.KeyTuple('hdr.ipv4.dst_addr', 0xc0a8010b),
                                        gc.KeyTuple('meta.src_port', 1234),
                                        gc.KeyTuple('meta.dst_port', 4321),
                                        gc.KeyTuple('hdr.ipv4.protocol', 0x6)])
        # Create a value 
        data = inject_loss_tbl.make_data([], 'UpstreamEgress.drop_packet')

        target = gc.Target(device_id=0, pipe_id=0xffff)
        inject_loss_tbl.entry_add(target, [key], [data])
        return inject_loss_tbl, key, target

    def remove_inject_loss(self, inject_loss_tbl, key, target):
        inject_loss_tbl.entry_del(target, [key])

if __name__ == "__main__":
    config["log_dir"] = "log"
    test = TestKSet()
    test.setup_grpc()

    # Enable ports connected (100G / NONE)
    test.enable_ports()

    # test.get_hash_tables()
    # test.get_stack()

    # # Enable the packet generator only when no extra arguments 
    # if len(sys.argv) == 2 and sys.argv[1] == "full":
    #     test.enable_pkt_gen()

    #     input("Press return to inject_loss: ")
    #     test.inject_loss()

    #     input("Press return to disable pkt_gen: ")

    #     test.disable_pkt_gen()
    # # Simulate baseline (no feedback loop)
    # elif len(sys.argv) == 2 and sys.argv[1] == "baseline":
    #     input("Press return to inject_loss: ")
    #     test.inject_loss()
    # # Simulate lossRadar (10ms feedback loop)
    # elif len(sys.argv) == 2 and sys.argv[1] == "lossradar":
    #     input("Press return to inject_loss: ")
    #     inject_loss_tbl, key, target = test.inject_loss()
    #     feedback_time = 10 / 1000
    #     # Sleep 10ms
    #     time.sleep(feedback_time)
    #     test.remove_inject_loss(inject_loss_tbl, key, target)

    time.sleep(60)

    test.stop_grpc()
