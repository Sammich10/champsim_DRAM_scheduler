#include "dram_controller.h"

// initialized in main.cc
uint32_t DRAM_MTPS, DRAM_DBUS_RETURN_TIME,
         tRP, tRCD, tCAS;

void print_dram_config()
{
    cout << "dram_channel_width " << DRAM_CHANNEL_WIDTH << endl
        << "dram_wq_size " << DRAM_WQ_SIZE << endl
        << "dram_rq_size " << DRAM_RQ_SIZE << endl
        << "tRP " << tRP_DRAM_NANOSECONDS << endl
        << "tRCD " << tRCD_DRAM_NANOSECONDS << endl
        << "tCAS " << tCAS_DRAM_NANOSECONDS << endl
        << "dram_dbus_turn_around_time " << DRAM_DBUS_TURN_AROUND_TIME << endl
        << "dram_write_high_wm " << DRAM_WRITE_HIGH_WM << endl
        << "dram_write_low_wm " << DRAM_WRITE_LOW_WM << endl
        << "min_dram_writes_per_switch " << MIN_DRAM_WRITES_PER_SWITCH << endl
        << "dram_mtps " << DRAM_MTPS << endl
        << "dram_dbus_return_time " << DRAM_DBUS_RETURN_TIME << endl
        #ifdef ATLAS
        << "USING ATLAS SCHEDULING" << endl
        #endif
        << endl;
}

void MEMORY_CONTROLLER::reset_remain_requests(PACKET_QUEUE *queue, uint32_t channel)
{
    for (uint32_t i=0; i<queue->SIZE; i++) {
        if (queue->entry[i].scheduled) {

            uint64_t op_addr = queue->entry[i].address;
            uint32_t op_cpu = queue->entry[i].cpu,
                     op_channel = dram_get_channel(op_addr), 
                     op_rank = dram_get_rank(op_addr), 
                     op_bank = dram_get_bank(op_addr), 
                     op_row = dram_get_row(op_addr);

#ifdef DEBUG_PRINT
            //uint32_t op_column = dram_get_column(op_addr);
#endif

            // update open row
            if ((bank_request[op_channel][op_rank][op_bank].cycle_available - tCAS) <= current_core_cycle[op_cpu])
                bank_request[op_channel][op_rank][op_bank].open_row = op_row;
            else
                bank_request[op_channel][op_rank][op_bank].open_row = UINT32_MAX;

            // this bank is ready for another DRAM request
            bank_request[op_channel][op_rank][op_bank].request_index = -1;
            bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
            bank_request[op_channel][op_rank][op_bank].working = 0;
            bank_request[op_channel][op_rank][op_bank].cycle_available = current_core_cycle[op_cpu];
            if (bank_request[op_channel][op_rank][op_bank].is_write) {
                scheduled_writes[channel]--;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
            }
            else if (bank_request[op_channel][op_rank][op_bank].is_read) {
                scheduled_reads[channel]--;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;
            }

            queue->entry[i].scheduled = 0;
            queue->entry[i].event_cycle = current_core_cycle[op_cpu];

            DP ( if (warmup_complete[op_cpu]) {
            cout << queue->NAME << " instr_id: " << queue->entry[i].instr_id << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel] << endl; });

        }
    }
    
    update_schedule_cycle(&RQ[channel]);
    update_schedule_cycle(&WQ[channel]);
    update_process_cycle(&RQ[channel]);
    update_process_cycle(&WQ[channel]);

#ifdef SANITY_CHECK
    if (queue->is_WQ) {
        if (scheduled_writes[channel] != 0)
            assert(0);
    }
    else {
        if (scheduled_reads[channel] != 0)
            assert(0);
    }
#endif
}

void MEMORY_CONTROLLER::operate()
{
    for (uint32_t i=0; i<DRAM_CHANNELS; i++) {
        //if ((write_mode[i] == 0) && (WQ[i].occupancy >= DRAM_WRITE_HIGH_WM)) {
      if ((write_mode[i] == 0) && ((WQ[i].occupancy >= DRAM_WRITE_HIGH_WM) || ((RQ[i].occupancy == 0) && (WQ[i].occupancy > 0)))) { // use idle cycles to perform writes
            write_mode[i] = 1;

            // reset scheduled RQ requests
            reset_remain_requests(&RQ[i], i);
            // add data bus turn-around time
            dbus_cycle_available[i] += DRAM_DBUS_TURN_AROUND_TIME;
        } else if (write_mode[i]) {

            if (WQ[i].occupancy == 0)
                write_mode[i] = 0;
            else if (RQ[i].occupancy && (WQ[i].occupancy < DRAM_WRITE_LOW_WM))
                write_mode[i] = 0;

            if (write_mode[i] == 0) {
                // reset scheduled WQ requests
                reset_remain_requests(&WQ[i], i);
                // add data bus turnaround time
                dbus_cycle_available[i] += DRAM_DBUS_TURN_AROUND_TIME;
            }
        }

        // handle write
        // schedule new entry
        if (write_mode[i] && (WQ[i].next_schedule_index < WQ[i].SIZE)) {
            if (WQ[i].next_schedule_cycle <= current_core_cycle[WQ[i].entry[WQ[i].next_schedule_index].cpu])
                schedule(&WQ[i]);
        }

        // process DRAM requests
        if (write_mode[i] && (WQ[i].next_process_index < WQ[i].SIZE)) {
            if (WQ[i].next_process_cycle <= current_core_cycle[WQ[i].entry[WQ[i].next_process_index].cpu])
                process(&WQ[i]);
        }

        // handle read
        // schedule new entry
        if ((write_mode[i] == 0) && (RQ[i].next_schedule_index < RQ[i].SIZE)) {
            if (RQ[i].next_schedule_cycle <= current_core_cycle[RQ[i].entry[RQ[i].next_schedule_index].cpu])
                schedule(&RQ[i]);
        }

        // process DRAM requests
        if ((write_mode[i] == 0) && (RQ[i].next_process_index < RQ[i].SIZE)) {
            if (RQ[i].next_process_cycle <= current_core_cycle[RQ[i].entry[RQ[i].next_process_index].cpu])
                process(&RQ[i]);
        }
    }
}

