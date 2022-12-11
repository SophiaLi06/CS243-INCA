control Route(
        inout switch_header_t hdr,
        inout entry_h p4ml_entries,
        inout ingress_intrinsic_metadata_for_deparser_t ig_intr_md_for_dprsr,
        inout ingress_intrinsic_metadata_for_tm_t ig_intr_md_for_tm
        ){
    
    action dmac_forward(PortId_t port) {
        /* zeroed out tensor */
        p4ml_entries.data0 = 0;
        p4ml_entries.data1 = 0;
        p4ml_entries.data2 = 0;
        p4ml_entries.data3 = 0;
        p4ml_entries.data4 = 0;
        p4ml_entries.data5 = 0;
        p4ml_entries.data6 = 0;
        p4ml_entries.data7 = 0;
        p4ml_entries.data8 = 0;
        p4ml_entries.data9 = 0;
        p4ml_entries.data10 = 0;
        p4ml_entries.data11 = 0;
        p4ml_entries.data12 = 0;
        p4ml_entries.data13 = 0;
        p4ml_entries.data14 = 0;
        p4ml_entries.data15 = 0;
        p4ml_entries.data16 = 0;
        p4ml_entries.data17 = 0;
        p4ml_entries.data18 = 0;
        p4ml_entries.data19 = 0;
        p4ml_entries.data20 = 0;
        p4ml_entries.data21 = 0;
        p4ml_entries.data22 = 0;
        p4ml_entries.data23 = 0;
        p4ml_entries.data24 = 0;
        p4ml_entries.data25 = 0;
        p4ml_entries.data26 = 0;
        p4ml_entries.data27 = 0;
        p4ml_entries.data28 = 0;
        p4ml_entries.data29 = 0;
        p4ml_entries.data30 = 0;
        p4ml_entries.data31 = 0;
        p4ml_entries.data32 = 0;
        p4ml_entries.data33 = 0;
        p4ml_entries.data34 = 0;
        p4ml_entries.data35 = 0;
        p4ml_entries.data36 = 0;
        p4ml_entries.data37 = 0;
        p4ml_entries.data38 = 0;
        p4ml_entries.data39 = 0;
        p4ml_entries.data40 = 0;
        p4ml_entries.data41 = 0;
        p4ml_entries.data42 = 0;
        p4ml_entries.data43 = 0;
        p4ml_entries.data44 = 0;
        p4ml_entries.data45 = 0;
        p4ml_entries.data46 = 0;
        p4ml_entries.data47 = 0;
        p4ml_entries.data48 = 0;
        p4ml_entries.data49 = 0;
        p4ml_entries.data50 = 0;
        p4ml_entries.data51 = 0;
        p4ml_entries.data52 = 0;
        p4ml_entries.data53 = 0;
        p4ml_entries.data54 = 0;
        p4ml_entries.data55 = 0;
        p4ml_entries.data56 = 0;
        p4ml_entries.data57 = 0;
        p4ml_entries.data58 = 0;
        p4ml_entries.data59 = 0;
        p4ml_entries.data60 = 0;
        p4ml_entries.data61 = 0;
        p4ml_entries.data62 = 0;
        p4ml_entries.data63 = 0;
        ig_intr_md_for_tm.ucast_egress_port = port;
    }

	action dmac_miss() {
        // Send the packet back where it came from.
        ig_intr_md_for_tm.ucast_egress_port = 176;
		//ig_intr_md_for_dprsr.drop_ctl = 3w1;
	}

    // action broadcast() {
    //     ig_intr_md_for_tm.mcast_grp_a = 1;
    // }

	table dmac {
		key = {
			hdr.ethernet.dst_addr : exact; //TODO: should add something like should_send
		}

		actions = {
            //broadcast;
			dmac_forward;
			@defaultonly dmac_miss;
		}

		const default_action = dmac_miss;
		size = 32;
	}
    apply{
        dmac.apply();
    }

}

