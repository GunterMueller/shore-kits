/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file:   dora_tpcb_xct.cpp
 *
 *  @brief:  Declaration of the DORA TPCB transactions
 *
 *  @author: Ippokratis Pandis (ipandis)
 *  @date:   July 2009
 */

#include "dora/tpcb/dora_tpcb_impl.h"
#include "dora/tpcb/dora_tpcb.h"

using namespace shore;
using namespace tpcb;


ENTER_NAMESPACE(dora);


typedef range_partition_impl<int>   irpImpl; 


/******** Exported functions  ********/


/********
 ******** Caution: The functions below should be invoked inside
 ********          the context of a smthread
 ********/


/******************************************************************** 
 *
 * TPCB DORA TRXS
 *
 * (1) The dora_XXX functions are wrappers to the real transactions
 * (2) The xct_dora_XXX functions are the implementation of the transactions
 *
 ********************************************************************/


/******************************************************************** 
 *
 * TPCB DORA TRXs Wrappers
 *
 * @brief: They are wrappers to the functions that execute the transaction
 *         body. Their responsibility is to:
 *
 *         1. Prepare the corresponding input
 *         2. Check the return of the trx function and abort the trx,
 *            if something went wrong
 *         3. Update the tpcb db environment statistics
 *
 ********************************************************************/


// --- without input specified --- //

DEFINE_DORA_WITHOUT_INPUT_TRX_WRAPPER(DoraTPCBEnv,acct_update);



// --- with input specified --- //

/******************************************************************** 
 *
 * DORA TPCB ACCT_UPDATE
 *
 ********************************************************************/

w_rc_t 
DoraTPCBEnv::dora_acct_update(const int xct_id, 
                              trx_result_tuple_t& atrt, 
                              acct_update_input_t& in,
                              const bool bWake)
{
    // 1. Initiate transaction
    tid_t atid;   

#ifndef ONLYDORA
    W_DO(_pssm->begin_xct(atid));
#endif
    TRACE( TRACE_TRX_FLOW, "Begin (%d)\n", atid);

    xct_t* pxct = smthread_t::me()->xct();

    // 2. Detatch self from xct
#ifndef ONLYDORA
    assert (pxct);
    smthread_t::me()->detach_xct(pxct);
#endif
    TRACE( TRACE_TRX_FLOW, "Detached from (%d)\n", atid);

    // 3. Setup the final RVP
    final_au_rvp* frvp = new_final_au_rvp(atid,pxct,xct_id,atrt);    

    // 4. Generate the actions
    upd_br_action* upd_br = new_upd_br_action(atid,pxct,frvp,in);
    upd_te_action* upd_te = new_upd_te_action(atid,pxct,frvp,in);
    upd_ac_action* upd_ac = new_upd_ac_action(atid,pxct,frvp,in);
    ins_hi_action* ins_hi = new_ins_hi_action(atid,pxct,frvp,in);

    // 5a. Decide about partition
    // 5b. Enqueue

    {        
        irpImpl* my_br_part = decide_part(br(),(in.b_id-1));
        assert (my_br_part);
        irpImpl* my_te_part = decide_part(te(),((in.t_id)/TPCB_TELLERS_PER_BRANCH - 1));
        assert (my_te_part);
        irpImpl* my_ac_part = decide_part(ac(),((in.a_id)/TPCB_ACCOUNTS_PER_BRANCH - 1));
        assert (my_ac_part);
        irpImpl* my_hi_part = decide_part(hi(),((in.a_id)/TPCB_ACCOUNTS_PER_BRANCH - 1));
        assert (my_hi_part);

        // BR_PART_CS
        CRITICAL_SECTION(br_part_cs, my_br_part->_enqueue_lock);
        if (my_br_part->enqueue(upd_br,bWake)) {
            TRACE( TRACE_DEBUG, "Problem in enqueueing UPD_BR\n");
            assert (0); 
            return (RC(de_PROBLEM_ENQUEUE));
        }

        // TE_PART_CS
        CRITICAL_SECTION(te_part_cs, my_te_part->_enqueue_lock);
        te_part_cs.exit();
        if (my_te_part->enqueue(upd_te,bWake)) {
            TRACE( TRACE_DEBUG, "Problem in enqueueing UPD_TE\n");
            assert (0); 
            return (RC(de_PROBLEM_ENQUEUE));
        }

        // AC_PART_CS
        CRITICAL_SECTION(ac_part_cs, my_ac_part->_enqueue_lock);
        ac_part_cs.exit();
        if (my_ac_part->enqueue(upd_ac,bWake)) {
            TRACE( TRACE_DEBUG, "Problem in enqueueing UPD_AC\n");
            assert (0); 
            return (RC(de_PROBLEM_ENQUEUE));
        }

        // HI_PART_CS
        CRITICAL_SECTION(hi_part_cs, my_hi_part->_enqueue_lock);
        hi_part_cs.exit();
        if (my_hi_part->enqueue(ins_hi,bWake)) {
            TRACE( TRACE_DEBUG, "Problem in enqueueing INS_HI\n");
            assert (0); 
            return (RC(de_PROBLEM_ENQUEUE));
        }
    }

    return (RCOK); 
}


EXIT_NAMESPACE(dora);