void MEMORY_CONTROLLER::schedule(PACKET_QUEUE *queue)
{

    #ifdef BLISS

    uint64_t read_addr;
    uint32_t read_channel, read_rank, read_bank, read_row;
    uint8_t  row_buffer_hit = 0;

    int index = -1;
    uint64_t oldest_cycle = UINT64_MAX;

    //if BLISS is enabled prioritize requests that are not blacklisted, then requests that are row hits, and then requests that are older

    //first, search for the oldest row-buffer hit request that is not blacklisted
    for (uint32_t i = 0; i < queue->SIZE; i++) {
        uint8_t CPU_ID = queue->entry[i].cpu;
        // skip if blacklisted
        if(BLACKLIST[CPU_ID]){
            continue;
        }

        // search for the oldest open row hit
        // already scheduled
        if (queue->entry[i].scheduled) 
            continue;

        // empty entry
        read_addr = queue->entry[i].address;
        if (read_addr == 0) 
            continue;

        read_channel = dram_get_channel(read_addr);
        read_rank = dram_get_rank(read_addr);
        read_bank = dram_get_bank(read_addr);

        // bank is busy
        if (bank_request[read_channel][read_rank][read_bank].working) { // should we check this or not? how do we know if bank is busy or not for all requests in the queue?

            //DP ( if (warmup_complete[0]) {
            //cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " bank is busy";
            //cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
            //cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
            //cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });

            continue;
        }

        read_row = dram_get_row(read_addr);
        //read_column = dram_get_column(read_addr);

        // check open row
        if (bank_request[read_channel][read_rank][read_bank].open_row != read_row) {

            /*
            DP ( if (warmup_complete[0]) {
            cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " row is inactive";
            cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
            cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
            cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });
            */

            continue;
        }

        // select the oldest entry
        if (queue->entry[i].event_cycle < oldest_cycle) {
            oldest_cycle = queue->entry[i].event_cycle;
            index = i;
            row_buffer_hit = 1;
        }	  
    }
    
    // if no non-blacklisted requests are found, then search for the oldest row-buffer hit request
    if(index == -1){
        for(uint32_t i=0; i<queue->SIZE; i++){
            // search for the oldest open row hit
            // already scheduled
            if (queue->entry[i].scheduled) 
                continue;

            // empty entry
            read_addr = queue->entry[i].address;
            if (read_addr == 0) 
                continue;

            read_channel = dram_get_channel(read_addr);
            read_rank = dram_get_rank(read_addr);
            read_bank = dram_get_bank(read_addr);

            // bank is busy
            if (bank_request[read_channel][read_rank][read_bank].working) { // should we check this or not? how do we know if bank is busy or not for all requests in the queue?

                //DP ( if (warmup_complete[0]) {
                //cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " bank is busy";
                //cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
                //cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
                //cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });

                continue;
            }

            read_row = dram_get_row(read_addr);
            //read_column = dram_get_column(read_addr);

            // check open row
            if (bank_request[read_channel][read_rank][read_bank].open_row != read_row) {

                /*
                DP ( if (warmup_complete[0]) {
                cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " row is inactive";
                cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
                cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
                cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });
                */

                continue;
            }

            // select the oldest entry
            if (queue->entry[i].event_cycle < oldest_cycle) {
                oldest_cycle = queue->entry[i].event_cycle;
                index = i;
                row_buffer_hit = 1;
            }	  
        }
        
    }

    // no matching non-blacklisted open_row (row buffer miss), simply find the oldest request, even if it is blacklisted
    if (index == -1) { 

        oldest_cycle = UINT64_MAX;
        for (uint32_t i=0; i<queue->SIZE; i++) {

            // already scheduled
            if (queue->entry[i].scheduled)
                continue;

            // empty entry
            read_addr = queue->entry[i].address;
            if (read_addr == 0) 
                continue;

            // bank is busy
            read_channel = dram_get_channel(read_addr);
            read_rank = dram_get_rank(read_addr);
            read_bank = dram_get_bank(read_addr);
            if (bank_request[read_channel][read_rank][read_bank].working) 
                continue;

            //read_row = dram_get_row(read_addr);
            //read_column = dram_get_column(read_addr);

            // select the oldest entry
            if (queue->entry[i].event_cycle <= oldest_cycle) {
                oldest_cycle = queue->entry[i].event_cycle;
                index = i;
            }
        }
    }

    // at this point, the scheduler knows which bank to access and if the request is a row buffer hit or miss
    if (index != -1) { // scheduler might not find anything if all requests are already scheduled or all banks are busy

        uint64_t LATENCY = 0;
        if (row_buffer_hit)  
            LATENCY = tCAS;
        else 
            LATENCY = tRP + tRCD + tCAS;

        uint64_t op_addr = queue->entry[index].address;
        uint32_t op_cpu = queue->entry[index].cpu,
                 op_channel = dram_get_channel(op_addr), 
                 op_rank = dram_get_rank(op_addr), 
                 op_bank = dram_get_bank(op_addr), 
                 op_row = dram_get_row(op_addr);
        #ifdef DEBUG_PRINT
                uint32_t op_column = dram_get_column(op_addr);
        #endif

        // this bank is now busy
        bank_request[op_channel][op_rank][op_bank].working = 1;
        bank_request[op_channel][op_rank][op_bank].working_type = queue->entry[index].type;
        bank_request[op_channel][op_rank][op_bank].cycle_available = current_core_cycle[op_cpu] + LATENCY;

        bank_request[op_channel][op_rank][op_bank].request_index = index;
        bank_request[op_channel][op_rank][op_bank].row_buffer_hit = row_buffer_hit;
        if (queue->is_WQ) {
            bank_request[op_channel][op_rank][op_bank].is_write = 1;
            bank_request[op_channel][op_rank][op_bank].is_read = 0;
            scheduled_writes[op_channel]++;
        }
        else {
            bank_request[op_channel][op_rank][op_bank].is_write = 0;
            bank_request[op_channel][op_rank][op_bank].is_read = 1;
            scheduled_reads[op_channel]++;
        }

        // update open row
        bank_request[op_channel][op_rank][op_bank].open_row = op_row;

        queue->entry[index].scheduled = 1;
        queue->entry[index].event_cycle = current_core_cycle[op_cpu] + LATENCY;

        update_schedule_cycle(queue);
        update_process_cycle(queue);

        DP (if (warmup_complete[op_cpu]) {
        cout << "[" << queue->NAME << "] " <<  __func__ << " instr_id: " << queue->entry[index].instr_id;
        cout << " row buffer: " << (row_buffer_hit ? (int)bank_request[op_channel][op_rank][op_bank].open_row : -1) << hex;
        cout << " address: " << queue->entry[index].address << " full_addr: " << queue->entry[index].full_addr << dec;
        cout << " index: " << index << " occupancy: " << queue->occupancy;
        cout << " ch: " << op_channel << " rank: " << op_rank << " bank: " << op_bank; // wrong from here
        cout << " row: " << op_row << " col: " << op_column;
        cout << " current: " << current_core_cycle[op_cpu] << " event: " << queue->entry[index].event_cycle << endl; });
    }

    #endif

    #ifdef ATLAS

    uint64_t read_addr;
    uint32_t read_channel, read_rank, read_bank, read_row;
    uint8_t  row_buffer_hit = 0;

    int index = -1;
    uint64_t oldest_cycle = UINT64_MAX;
    uint32_t lowest_rank = UINT32_MAX;

    //first, search for a request from the lowest ATLAS rank, that is the oldest open row hit
    for (uint32_t i = 0; i < queue->SIZE; i++) {
        uint8_t CPU_ID = queue->entry[i].cpu;

        // search for the oldest open row hit
        // already scheduled
        if (queue->entry[i].scheduled) 
            continue;

        // empty entry
        read_addr = queue->entry[i].address;
        if (read_addr == 0) 
            continue;

        read_channel = dram_get_channel(read_addr);
        read_rank = dram_get_rank(read_addr);
        read_bank = dram_get_bank(read_addr);

        // bank is busy
        if (bank_request[read_channel][read_rank][read_bank].working) { // should we check this or not? how do we know if bank is busy or not for all requests in the queue?

            //DP ( if (warmup_complete[0]) {
            //cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " bank is busy";
            //cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
            //cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
            //cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });

            continue;
        }

        read_row = dram_get_row(read_addr);
        //read_column = dram_get_column(read_addr);

        // check open row
        if (bank_request[read_channel][read_rank][read_bank].open_row != read_row) {

            /*
            DP ( if (warmup_complete[0]) {
            cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " row is inactive";
            cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
            cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
            cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });
            */

            continue;
        }

        // select the entry from the lowest rank that has an open row hit
        if (ATLAS_RANK[CPU_ID] < lowest_rank){
            oldest_cycle = queue->entry[i].event_cycle;
            lowest_rank = ATLAS_RANK[CPU_ID];
            index = i;
            row_buffer_hit = 1;
        }

        // if there is an entry from the same rank, select the oldest entry
        if (queue->entry[i].event_cycle < oldest_cycle && ATLAS_RANK[CPU_ID] == lowest_rank) {
            oldest_cycle = queue->entry[i].event_cycle;
            lowest_rank = ATLAS_RANK[CPU_ID];
            index = i;
            row_buffer_hit = 1;
        }	  
    }
    // no matching open_row (row buffer miss)
    if (index == -1) { 
        lowest_rank = UINT32_MAX;
        oldest_cycle = UINT64_MAX;
        for (uint32_t i=0; i<queue->SIZE; i++) {
            uint8_t CPU_ID = queue->entry[i].cpu;

            // already scheduled
            if (queue->entry[i].scheduled)
                continue;

            // empty entry
            read_addr = queue->entry[i].address;
            if (read_addr == 0) 
                continue;

            // bank is busy
            read_channel = dram_get_channel(read_addr);
            read_rank = dram_get_rank(read_addr);
            read_bank = dram_get_bank(read_addr);
            if (bank_request[read_channel][read_rank][read_bank].working) 
                continue;

            //read_row = dram_get_row(read_addr);
            //read_column = dram_get_column(read_addr);

            // select the entry from the lowest rank that has an open row hit
            if (ATLAS_RANK[CPU_ID] < lowest_rank){
                oldest_cycle = queue->entry[i].event_cycle;
                lowest_rank = ATLAS_RANK[CPU_ID];
                index = i;
                row_buffer_hit = 1;
            }

            // if there is an entry from the same rank, select the oldest entry
            if (queue->entry[i].event_cycle < oldest_cycle && ATLAS_RANK[CPU_ID] == lowest_rank) {
                oldest_cycle = queue->entry[i].event_cycle;
                lowest_rank = ATLAS_RANK[CPU_ID];
                index = i;
                row_buffer_hit = 1;
            }	  
        }
    }
    
    // at this point, the scheduler knows which bank to access and if the request is a row buffer hit or miss
    if (index != -1) { // scheduler might not find anything if all requests are already scheduled or all banks are busy

        uint64_t LATENCY = 0;
        if (row_buffer_hit)  
            LATENCY = tCAS;
        else 
            LATENCY = tRP + tRCD + tCAS;

        uint64_t op_addr = queue->entry[index].address;
        uint32_t op_cpu = queue->entry[index].cpu,
                 op_channel = dram_get_channel(op_addr), 
                 op_rank = dram_get_rank(op_addr), 
                 op_bank = dram_get_bank(op_addr), 
                 op_row = dram_get_row(op_addr);
        #ifdef DEBUG_PRINT
                uint32_t op_column = dram_get_column(op_addr);
        #endif

        // this bank is now busy
        bank_request[op_channel][op_rank][op_bank].working = 1;
        bank_request[op_channel][op_rank][op_bank].working_type = queue->entry[index].type;
        bank_request[op_channel][op_rank][op_bank].cycle_available = current_core_cycle[op_cpu] + LATENCY;

        bank_request[op_channel][op_rank][op_bank].request_index = index;
        bank_request[op_channel][op_rank][op_bank].row_buffer_hit = row_buffer_hit;
        if (queue->is_WQ) {
            bank_request[op_channel][op_rank][op_bank].is_write = 1;
            bank_request[op_channel][op_rank][op_bank].is_read = 0;
            scheduled_writes[op_channel]++;
        }
        else {
            bank_request[op_channel][op_rank][op_bank].is_write = 0;
            bank_request[op_channel][op_rank][op_bank].is_read = 1;
            scheduled_reads[op_channel]++;
        }

        // update open row
        bank_request[op_channel][op_rank][op_bank].open_row = op_row;

        queue->entry[index].scheduled = 1;
        queue->entry[index].event_cycle = current_core_cycle[op_cpu] + LATENCY;

        update_schedule_cycle(queue);
        update_process_cycle(queue);

        DP (if (warmup_complete[op_cpu]) {
        cout << "[" << queue->NAME << "] " <<  __func__ << " instr_id: " << queue->entry[index].instr_id;
        cout << " row buffer: " << (row_buffer_hit ? (int)bank_request[op_channel][op_rank][op_bank].open_row : -1) << hex;
        cout << " address: " << queue->entry[index].address << " full_addr: " << queue->entry[index].full_addr << dec;
        cout << " index: " << index << " occupancy: " << queue->occupancy;
        cout << " ch: " << op_channel << " rank: " << op_rank << " bank: " << op_bank; // wrong from here
        cout << " row: " << op_row << " col: " << op_column;
        cout << " current: " << current_core_cycle[op_cpu] << " event: " << queue->entry[index].event_cycle << endl; });
    }

    #endif

    #ifdef FRFCFS

    uint64_t read_addr;
    uint32_t read_channel, read_rank, read_bank, read_row;
    uint8_t  row_buffer_hit = 0;

    int oldest_index = -1;
    uint64_t oldest_cycle = UINT64_MAX;

    // search for the oldest open row hit 
    for (uint32_t i=0; i<queue->SIZE; i++) {

        // already scheduled
        if (queue->entry[i].scheduled) 
            continue;

        // empty entry
        read_addr = queue->entry[i].address;
        if (read_addr == 0) 
            continue;

        read_channel = dram_get_channel(read_addr);
        read_rank = dram_get_rank(read_addr);
        read_bank = dram_get_bank(read_addr);

        // bank is busy
        if (bank_request[read_channel][read_rank][read_bank].working) { // should we check this or not? how do we know if bank is busy or not for all requests in the queue?

            //DP ( if (warmup_complete[0]) {
            //cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " bank is busy";
            //cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
            //cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
            //cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });

            continue;
        }

        read_row = dram_get_row(read_addr);
        //read_column = dram_get_column(read_addr);

        // check open row
        if (bank_request[read_channel][read_rank][read_bank].open_row != read_row) {

            /*
            DP ( if (warmup_complete[0]) {
            cout << queue->NAME << " " << __func__ << " instr_id: " << queue->entry[i].instr_id << " row is inactive";
            cout << " swrites: " << scheduled_writes[channel] << " sreads: " << scheduled_reads[channel];
            cout << " write: " << +bank_request[read_channel][read_rank][read_bank].is_write << " read: " << +bank_request[read_channel][read_rank][read_bank].is_read << hex;
            cout << " address: " << queue->entry[i].address << dec << " channel: " << read_channel << " rank: " << read_rank << " bank: " << read_bank << endl; });
            */

            continue;
        }

        // select the oldest entry
        if (queue->entry[i].event_cycle < oldest_cycle) {
            oldest_cycle = queue->entry[i].event_cycle;
            oldest_index = i;
            row_buffer_hit = 1;
        }	  
    }


    if (oldest_index == -1) { // no matching open_row (row buffer miss)

        oldest_cycle = UINT64_MAX;
        for (uint32_t i=0; i<queue->SIZE; i++) {

            // already scheduled
            if (queue->entry[i].scheduled)
                continue;

            // empty entry
            read_addr = queue->entry[i].address;
            if (read_addr == 0) 
                continue;

            // bank is busy
            read_channel = dram_get_channel(read_addr);
            read_rank = dram_get_rank(read_addr);
            read_bank = dram_get_bank(read_addr);
            if (bank_request[read_channel][read_rank][read_bank].working) 
                continue;

            //read_row = dram_get_row(read_addr);
            //read_column = dram_get_column(read_addr);

            // select the oldest entry
            if (queue->entry[i].event_cycle < oldest_cycle) {
                oldest_cycle = queue->entry[i].event_cycle;
                oldest_index = i;
            }
        }
    }

    // at this point, the scheduler knows which bank to access and if the request is a row buffer hit or miss
    if (oldest_index != -1) { // scheduler might not find anything if all requests are already scheduled or all banks are busy

        uint64_t LATENCY = 0;
        if (row_buffer_hit)  
            LATENCY = tCAS;
        else 
            LATENCY = tRP + tRCD + tCAS;

        uint64_t op_addr = queue->entry[oldest_index].address;
        uint32_t op_cpu = queue->entry[oldest_index].cpu,
                 op_channel = dram_get_channel(op_addr), 
                 op_rank = dram_get_rank(op_addr), 
                 op_bank = dram_get_bank(op_addr), 
                 op_row = dram_get_row(op_addr);
        #ifdef DEBUG_PRINT
                uint32_t op_column = dram_get_column(op_addr);
        #endif

        // this bank is now busy
        bank_request[op_channel][op_rank][op_bank].working = 1;
        bank_request[op_channel][op_rank][op_bank].working_type = queue->entry[oldest_index].type;
        bank_request[op_channel][op_rank][op_bank].cycle_available = current_core_cycle[op_cpu] + LATENCY;

        bank_request[op_channel][op_rank][op_bank].request_index = oldest_index;
        bank_request[op_channel][op_rank][op_bank].row_buffer_hit = row_buffer_hit;
        if (queue->is_WQ) {
            bank_request[op_channel][op_rank][op_bank].is_write = 1;
            bank_request[op_channel][op_rank][op_bank].is_read = 0;
            scheduled_writes[op_channel]++;
        }
        else {
            bank_request[op_channel][op_rank][op_bank].is_write = 0;
            bank_request[op_channel][op_rank][op_bank].is_read = 1;
            scheduled_reads[op_channel]++;
        }

        // update open row
        bank_request[op_channel][op_rank][op_bank].open_row = op_row;

        queue->entry[oldest_index].scheduled = 1;
        queue->entry[oldest_index].event_cycle = current_core_cycle[op_cpu] + LATENCY;

        update_schedule_cycle(queue);
        update_process_cycle(queue);

        DP (if (warmup_complete[op_cpu]) {
        cout << "[" << queue->NAME << "] " <<  __func__ << " instr_id: " << queue->entry[oldest_index].instr_id;
        cout << " row buffer: " << (row_buffer_hit ? (int)bank_request[op_channel][op_rank][op_bank].open_row : -1) << hex;
        cout << " address: " << queue->entry[oldest_index].address << " full_addr: " << queue->entry[oldest_index].full_addr << dec;
        cout << " index: " << oldest_index << " occupancy: " << queue->occupancy;
        cout << " ch: " << op_channel << " rank: " << op_rank << " bank: " << op_bank; // wrong from here
        cout << " row: " << op_row << " col: " << op_column;
        cout << " current: " << current_core_cycle[op_cpu] << " event: " << queue->entry[oldest_index].event_cycle << endl; });
    }

    #endif
}

