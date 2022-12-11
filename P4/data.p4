/* todo define the register values as signed */

typedef bit<32> const_t;
// const const_t total_aggregator_cnt = 32w0x0100;
const const_t total_aggregator_cnt = 32w0x4fff;
// const const_t total_aggregator_cnt = 32w0x27ff8;
typedef bit<16> total_aggregator_idx_t;
//typedef bit<15> total_aggregator_idx_t;


/* note that in the case of that the sume is already saturateed, 
        it is possible that the max positive + negative value;*/
// && value > -2147483647 should add this;
//&& register_lo > -2147483647
#define REG_TABLES(n)                                                                                           \
control ProcessData##n(                                                                                         \
        inout p4ml_h p4ml,                                                                                      \
        inout entry_h p4ml_entries,                                                                             \
        inout p4ml_meta_h mdata,                                                                                \
        in p4ml_agtr_index_h p4ml_agtr_index                                                                    \
  ){                                                                                                            \
                                                                                                                \
    Register<pair_t, total_aggregator_idx_t>(total_aggregator_cnt) register##n;                                 \
    RegisterAction<pair_t, total_aggregator_idx_t, data_type_t>(register##n) noequ0_write_read_data_entry##n= { \
        void apply(inout pair_t value, out data_type_t rv){                                                     \
            if(value.first[30:0] != 0x7fffffff){                                                                \
                value.first = value.first |+| p4ml_entries.data##n;                                             \
            }                                                                                                   \
            rv = value.first;                                                                                   \
        }                                                                                                       \
    };                                                                                                          \
    action noequ0_read_write_entry##n() {                                                                       \
        p4ml_entries.data##n= noequ0_write_read_data_entry##n.execute(p4ml_agtr_index.agtr);                    \
    }                                                                                                           \
                                                                                                                \
    RegisterAction<pair_t, total_aggregator_idx_t, data_type_t>(register##n) write_and_clean_first_entry##n= {  \
        void apply(inout pair_t value, out data_type_t rv){                                                     \
            if(value.first[30:0] != 0x7fffffff){                                                                \
                rv = value.first |+| p4ml_entries.data##n;                                                      \
                value.first = 0;                                                                                \
            }                                                                                                   \
            else{                                                                                               \
                rv = value.first;                                                                               \
                value.first = 0;                                                                                \
            }                                                                                                   \
        }                                                                                                       \
    };                                                                                                          \
    RegisterAction<pair_t, total_aggregator_idx_t, data_type_t>(register##n) aggr_first_entry##n= {             \
        void apply(inout pair_t value, out data_type_t rv){                                                     \
            if(value.first[30:0] != 0x7fffffff){                                                                \
                rv = value.first |+| p4ml_entries.data##n;                                                      \
            }                                                                                                   \
            else{                                                                                               \
                rv = value.first;                                                                               \
            }                                                                                                   \
        }                                                                                                       \
    };                                                                                                          \
    RegisterAction<pair_t, total_aggregator_idx_t, data_type_t>(register##n) clean_entry##n= {                  \
        void apply(inout pair_t value, out data_type_t rv){                                                     \
            value.first = 0;                                                                                    \
            value.second = 0;                                                                                   \
        }                                                                                                       \
    };                                                                                                          \
                                                                                                                \
    RegisterAction<pair_t, total_aggregator_idx_t, data_type_t>(register##n) write_data_entry##n= {             \
        void apply(inout pair_t value, out data_type_t rv){                                                     \
                value.first = p4ml_entries.data##n;                                                             \
        }                                                                                                       \
    };                                                                                                          \
    action processentry##n() {                                                                                  \
        write_data_entry##n.execute(p4ml_agtr_index.agtr);                                                      \
    }                                                                                                           \
                                                                                                                \
    RegisterAction<pair_t, total_aggregator_idx_t, data_type_t>(register##n) read_first_data_entry##n= {        \
        void apply(inout pair_t value, out data_type_t rv){                                                     \
            rv = value.first;                                                                                   \
        }                                                                                                       \
    };                                                                                                          \
    action read_first_entry##n() {                                                                              \
      p4ml_entries.data##n= read_first_data_entry##n.execute(p4ml_agtr_index.agtr);                             \
    }                                                                                                           \
    action zeroed_entries##n() {                                                                                \
        p4ml_entries.data##n = 0;                                                                               \
    }                                                                                                           \
    table processEntry##n {                                                                                     \
        key =  {                                                                                                \
            mdata.bitmap : ternary;                                                                             \
            mdata.need_send_out : exact;                                                                        \
            mdata.isAggregate : ternary;                                                                        \
            p4ml.dataIndex : exact;                                                                             \
        }                                                                                                       \
        actions = {                                                                                             \
            processentry##n;                                                                                    \
            noequ0_read_write_entry##n;                                                                         \
            read_first_entry##n;                                                                                \
        }                                                                                                       \
        size = 8;                                                                                               \
    }                                                                                                           \
    apply{                                                                                                      \
        /*processEntry##n.apply();*/                                                                            \
        if(mdata.seen_isAggregate == 1){/* the first packet for a given slot of a given round*/                 \
            processentry##n();                                                                                  \
        }                                                                                                       \
        else{                                                                                                   \
            noequ0_read_write_entry##n();                                                                       \
        }                                                                                                       \
    }                                                                                                           \
                                                                                                                \
}                                                                                                               \


