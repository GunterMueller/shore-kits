/* -*- mode:C++; c-basic-offset:4 -*- */

#ifndef _FSCAN_H
#define _FSCAN_H

#include <cstdio>

#include "engine/core/tuple.h"
#include "engine/core/packet.h"
#include "engine/core/stage.h"
#include "engine/dispatcher.h"
#include "trace.h"
#include "qpipe_panic.h"



using namespace qpipe;



/* exported datatypes */


class fscan_packet_t : public packet_t {
  
public:

    static const c_str PACKET_TYPE;
    

    c_str _filename;


    /**
     *  @brief fscan_packet_t constructor.
     *
     *  @param packet_id The ID of this packet. This should point to a
     *  block of bytes allocated with malloc(). This packet will take
     *  ownership of this block and invoke free() when it is
     *  destroyed.
     *
     *  @param output_buffer The buffer where this packet should send
     *  its data. A packet DOES NOT own its output buffer (we will not
     *  invoke delete or free() on this field in our packet
     *  destructor).
     *
     *  @param output_filter The filter that will be applied to any
     *  tuple sent to output_buffer. The packet OWNS this filter. It
     *  will be deleted in the packet destructor.
     *
     *  @param filename The name of the file to scan. This should be a
     *  block of bytes allocated with malloc(). This packet will take
     *  ownership of this block and invoke free() when it is
     *  destroyed.
     *
     *  Raw FSCANS should not be mergeable based on not
     *  mergeable. They should be merged at the meta-stage.
     */
    fscan_packet_t(const c_str    &packet_id,
		   tuple_fifo* output_buffer,
		   tuple_filter_t* output_filter,
		   const c_str    &filename)
	: packet_t(packet_id, PACKET_TYPE, output_buffer, output_filter,
                   create_plan(output_filter, filename)),
          _filename(filename)
    {
    }

    static query_plan* create_plan(tuple_filter_t* filter, const c_str &file_name) {
        c_str action("%s:%s", PACKET_TYPE.data(), file_name.data());
        return new query_plan(action, filter->to_string(), NULL, 0);
    }
};



/**
 *  @brief FSCAN stage. Reads tuples from a file on the local file
 *  system.
 */
class fscan_stage_t : public stage_t {

public:

    static const c_str DEFAULT_STAGE_NAME;
    typedef fscan_packet_t stage_packet_t;

    fscan_stage_t() { }
    
    virtual ~fscan_stage_t() { }
    
protected:

    virtual void process_packet();
    
    void read_file(adaptor_t* adaptor, FILE* file, tuple_page_t* tuple_page);
};



#endif
