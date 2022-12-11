/*******************************************************************************
 * BAREFOOT NETWORKS CONFIDENTIAL & PROPRIETARY
 *
 * Copyright (c) 2018-2019 Barefoot Networks, Inc.
 * All Rights Reserved.
 *
 * NOTICE: All information contained herein is, and remains the property of
 * Barefoot Networks, Inc. and its suppliers, if any. The intellectual and
 * technical concepts contained herein are proprietary to Barefoot Networks,
 * Inc.
 * and its suppliers and may be covered by U.S. and Foreign Patents, patents in
 * process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material is
 * strictly forbidden unless prior written permission is obtained from
 * Barefoot Networks, Inc.
 *
 * No warranty, explicit or implicit is provided, unless granted under a
 * written agreement with Barefoot Networks, Inc.
 *
 *
 ******************************************************************************/


parser TofinoIngressParser(
        packet_in pkt,
        out ingress_intrinsic_metadata_t ig_intr_md) {
    state start {
        pkt.extract(ig_intr_md);
        transition select(ig_intr_md.resubmit_flag) {
            1 : parse_resubmit;
            0 : parse_port_metadata;
        }
    }

    state parse_resubmit {
        // Parse resubmitted packet here.
        transition reject;
    }

    state parse_port_metadata {
        pkt.advance(PORT_METADATA_SIZE);
        transition accept;
    }
}

parser TofinoEgressParser(
        packet_in pkt,
        out egress_intrinsic_metadata_t eg_intr_md) {
    state start {
        pkt.extract(eg_intr_md);
        transition accept;
    }
}


// ---------------------------------------------------------------------------
// Ingress parser
// ---------------------------------------------------------------------------
parser SwitchIngressParser(
        packet_in pkt,
        out switch_header_t hdr,
        out switch_ingress_metadata_t ig_md,
        out ingress_intrinsic_metadata_t ig_intr_md) {

    TofinoIngressParser() tofino_parser;

    state start {
        tofino_parser.apply(pkt, ig_intr_md);
        transition parse_ethernet;
    }
    // transition select ingress port; set p4ml_h's recirc_time field accordingly.
    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            0x0700   : parse_p4ml_ipv4;
            0x0900   : parse_bg;
            ETHERTYPE_IPV4 : parse_ipv4; //ETHERTYPE_IPV4=16w0x0800; (see headers.p4)
            0x0a00  : parse_p4ml; // this is a dpdk packet, for now we can directly parse_p4ml
            default : accept;
        }
    }

    state parse_ipv4{
        pkt.extract(hdr.ipv4);
        transition accept;
    }
    
	  // /* BG */
	state parse_bg {
	    pkt.extract(hdr.ipv4);
	    transition parse_udp_bg;
	}
	
	state parse_udp_bg {
	    pkt.extract(hdr.udp);
	    transition parse_p4ml_bg;
	}
	
	state parse_p4ml_bg {
	    pkt.extract(hdr.p4ml_bg);
	   	transition accept;
	}  

    state parse_p4ml_ipv4 {
        pkt.extract(hdr.ipv4);
        transition parse_p4ml;
    }

    state parse_p4ml {
        pkt.extract(hdr.p4ml);
        transition parse_agtr_index;  
    }
    state parse_agtr_index{
        pkt.extract(hdr.p4ml_agtr_index);
        transition parse_entry;
    }
    state parse_entry {
        pkt.extract(hdr.p4ml_entries); // field of switch_header_t, see struct.p4
        transition accept;
    }

}

// ---------------------------------------------------------------------------
// Ingress Deparser
// ---------------------------------------------------------------------------
control SwitchIngressDeparser(
        packet_out pkt,
        inout switch_header_t hdr,
        in switch_ingress_metadata_t ig_md,
        in ingress_intrinsic_metadata_for_deparser_t ig_dprsr_md) {
    apply {
        pkt.emit(hdr.ethernet);  
        pkt.emit(hdr.ipv4);
        pkt.emit(hdr.p4ml);
		pkt.emit(hdr.udp);
        pkt.emit(hdr.p4ml_agtr_index);
        pkt.emit(hdr.p4ml_entries);
    }
}

// ---------------------------------------------------------------------------
// Egress parser
// ---------------------------------------------------------------------------
parser SwitchEgressParser(
        packet_in pkt,
        out switch_header_t hdr,
        out switch_egress_metadata_t eg_md,
        out egress_intrinsic_metadata_t eg_intr_md) {

    TofinoEgressParser() tofino_parser;

    state start {
        tofino_parser.apply(pkt, eg_intr_md);
        transition parse_ethernet;
    }

    // transition select ingress port; set p4ml_h's recirc_time field accordingly.
    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            0x0700   : parse_p4ml_ipv4;
            0x0900   : parse_bg;
            ETHERTYPE_IPV4 : parse_ipv4; //ETHERTYPE_IPV4=16w0x0800; (see headers.p4)
            0x0a00  : parse_p4ml; // this is a dpdk packet, for now we can directly parse_p4ml
            default : accept;
        }
    }

    state parse_ipv4{
        pkt.extract(hdr.ipv4);
        transition accept;
    }
    
	  // /* BG */
	state parse_bg {
	    pkt.extract(hdr.ipv4);
	    transition parse_udp_bg;
	}
	
	state parse_udp_bg {
	    pkt.extract(hdr.udp);
	    transition parse_p4ml_bg;
	}
	
	state parse_p4ml_bg {
	    pkt.extract(hdr.p4ml_bg);
	   	transition accept;
	}  

    state parse_p4ml_ipv4 {
        pkt.extract(hdr.ipv4);
        transition parse_p4ml;
    }

    state parse_p4ml {
        pkt.extract(hdr.p4ml);
        transition accept;  
    }


}

// ---------------------------------------------------------------------------
// Egress Deparser
// ---------------------------------------------------------------------------
control SwitchEgressDeparser(
        packet_out pkt,
        inout switch_header_t hdr,
        in switch_egress_metadata_t eg_md,
        in egress_intrinsic_metadata_for_deparser_t eg_dprsr_md) {
    apply{
        pkt.emit(hdr.ethernet);
        pkt.emit(hdr.ipv4);
        pkt.emit(hdr.p4ml);
        pkt.emit(hdr.udp);
    }
}

