#include <cstdint>
#include "../common/common.h"

// TODO: use this later when we have multiple clients
// struct DPDKcontext
// {
//     /* data */
// };

//void send_packet(DPDKcontext* dpdk_context, int packet_size, uint64_t offset);
void send_packet(int packet_size, uint64_t offset);