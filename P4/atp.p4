#include "data.p4"
#include "route.p4"

/// Initialize an array of <size> registers and set their value to
/// initial_value.
/// Register(bit<32> size, T initial_value);

Register<bit<32>, total_aggregator_idx_t>(total_aggregator_cnt, 0) roundNumbers;
Register<bit<32>, total_aggregator_idx_t>(total_aggregator_cnt, 0) bitmap;
// Register<bit<32>, total_aggregator_idx_t>(total_aggregator_cnt, 0) max_info;
Register<bit<32>, total_aggregator_idx_t>(total_aggregator_cnt, 0) norm_info;
Register<bit<8>, total_aggregator_idx_t>(total_aggregator_cnt, 0) receive_count;

control AppIdSeq(
    inout switch_header_t hdr,
    inout ingress_intrinsic_metadata_for_deparser_t ig_intr_md_for_dprsr,
    inout ingress_intrinsic_metadata_for_tm_t ig_intr_md_for_tm,
    inout p4ml_h p4ml,
    inout entry_h p4ml_entries,
    inout p4ml_meta_h mdata,
    inout p4ml_agtr_index_h p4ml_agtr_index){
    
    RegisterAction<bit<32>, total_aggregator_idx_t, bit<32>>(roundNumbers) read_roundNum = {
        void apply(inout bit<32> value, out bit<32> rv){
            rv = value;
            if(value < p4ml.roundNum){
                value = p4ml.roundNum;
            }
        }
    };

    RegisterAction<bit<32>, total_aggregator_idx_t, bit<2>>(roundNumbers) normal_check_roundNum = {
        void apply(inout bit<32> value, out bit<2> rv){
            if(value > p4ml.roundNum){
                rv = (bit<2>)0;
            }else{
                if (value == p4ml.roundNum) {
                    rv = (bit<2>)1;
                }
                else{
                    value = p4ml.roundNum; // use this larger roundNum
                    rv = (bit<2>)2;
                }
            }
        }
    };

    RegisterAction<bit<32>, total_aggregator_idx_t, bit<32>>(bitmap) normal_read_write_bitmap = {
        void apply(inout bit<32> value, out bit<32> rv){
            value = value | p4ml.bitmap;
            rv = value;
        }
    };

    RegisterAction<bit<8>, total_aggregator_idx_t, bit<8>>(receive_count) normal_incr_check_count = {
        void apply(inout bit<8> value, out bit<8> rv){
            if (value == p4ml.num_worker){ // this aggregator has finished the last round
                rv = 1;
                value = 1;
            }
            else{
                rv = value + 1;
                value = value + 1;
            }
            //rv = value;
        }
    };

    RegisterAction<bit<8>, total_aggregator_idx_t, bit<8>>(receive_count) reset_check_count = {
        void apply(inout bit<8> value, out bit<8> rv){
            rv = 1;
            value = 1;
        }
    };

    RegisterAction<bit<32>, total_aggregator_idx_t, bit<32>>(bitmap) clean_bitmap = {
        void apply(inout bit<32> value, out bit<32> rv){
            rv = value;
            value = 0;
        }
    };

    // RegisterAction<bit<32>, total_aggregator_idx_t, bit<32>>(max_info) update_max = {
    //     void apply(inout bit<32> value, out bit<32> rv){
    //         if (p4ml.max_info > value){
    //             value = p4ml.max_info;
    //         }
    //         rv = value;
    //     }
    // };

    RegisterAction<bit<32>, total_aggregator_idx_t, bit<32>>(norm_info) update_norm = {
        void apply(inout bit<32> value, out bit<32> rv){
            if(p4ml.norm_info > value){
                value = p4ml.norm_info;
            }
            rv = value;
        }
    };

    // RegisterAction<bit<32>, total_aggregator_idx_t, bit<32>>(max_info) set_max = {
    //     void apply(inout bit<32> value, out bit<32> rv){
    //         value = p4ml.max_info;
    //         rv = value;
    //     }
    // };

    RegisterAction<bit<32>, total_aggregator_idx_t, bit<32>>(norm_info) set_norm = {
        void apply(inout bit<32> value, out bit<32> rv){
            value = p4ml.norm_info;
            rv = value;
        }
    };

    // RegisterAction<bit<8>, total_aggregator_idx_t, bit<8>>(receive_count) clean_check_count = {
    //     void apply(inout bit<8> value){
    //         value = 0;
    //     }
    // };

    action drop_pkt(){
		ig_intr_md_for_dprsr.drop_ctl = 3w1;
	}

    action broadcast(){
        ig_intr_md_for_tm.mcast_grp_a = 1;
        // hdr.ethernet.dst_addr = 08:c0:eb:a7:4c:85;
        // ig_intr_md_for_tm.ucast_egress_port = 176;
        // No need for egress processing, skip it and use empty controls for egress.
        // ig_intr_md_for_tm.bypass_egress = 1w1;
    }

    action set_recirc_port(PortId_t recirc_port){
        ig_intr_md_for_tm.ucast_egress_port = recirc_port;
    }

    table recirc_port {
        key = {
            p4ml.recirc_time : exact;
        }
        actions = {
            set_recirc_port;
            @defaultonly drop_pkt;
        }
        const default_action = drop_pkt;
        size = 8;
    }

    ProcessData0() process_data0;
    ProcessData1() process_data1;
    ProcessData2() process_data2;
    ProcessData3() process_data3;
    ProcessData4() process_data4;
    ProcessData5() process_data5;
    ProcessData6() process_data6;
    ProcessData7() process_data7;
    ProcessData8() process_data8;
    ProcessData9() process_data9;
    ProcessData10() process_data10;
    ProcessData11() process_data11;
    ProcessData12() process_data12;
    ProcessData13() process_data13;
    ProcessData14() process_data14;
    ProcessData15() process_data15;
    ProcessData16() process_data16;
    ProcessData17() process_data17;
    ProcessData18() process_data18;
    ProcessData19() process_data19;
    ProcessData20() process_data20;
    ProcessData21() process_data21;
    ProcessData22() process_data22;
    ProcessData23() process_data23;
    ProcessData24() process_data24;
    ProcessData25() process_data25;
    ProcessData26() process_data26;
    ProcessData27() process_data27;
    ProcessData28() process_data28;
    ProcessData29() process_data29;
    ProcessData30() process_data30;
    // start adding more entries
    ProcessData31() process_data31;
    ProcessData32() process_data32;
    ProcessData33() process_data33;
    ProcessData34() process_data34;
    ProcessData35() process_data35;
    ProcessData36() process_data36;
    ProcessData37() process_data37;
    ProcessData38() process_data38;
    ProcessData39() process_data39;
    ProcessData40() process_data40;
    ProcessData41() process_data41;
    ProcessData42() process_data42;
    ProcessData43() process_data43;
    ProcessData44() process_data44;
    ProcessData45() process_data45;
    ProcessData46() process_data46;
    ProcessData47() process_data47;
    ProcessData48() process_data48;
    ProcessData49() process_data49;
    ProcessData50() process_data50;
    ProcessData51() process_data51;
    ProcessData52() process_data52;
    ProcessData53() process_data53;
    ProcessData54() process_data54;
    ProcessData55() process_data55;
    ProcessData56() process_data56;
    ProcessData57() process_data57;
    ProcessData58() process_data58;
    ProcessData59() process_data59;
    ProcessData60() process_data60;
    ProcessData61() process_data61;
    ProcessData62() process_data62;
    ProcessData63() process_data63;

    Route() route;

    apply{
        /* check roundNum */
        
        mdata.match_roundnum = normal_check_roundNum.execute(p4ml_agtr_index.agtr);
        if(mdata.match_roundnum == (bit<2>)0){
            /* Process entries*/
            p4ml.roundNum = 0; // setting the roundNum to 0 to denote that this is a zeroed out tensor
            route.apply(hdr, p4ml_entries, ig_intr_md_for_dprsr, ig_intr_md_for_tm);
        }
        else{
            if(mdata.match_roundnum == (bit<2>)2){
                /* as we are increasing the roundNum, we need to reset everything */
                mdata.seen_isAggregate = reset_check_count.execute(p4ml_agtr_index.agtr);
                p4ml.norm_info = set_norm.execute(p4ml_agtr_index.agtr);

            }
            else{
                /* increment the number of workers we have heard from, and check whether we have heard from everyone */
                mdata.seen_isAggregate = normal_incr_check_count.execute(p4ml_agtr_index.agtr);

                p4ml.norm_info = update_norm.execute(p4ml_agtr_index.agtr);
            }

            /* Process entries if this is a gradient packet */
            if(p4ml.packet_type == 0){
                PROCESS_ENTRY
            }


            if(p4ml.recirc_time == (bit<8>)3){
                if(mdata.seen_isAggregate == p4ml.num_worker){
                    /* bitmap, max, and norm cleaning here */
                    p4ml.bitmap = clean_bitmap.execute(p4ml_agtr_index.agtr);

                    broadcast();
                }
                else{
                    /* bitmap processing here */
                    p4ml.bitmap = normal_read_write_bitmap.execute(p4ml_agtr_index.agtr);

                    drop_pkt();
                }

            }
            else{
                p4ml_agtr_index.agtr = p4ml_agtr_index.agtr + 1;
                p4ml.recirc_time = p4ml.recirc_time + 1;
                recirc_port.apply();
                ig_intr_md_for_tm.bypass_egress = 1w1;
            }
        }

    }// apply
}// control AppIdSeq