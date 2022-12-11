/*******************************************************************************
 * BAREFOOT NETWORKS CONFIDENTIAL & PROPRIETARY
 *
 * Copyright (c) 2019-present Barefoot Networks, Inc.
 *
 * All Rights Reserved.
 *
 * NOTICE: All information contained herein is, and remains the property of
 * Barefoot Networks, Inc. and its suppliers, if any. The intellectual and
 * technical concepts contained herein are proprietary to Barefoot Networks, Inc.
 * and its suppliers and may be covered by U.S. and Foreign Patents, patents in
 * process, and are protected by trade secret or copyright law.  Dissemination of
 * this information or reproduction of this material is strictly forbidden unless
 * prior written permission is obtained from Barefoot Networks, Inc.
 *
 * No warranty, explicit or implicit is provided, unless granted under a written
 * agreement with Barefoot Networks, Inc.
 *
 ******************************************************************************/

#ifndef _HEADERS_
#define _HEADERS_

typedef bit<48> mac_addr_t;
typedef bit<32> ipv4_addr_t;

typedef bit<16> ether_type_t;
const ether_type_t ETHERTYPE_IPV4 = 16w0x0800; //16-bit unsigned number (w indicates unsigned numbers, s indicates signed numbers)
const ether_type_t ETHERTYPE_PFC = 16w0x8803;//0x8808

typedef bit<8> ip_protocol_t;
const ip_protocol_t IP_PROTOCOLS_IPV4 = 4;
const ip_protocol_t IP_PROTOCOLS_UDP = 17;
const ip_protocol_t IP_PROTOCOLS_TCP = 6;

typedef bit<16> udp_port_t;

typedef bit<24> dst_qp_t;

/**/
typedef bit<8>  pkt_type_t;
const pkt_type_t PKT_TYPE_NORMAL = 1;
const pkt_type_t PKT_TYPE_MIRROR = 2;

#if __TARGET_TOFINO__ == 1
typedef bit<6> mirror_type_t;
#else
typedef bit<4> mirror_type_t;
#endif
//const mirror_type_t MIRROR_TYPE_I2E = 1;
//const mirror_type_t MIRROR_TYPE_E2E = 2;
#define SWITCH_MIRROR_TYPE_PORT 5
#define SWITCH_MIRROR_TYPE_SQ_PAUSE 10
#define SWITCH_MIRROR_TYPE_CUT_PAYLOAD 9

// data are converted ftom 32-bit floating points into chunks of 32-bit values
typedef int<32> data_type_t;
// 64-bit registers can contain two 32-bit values
struct pair_t {
    data_type_t first;
    data_type_t second;
}


header ethernet_h {
    mac_addr_t dst_addr;
    mac_addr_t src_addr;
    bit<16> ether_type;
}

header ipv4_h {
    bit<4> version;
    bit<4> ihl;
    bit<8> diffserv;
    bit<16> total_len;
    bit<16> identification;
    bit<3> flags;
    bit<13> frag_offset;
    bit<8> ttl;
    bit<8> protocol;
    bit<16> hdr_checksum;
    ipv4_addr_t src_addr;
    ipv4_addr_t dst_addr;
}

header udp_h {
    bit<16> src_port;
    bit<16> dst_port;
    bit<16> length;
    bit<16> checksum;
}

header p4ml_h{
    bit<32> bitmap;
    bit<8> num_worker;
    bit<32> roundNum; // round number
    bit<32> max_info; // max
    bit<32> norm_info; // norm
    bit<1> packet_type; // 0: gradient, 1: norm
    bit<1> dataIndex;
    bit<6> pad;
    bit<8> recirc_time;
    bit<16> pad_1;
    bit<48> ingress_global; // ingress global timestamp
    bit<16> pad_2;
    bit<48> egress_global; // egress global timestamp 
}

header p4ml_agtr_index_h{
    bit<16> agtr;
    //bit<1> pad;
}

header entry_h{
data_type_t        data0 ; 
data_type_t        data1 ;
data_type_t        data2 ;
data_type_t        data3 ;
data_type_t        data4 ;
data_type_t        data5 ;
data_type_t        data6 ;
data_type_t        data7 ;
data_type_t        data8 ;
data_type_t        data9 ;
data_type_t        data10;
data_type_t        data11;
data_type_t        data12;
data_type_t        data13;
data_type_t        data14;
data_type_t        data15;
data_type_t        data16;
data_type_t        data17;
data_type_t        data18;
data_type_t        data19;
data_type_t        data20;
data_type_t        data21;
data_type_t        data22;
data_type_t        data23;
data_type_t        data24;
data_type_t        data25;
data_type_t        data26;
data_type_t        data27;
data_type_t        data28;
data_type_t        data29;
data_type_t        data30;
data_type_t        data31;
data_type_t        data32;
data_type_t        data33;
data_type_t        data34;
data_type_t        data35;
data_type_t        data36;
data_type_t        data37;
data_type_t        data38;
data_type_t        data39;
data_type_t        data40;
data_type_t        data41;
data_type_t        data42;
data_type_t        data43;
data_type_t        data44;
data_type_t        data45;
data_type_t        data46;
data_type_t        data47;
data_type_t        data48;
data_type_t        data49;
data_type_t        data50;
data_type_t        data51;
data_type_t        data52;
data_type_t        data53;
data_type_t        data54;
data_type_t        data55;
data_type_t        data56;
data_type_t        data57;
data_type_t        data58;
data_type_t        data59;
data_type_t        data60;
data_type_t        data61;
data_type_t        data62;
data_type_t        data63;
//bit<32>        data31;
}

header p4ml_meta_h{
// P4ML
 	bit<32>         bitmap                  ;
    bit<16>         roundNum; // round number
    bit<32>         max_info; // max or norm
    bit<32>         norm_info;
 	bit<32>         isAggregate             ; // TODO: for debug
 	// bit<8>          agtr_time               ;
 	bit<32>         integrated_bitmap       ;
 	bit<32>         agtr_index              ;
 	bit<8>          seen_bitmap0            ;
 	bit<8>          seen_isAggregate        ;
    bit<2>          match_roundnum          ;
    bit<1>          need_send_out           ; // TODO: for debug
    bit<4>          recirc_time             ; // the number of times the packet is recirculated
    bit<1>          pad                     ; // TODO: for debug
}


header p4ml_bg_h {
	bit<64>        key             ; 
	bit<32>        len_tensor      ; 
	bit<32>        bitmap          ;   
}

#endif /* _HEADERS_ */