void MEMORY_CONTROLLER::process(PACKET_QUEUE *queue)
{
    uint32_t request_index = queue->next_process_index;
    
    // sanity check
    if (request_index == queue->SIZE)
        assert(0);

    uint8_t  op_type = queue->entry[request_index].type;
    uint64_t op_addr = queue->entry[request_index].address;
    uint32_t op_cpu = queue->entry[request_index].cpu,
             op_channel = dram_get_channel(op_addr), 
             op_rank = dram_get_rank(op_addr), 
             op_bank = dram_get_bank(op_addr);
#ifdef DEBUG_PRINT
    uint32_t op_row = dram_get_row(op_addr), 
             op_column = dram_get_column(op_addr);
#endif

    // sanity check
    if (bank_request[op_channel][op_rank][op_bank].request_index != (int)request_index) {
        assert(0);
    }

    // paid all DRAM access latency, data is ready to be processed
    if (bank_request[op_channel][op_rank][op_bank].cycle_available <= current_core_cycle[op_cpu]) {

        // check if data bus is available
        if (dbus_cycle_available[op_channel] <= current_core_cycle[op_cpu]) {

            if (queue->is_WQ) {
                // update data bus cycle time
                dbus_cycle_available[op_channel] = current_core_cycle[op_cpu] + DRAM_DBUS_RETURN_TIME;

                if (bank_request[op_channel][op_rank][op_bank].row_buffer_hit)
                    queue->ROW_BUFFER_HIT++;
                else
                    queue->ROW_BUFFER_MISS++;

                // this bank is ready for another DRAM request
                bank_request[op_channel][op_rank][op_bank].request_index = -1;
                bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
                bank_request[op_channel][op_rank][op_bank].working = false;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;

                scheduled_writes[op_channel]--;
            } else {
                // update data bus cycle time
                dbus_cycle_available[op_channel] = current_core_cycle[op_cpu] + DRAM_DBUS_RETURN_TIME;
                queue->entry[request_index].event_cycle = dbus_cycle_available[op_channel]; 

                DP ( if (warmup_complete[op_cpu]) {
                cout << "[" << queue->NAME << "] " <<  __func__ << " return data" << hex;
                cout << " address: " << queue->entry[request_index].address << " full_addr: " << queue->entry[request_index].full_addr << dec;
                cout << " occupancy: " << queue->occupancy << " channel: " << op_channel << " rank: " << op_rank << " bank: " << op_bank;
                cout << " row: " << op_row << " column: " << op_column;
                cout << " current_cycle: " << current_core_cycle[op_cpu] << " event_cycle: " << queue->entry[request_index].event_cycle << endl; });

                // send data back to the core cache hierarchy
                upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);

                if (bank_request[op_channel][op_rank][op_bank].row_buffer_hit)
                    queue->ROW_BUFFER_HIT++;
                else
                    queue->ROW_BUFFER_MISS++;

                // this bank is ready for another DRAM request
                bank_request[op_channel][op_rank][op_bank].request_index = -1;
                bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
                bank_request[op_channel][op_rank][op_bank].working = false;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;

                scheduled_reads[op_channel]--;
            }



            #ifdef BLISS

            //the request that was scheduled belongs to the same ASID as the last scheduled request
            uint16_t CPU_ID = queue->entry[request_index].cpu;
            if(CPU_ID == last_scheduled_request){
                BLACKLIST_COUNTER[CPU_ID] += 1;
                if(BLACKLIST_COUNTER[CPU_ID] > blacklisting_threshold){
                    BLACKLIST[CPU_ID] = true;
                    BLACKLIST_COUNTER[CPU_ID] = 0;
                }
            }else{
                BLACKLIST_COUNTER[CPU_ID] = 0;
            }
            //change the last scheduled request match the ASID of the request that was just scheduled
            last_scheduled_request = CPU_ID;

            #endif

            #ifdef ATLAS

            AS[queue->entry[request_index].cpu]++;
            //printf("AS[%d] = %d\n", queue->entry[request_index].cpu, AS[queue->entry[request_index].cpu]);
            
            #endif


            // remove the oldest entry
            queue->remove_queue(&queue->entry[request_index]);
            update_process_cycle(queue);
        }
        else { // data bus is busy, the available bank cycle time is fast-forwarded for faster simulation

#if 0
            // TODO: what if we can service prefetching request without dbus congestion?
            // can we have more timely prefetches and improve performance?
            if ((op_type == PREFETCH) || (op_type == LOAD)) {
                // just magically return prefetch request (no need to update data bus cycle time)
                /*
                dbus_cycle_available[op_channel] = current_core_cycle[op_cpu] + DRAM_DBUS_RETURN_TIME;
                queue->entry[request_index].event_cycle = dbus_cycle_available[op_channel]; 

                DP ( if (warmup_complete[op_cpu]) {
                cout << "[" << queue->NAME << "] " <<  __func__ << " return data" << hex;
                cout << " address: " << queue->entry[request_index].address << " full_addr: " << queue->entry[request_index].full_addr << dec;
                cout << " occupancy: " << queue->occupancy << " channel: " << op_channel << " rank: " << op_rank << " bank: " << op_bank;
                cout << " row: " << op_row << " column: " << op_column;
                cout << " current_cycle: " << current_core_cycle[op_cpu] << " event_cycle: " << queue->entry[request_index].event_cycle << endl; });
                */

                // send data back to the core cache hierarchy
                upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);

                if (bank_request[op_channel][op_rank][op_bank].row_buffer_hit)
                    queue->ROW_BUFFER_HIT++;
                else
                    queue->ROW_BUFFER_MISS++;

                // this bank is ready for another DRAM request
                bank_request[op_channel][op_rank][op_bank].request_index = -1;
                bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
                bank_request[op_channel][op_rank][op_bank].working = false;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;

                scheduled_reads[op_channel]--;

                // remove the oldest entry
                queue->remove_queue(&queue->entry[request_index]);
                update_process_cycle(queue);

                return;
            }
#endif

            dbus_cycle_congested[op_channel] += (dbus_cycle_available[op_channel] - current_core_cycle[op_cpu]);
            bank_request[op_channel][op_rank][op_bank].cycle_available = dbus_cycle_available[op_channel];
            dbus_congested[NUM_TYPES][NUM_TYPES]++;
            dbus_congested[NUM_TYPES][op_type]++;
            dbus_congested[bank_request[op_channel][op_rank][op_bank].working_type][NUM_TYPES]++;
            dbus_congested[bank_request[op_channel][op_rank][op_bank].working_type][op_type]++;

            DP ( if (warmup_complete[op_cpu]) {
            cout << "[" << queue->NAME << "] " <<  __func__ << " dbus_occupied" << hex;
            cout << " address: " << queue->entry[request_index].address << " full_addr: " << queue->entry[request_index].full_addr << dec;
            cout << " occupancy: " << queue->occupancy << " channel: " << op_channel << " rank: " << op_rank << " bank: " << op_bank;
            cout << " row: " << op_row << " column: " << op_column;
            cout << " current_cycle: " << current_core_cycle[op_cpu] << " event_cycle: " << bank_request[op_channel][op_rank][op_bank].cycle_available << endl; });
        }
    }
}