#define PROCESS_ENTRY                                                      \
process_data0.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data1.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data2.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data3.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data4.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data5.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data6.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data7.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data8.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data9.apply( p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data10.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data11.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data12.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data13.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data14.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data15.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data16.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data17.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data18.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data19.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data20.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data21.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data22.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data23.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data24.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data25.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data26.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data27.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data28.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data29.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data30.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data31.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data32.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data33.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data34.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data35.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data36.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data37.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data38.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data39.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data40.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data41.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data42.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data43.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data44.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data45.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data46.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data47.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data48.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data49.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data50.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data51.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data52.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data53.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data54.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data55.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data56.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data57.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data58.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data59.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data60.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data61.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data62.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \
process_data63.apply(p4ml, p4ml_entries, mdata, p4ml_agtr_index);          \

 

REG_TABLES(0) 
REG_TABLES(1) 
REG_TABLES(2) 
REG_TABLES(3) 
REG_TABLES(4) 
REG_TABLES(5) 
REG_TABLES(6) 
REG_TABLES(7) 
REG_TABLES(8) 
REG_TABLES(9) 
REG_TABLES(10) 
REG_TABLES(11) 
REG_TABLES(12) 
REG_TABLES(13) 
REG_TABLES(14) 
REG_TABLES(15) 
REG_TABLES(16) 
REG_TABLES(17) 
REG_TABLES(18) 
REG_TABLES(19) 
REG_TABLES(20) 
REG_TABLES(21) 
REG_TABLES(22) 
REG_TABLES(23) 
REG_TABLES(24) 
REG_TABLES(25) 
REG_TABLES(26) 
REG_TABLES(27) 
REG_TABLES(28) 
REG_TABLES(29) 
REG_TABLES(30) 
REG_TABLES(31) 
REG_TABLES(32) 
REG_TABLES(33) 
REG_TABLES(34) 
REG_TABLES(35) 
REG_TABLES(36) 
REG_TABLES(37) 
REG_TABLES(38) 
REG_TABLES(39) 
REG_TABLES(40)
REG_TABLES(41) 
REG_TABLES(42) 
REG_TABLES(43) 
REG_TABLES(44) 
REG_TABLES(45) 
REG_TABLES(46) 
REG_TABLES(47)
REG_TABLES(48)
REG_TABLES(49)
REG_TABLES(50)
REG_TABLES(51)
REG_TABLES(52)
REG_TABLES(53)
REG_TABLES(54)
REG_TABLES(55)
REG_TABLES(56)
REG_TABLES(57)
REG_TABLES(58)
REG_TABLES(59)
REG_TABLES(60)
REG_TABLES(61)
REG_TABLES(62)
REG_TABLES(63)
