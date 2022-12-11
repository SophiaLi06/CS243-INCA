#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif
#include "headers.p4"
#include "struct.p4"
#include "parse.p4"
#include "atp.p4"
#include "route.p4"
//-----------------------------------------------------------------------------
// Destination MAC lookup
// - Bridge out the packet of the interface in the MAC entry.
// - Flood the packet out of all ports within the ingress BD.
//-----------------------------------------------------------------------------

control SwitchIngress(
        inout switch_header_t hdr,
        inout switch_ingress_metadata_t ig_md,
        in ingress_intrinsic_metadata_t ig_intr_md,
        in ingress_intrinsic_metadata_from_parser_t ig_prsr_md,
        inout ingress_intrinsic_metadata_for_deparser_t ig_intr_md_for_dprsr,
        inout ingress_intrinsic_metadata_for_tm_t ig_intr_md_for_tm){

    action dmac_forward(PortId_t port) {
        ig_intr_md_for_tm.ucast_egress_port = port;
        // No need for egress processing, skip it and use empty controls for egress.
        ig_intr_md_for_tm.bypass_egress = 1w1;
    }

	action dmac_miss() {
		ig_intr_md_for_dprsr.drop_ctl = 3w1;
	}

    action broadcast(){
        ig_intr_md_for_tm.mcast_grp_a = 1;
    }

	table dmac {
		key = {
			hdr.ethernet.dst_addr : exact;
		}

		actions = {
			dmac_forward;
			@defaultonly dmac_miss;
		}

		const default_action = dmac_miss;
		size = 32;
	}

    AppIdSeq() appid_seq; // control in atp.p4

    apply{

        if(hdr.p4ml.isValid()){
            // update ingress global timestamp
            if(hdr.p4ml.recirc_time == 0){
                hdr.p4ml.ingress_global = ig_prsr_md.global_tstamp;
            }
            //handle normal p4ml packets
            appid_seq.apply(hdr, ig_intr_md_for_dprsr, ig_intr_md_for_tm, hdr.p4ml, hdr.p4ml_entries, ig_md.mdata, hdr.p4ml_agtr_index);

        }else{
            if(hdr.ethernet.ether_type == 0x0d00){
                // broadcast the sw ps reply to all workers
                broadcast();
            }
            else{
                //bg traffic; only forward;
                dmac.apply();
            }
            // dmac_miss();
        }
	}

}

control SwitchEgress(
        inout switch_header_t hdr,
        inout switch_egress_metadata_t eg_md,
        in egress_intrinsic_metadata_t eg_intr_md,
        in egress_intrinsic_metadata_from_parser_t eg_intr_from_prsr,
        inout egress_intrinsic_metadata_for_deparser_t eg_intr_md_for_dprsr,
        inout egress_intrinsic_metadata_for_output_port_t eg_intr_md_for_oport) {

	apply {
        if(hdr.p4ml.isValid()){
            // update egress global timestamp
            hdr.p4ml.egress_global = eg_intr_from_prsr.global_tstamp;
        }
    }

}
Pipeline(SwitchIngressParser(),
        SwitchIngress(),
        SwitchIngressDeparser(),
        SwitchEgressParser(),
        SwitchEgress(),
        SwitchEgressDeparser()) pipe;

Switch(pipe) main;