int MEMORY_CONTROLLER::add_rq(PACKET *packet)
{
    // simply return read requests with dummy response before the warmup
    if (all_warmup_complete < NUM_CPUS) {
        if (packet->instruction) 
            upper_level_icache[packet->cpu]->return_data(packet);
        else // data
            upper_level_dcache[packet->cpu]->return_data(packet);

        return -1;
    }

    // check for the latest writebacks in the write queue
    uint32_t channel = dram_get_channel(packet->address);
    int wq_index = check_dram_queue(&WQ[channel], packet);
    if (wq_index != -1) {
        
        // no need to check fill level
        //if (packet->fill_level < fill_level) {

            packet->data = WQ[channel].entry[wq_index].data;
            if (packet->instruction) 
                upper_level_icache[packet->cpu]->return_data(packet);
            else // data
                upper_level_dcache[packet->cpu]->return_data(packet);
        //}

        DP ( if (packet->cpu) {
        cout << "[" << NAME << "_RQ] " << __func__ << " instr_id: " << packet->instr_id << " found recent writebacks";
        cout << hex << " read: " << packet->address << " writeback: " << WQ[channel].entry[wq_index].address << dec << endl; });

        ACCESS[1]++;
        HIT[1]++;

        WQ[channel].FORWARD++;
        RQ[channel].ACCESS++;
        //assert(0);

        return -1;
    }

    // check for duplicates in the read queue
    int index = check_dram_queue(&RQ[channel], packet);
    if (index != -1)
        return index; // merged index

    // search for the empty index
    for (index=0; index<DRAM_RQ_SIZE; index++) {
        if (RQ[channel].entry[index].address == 0) {
            
            RQ[channel].entry[index] = *packet;
            RQ[channel].occupancy++;

#ifdef DEBUG_PRINT
            uint32_t channel = dram_get_channel(packet->address),
                     rank = dram_get_rank(packet->address),
                     bank = dram_get_bank(packet->address),
                     row = dram_get_row(packet->address),
                     column = dram_get_column(packet->address); 
#endif

            DP ( if(warmup_complete[packet->cpu]) {
            cout << "[" << NAME << "_RQ] " <<  __func__ << " instr_id: " << packet->instr_id << " address: " << hex << packet->address;
            cout << " full_addr: " << packet->full_addr << dec << " ch: " << channel;
            cout << " rank: " << rank << " bank: " << bank << " row: " << row << " col: " << column;
            cout << " occupancy: " << RQ[channel].occupancy << " current: " << current_core_cycle[packet->cpu] << " event: " << packet->event_cycle << endl; });

            break;
        }
    }

    update_schedule_cycle(&RQ[channel]);

    return -1;
}

