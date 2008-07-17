/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file:   countdown.cpp
 *
 *  @brief:  Implementation of atomic countdown class
 *
 *  @author: Ryan Johnson (ryanjohn)
 */


#include "util/countdown.h"


countdown_t::countdown_t(int count)
    : _state(NUMBER*count)
{
    assert (count>0);
}

int countdown_t::remaining() const 
{
    int old_value = *&_state;
    return ((old_value == ERROR) ? -1 : old_value/NUMBER); 
}


bool countdown_t::post(bool is_error) 
{
    int old_value = *&_state;
    while (old_value >= 2*NUMBER) {
        int new_value = (is_error? ERROR : old_value-1);
        int cur_value = atomic_cas_32(&_state, old_value, new_value);
        if (cur_value == old_value) {
            assert(is_error || new_value >= NUMBER);
            return (is_error);
        }

        // try, try again
        old_value = cur_value;
    }

    // did some other thread report an error?
    if(old_value ==  ERROR)
        return false;
 
    // we're the last caller -- no more atomic ops needed
    assert(old_value == NUMBER);
    _state = (is_error? ERROR : 0);
    return (true);
}
