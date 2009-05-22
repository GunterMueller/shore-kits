/* -*- mode:C++; c-basic-offset:4 -*-
     Shore-MT -- Multi-threaded port of the SHORE storage manager
   
                       Copyright (c) 2007-2009
      Data Intensive Applications and Systems Labaratory (DIAS)
               Ecole Polytechnique Federale de Lausanne
   
                         All Rights Reserved.
   
   Permission to use, copy, modify and distribute this software and
   its documentation is hereby granted, provided that both the
   copyright notice and this permission notice appear in all copies of
   the software, derivative works or modified versions, and any
   portions thereof, and that both notices appear in supporting
   documentation.
   
   This code is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. THE AUTHORS
   DISCLAIM ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
   RESULTING FROM THE USE OF THIS SOFTWARE.
*/

/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file shore_helper_loader.cpp
 *
 *  @brief Declaration of helper loader thread classes
 *
 *  @author Ippokratis Pandis (ipandis)
 */

#include "sm/shore/shore_helper_loader.h"
#include "sm/shore/shore_env.h"


using namespace shore;


/** Exported functions */


void db_init_smt_t::work()
{
    if (!_env->is_initialized()) {
        if (_rv = _env->init()) {
            // Couldn't initialize the Shore environment
            // cannot proceed
            TRACE( TRACE_ALWAYS, "Couldn't initialize Shore...\n");
            return;
        }
    }

    // if reached this point everything went ok
    TRACE( TRACE_DEBUG, "Shore initialized...\n");
    _rv = 0;
}


void db_log_smt_t::work() 
{
    assert (_env);
    _env->db()->flushlog();
    _rv = 0;
}


void db_load_smt_t::work() 
{
    _rc = _env->loaddata();
    _rv = 0;
}



void close_smt_t::work() 
{
    assert (_env);
    TRACE( TRACE_ALWAYS, "Closing Shore...\n");
    if (_env) {
        _env->close();
        delete (_env);
        _env = NULL;
    }        
}




void dump_smt_t::work() 
{
    assert (_env);
    TRACE( TRACE_DEBUG, "Dumping...\n");
    _env->dump();
    _rv = 0;
}