int MEMORY_CONTROLLER::add_wq(PACKET *packet)
{
    // simply drop write requests before the warmup
    if (all_warmup_complete < NUM_CPUS)
        return -1;

    // check for duplicates in the write queue
    uint32_t channel = dram_get_channel(packet->address);
    int index = check_dram_queue(&WQ[channel], packet);
    if (index != -1)
        return index; // merged index

    // search for the empty index
    for (index=0; index<DRAM_WQ_SIZE; index++) {
        if (WQ[channel].entry[index].address == 0) {
            
            WQ[channel].entry[index] = *packet;
            WQ[channel].occupancy++;

#ifdef DEBUG_PRINT
            uint32_t channel = dram_get_channel(packet->address),
                     rank = dram_get_rank(packet->address),
                     bank = dram_get_bank(packet->address),
                     row = dram_get_row(packet->address),
                     column = dram_get_column(packet->address); 
#endif

            DP ( if(warmup_complete[packet->cpu]) {
            cout << "[" << NAME << "_WQ] " <<  __func__ << " instr_id: " << packet->instr_id << " address: " << hex << packet->address;
            cout << " full_addr: " << packet->full_addr << dec << " ch: " << channel;
            cout << " rank: " << rank << " bank: " << bank << " row: " << row << " col: " << column;
            cout << " occupancy: " << WQ[channel].occupancy << " current: " << current_core_cycle[packet->cpu] << " event: " << packet->event_cycle << endl; });

            break;
        }
    }

    update_schedule_cycle(&WQ[channel]);

    return -1;
}

