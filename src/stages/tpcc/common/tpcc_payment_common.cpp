/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file tpcc_payment_common.cpp
 *
 *  @brief Generates a PAYMENT input
 *
 *  @author Ippokratis Pandis (ipandis)
 */

#include "util.h"
#include "stages/tpcc/common/tpcc_random.h" 
#include "stages/tpcc/common/tpcc_payment_common.h"


ENTER_NAMESPACE(tpcc);


/** @fn create_payment_input
 *
 *  @brief Creates the input for a new PAYMENT request, 
 *  given the scaling factor (sf) of the database
 */

payment_input_t create_payment_input(int sf) {

    // check scaling factor
    assert(sf > 0);

    // produce PAYMENT params according to tpcc spec v.5.4
    payment_input_t pin;

#ifndef USE_SAME_INPUT

    pin._home_wh_id = URand(1, sf);
    pin._home_d_id = URand(1, 10);
    pin._v_cust_wh_selection = URand(1, 100); // 85 - 15
    pin._remote_wh_id = URand(1, sf);
    pin._remote_d_id = URand(1, 10);
    pin._v_cust_ident_selection = URand(1, 100); // 60 - 40
    pin._c_id = NURand(1023, 1, 3000);
    
    // Calls the function that returns the correct cust_last
    generate_cust_last(NURand(255, 0, 999),pin._c_last);
    
    pin._h_amount = (long)URand(100, 500000)/(long)100.00;
    pin._h_date = time(NULL);

#else
    // Same input
    pin._home_wh_id = 1;
    pin._home_d_id =  1;
    pin._v_cust_wh_selection = 50;
    pin._remote_wh_id = 1;
    pin._remote_d_id =  1;
    pin._v_cust_ident_selection = 50;
    pin._c_id =  1500;        
    pin._c_last = NULL;
    pin._h_amount = 1000.00;
    pin._h_date = time(NULL);
#endif        

    return (pin);
}



EXIT_NAMESPACE(tpcc);