int MEMORY_CONTROLLER::add_pq(PACKET *packet)
{
    return -1;
}

void MEMORY_CONTROLLER::return_data(PACKET *packet)
{

}

void MEMORY_CONTROLLER::update_schedule_cycle(PACKET_QUEUE *queue)
{
    // update next_schedule_cycle
    uint64_t min_cycle = UINT64_MAX;
    uint32_t min_index = queue->SIZE;
    for (uint32_t i=0; i<queue->SIZE; i++) {
        /*
        DP (if (warmup_complete[queue->entry[min_index].cpu]) {
        cout << "[" << queue->NAME << "] " <<  __func__ << " instr_id: " << queue->entry[i].instr_id;
        cout << " index: " << i << " address: " << hex << queue->entry[i].address << dec << " scheduled: " << +queue->entry[i].scheduled;
        cout << " event: " << queue->entry[i].event_cycle << " min_cycle: " << min_cycle << endl;
        });
        */

        if (queue->entry[i].address && (queue->entry[i].scheduled == 0) && (queue->entry[i].event_cycle < min_cycle)) {
            min_cycle = queue->entry[i].event_cycle;
            min_index = i;
        }
    }
    
    queue->next_schedule_cycle = min_cycle;
    queue->next_schedule_index = min_index;
    if (min_index < queue->SIZE) {

        DP (if (warmup_complete[queue->entry[min_index].cpu]) {
        cout << "[" << queue->NAME << "] " <<  __func__ << " instr_id: " << queue->entry[min_index].instr_id;
        cout << " address: " << hex << queue->entry[min_index].address << " full_addr: " << queue->entry[min_index].full_addr;
        cout << " data: " << queue->entry[min_index].data << dec;
        cout << " event: " << queue->entry[min_index].event_cycle << " current: " << current_core_cycle[queue->entry[min_index].cpu] << " next: " << queue->next_schedule_cycle << endl; });
    }
}

void MEMORY_CONTROLLER::update_process_cycle(PACKET_QUEUE *queue)
{
    // update next_process_cycle
    uint64_t min_cycle = UINT64_MAX;
    uint32_t min_index = queue->SIZE;
    for (uint32_t i=0; i<queue->SIZE; i++) {
        if (queue->entry[i].scheduled && (queue->entry[i].event_cycle < min_cycle)) {
            min_cycle = queue->entry[i].event_cycle;
            min_index = i;
        }
    }
    
    queue->next_process_cycle = min_cycle;
    queue->next_process_index = min_index;
    if (min_index < queue->SIZE) {

        DP (if (warmup_complete[queue->entry[min_index].cpu]) {
        cout << "[" << queue->NAME << "] " <<  __func__ << " instr_id: " << queue->entry[min_index].instr_id;
        cout << " address: " << hex << queue->entry[min_index].address << " full_addr: " << queue->entry[min_index].full_addr;
        cout << " data: " << queue->entry[min_index].data << dec << " num_returned: " << queue->num_returned;
        cout << " event: " << queue->entry[min_index].event_cycle << " current: " << current_core_cycle[queue->entry[min_index].cpu] << " next: " << queue->next_process_cycle << endl; });
    }
}

int MEMORY_CONTROLLER::check_dram_queue(PACKET_QUEUE *queue, PACKET *packet)
{
    // search write queue
    for (uint32_t index=0; index<queue->SIZE; index++) {
        if (queue->entry[index].address == packet->address) {
            
            DP ( if (warmup_complete[packet->cpu]) {
            cout << "[" << queue->NAME << "] " << __func__ << " same entry instr_id: " << packet->instr_id << " prior_id: " << queue->entry[index].instr_id;
            cout << " address: " << hex << packet->address << " full_addr: " << packet->full_addr << dec << endl; });

            return index;
        }
    }

    DP ( if (warmup_complete[packet->cpu]) {
    cout << "[" << queue->NAME << "] " << __func__ << " new address: " << hex << packet->address;
    cout << " full_addr: " << packet->full_addr << dec << endl; });

    DP ( if (warmup_complete[packet->cpu] && (queue->occupancy == queue->SIZE)) {
    cout << "[" << queue->NAME << "] " << __func__ << " mshr is full";
    cout << " instr_id: " << packet->instr_id << " mshr occupancy: " << queue->occupancy;
    cout << " address: " << hex << packet->address;
    cout << " full_addr: " << packet->full_addr << dec;
    cout << " cycle: " << current_core_cycle[packet->cpu] << endl; });

    return -1;
}

uint32_t MEMORY_CONTROLLER::dram_get_channel(uint64_t address)
{
    if (LOG2_DRAM_CHANNELS == 0)
        return 0;

    int shift = 0;

    return (uint32_t) (address >> shift) & (DRAM_CHANNELS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_bank(uint64_t address)
{
    if (LOG2_DRAM_BANKS == 0)
        return 0;

    int shift = LOG2_DRAM_CHANNELS;

    return (uint32_t) (address >> shift) & (DRAM_BANKS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_column(uint64_t address)
{
    if (LOG2_DRAM_COLUMNS == 0)
        return 0;

    int shift = LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS;

    return (uint32_t) (address >> shift) & (DRAM_COLUMNS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_rank(uint64_t address)
{
    if (LOG2_DRAM_RANKS == 0)
        return 0;

    int shift = LOG2_DRAM_COLUMNS + LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS;

    return (uint32_t) (address >> shift) & (DRAM_RANKS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_row(uint64_t address)
{
    if (LOG2_DRAM_ROWS == 0)
        return 0;

    int shift = LOG2_DRAM_RANKS + LOG2_DRAM_COLUMNS + LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS;

    return (uint32_t) (address >> shift) & (DRAM_ROWS - 1);
}

uint32_t MEMORY_CONTROLLER::get_occupancy(uint8_t queue_type, uint64_t address)
{
    uint32_t channel = dram_get_channel(address);
    if (queue_type == 1)
        return RQ[channel].occupancy;
    else if (queue_type == 2)
        return WQ[channel].occupancy;

    return 0;
}

uint32_t MEMORY_CONTROLLER::get_size(uint8_t queue_type, uint64_t address)
{
    uint32_t channel = dram_get_channel(address);
    if (queue_type == 1)
        return RQ[channel].SIZE;
    else if (queue_type == 2)
        return WQ[channel].SIZE;

    return 0;
}

void MEMORY_CONTROLLER::increment_WQ_FULL(uint64_t address)
{
    uint32_t channel = dram_get_channel(address);
    WQ[channel].FULL++;
}

#ifdef BLISS

void MEMORY_CONTROLLER::clear_blacklist(uint32_t cpu){
    for (uint32_t i=0; i<DRAM_CHANNELS; i++){
        for(uint32_t j=0; j<DRAM_WQ_SIZE; j++){
            if(WQ[i].entry[j].cpu == cpu){
                uint16_t ASID = ((uint16_t)WQ[i].entry[j].asid[0]) | WQ[i].entry[j].asid[1];
                BLACKLIST[ASID] = false;
                BLACKLIST_COUNTER[ASID] = 0;
            }
        }
        for(uint32_t j=0; j<DRAM_RQ_SIZE; j++){
            if(WQ[i].entry[j].cpu == cpu){
                uint16_t ASID = ((uint16_t)WQ[i].entry[j].asid[0]) | WQ[i].entry[j].asid[1];
                BLACKLIST[ASID] = false;
                BLACKLIST_COUNTER[ASID] = 0;
            }
        }
    }
}

#endif

#ifdef ATLAS

void MEMORY_CONTROLLER::calculateLAS(){
    std::vector<std::pair<unsigned int, unsigned int>> total_AS;
    for(const auto& x : AS){
        TOTAL_AS[x.first] = (alpha * TOTAL_AS[x.first]) + ((1-alpha) * x.second);
        AS[x.first] = 0;
        total_AS.push_back(std::make_pair(x.first, TOTAL_AS[x.first]));
    }
    std::sort(total_AS.begin(), total_AS.end(), 
        [](const std::pair<unsigned int, unsigned int> &left, const std::pair<unsigned int, unsigned int> &right) {
        return left.second < right.second;
    });

    int rank=0;
    for(const auto& x : total_AS){
        ATLAS_RANK[x.first] = rank;
        rank++;
    }
}

#endif