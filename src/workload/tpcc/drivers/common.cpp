/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file workload/tpcc/drivers/common.cpp
 *  
 *  @brief Functions used for the generation of all the tpcc transaction.
 *  @version Based on TPC-C Standard Specification Revision 5.4 (Apr 2005)
 */

#include "workload/tpcc/drivers/common.h"

ENTER_NAMESPACE(workload);


/** Terminology
 *  
 *  [x .. y]: Represents a closed range of values starting with x and ending 
 *            with y
 *
 *  random(x,y): uniformly distributed value between x and y.
 *
 *  NURand(A, x, y): (((random(0, A) | random(x, y)) + C) % (y - x + 1)) + x
 *                   non-uniform random, where
 *  exp-1 | exp-2: bitwise logical OR
 *  exp-1 % exp-2: exp-1 modulo exp-2
 *  C: random(0, A)
 */                  



/** @func random(int, int, randgen_t*)
 *
 *  @brief Generates a uniform random number between low and high. 
 *  Not seen by public.
 */

int random(int low, int high, randgen_t* rp) {

  return (low + rp->rand(high - low + 1));
}


/** @func URand(int, int)
 *
 *  @brief Generates a uniform random number between (low) and (high)
 */

int URand(int low, int high) {

  thread_t* self = thread_get_self();
  randgen_t* randgenp = self->randgen();

  return (low + randgenp->rand(high - low + 1));
}


/** FIXME: (ip) C has a constant value across ($2.1.6.1) */

/** @func NURand(int, int, int)
 *
 *  @brief Generates a non-uniform random number
 */

int NURand(int A, int low, int high) {

  thread_t* self = thread_get_self();
  randgen_t* randgenp = self->randgen();

  return ( (((random(0, A, randgenp) | random(low, high, randgenp)) 
             + random(0, A, randgenp)) 
            % (high - low + 1)) + low );
}

/** @func generate_cust_last(int)
 *
 *  @brief Generates a customer last name (C_LAST) according to clause 4.3.2.3 of the
 *  TPCC specification. 
 *  The C_LAST must be generated by the concatenation of three variable length syllabes
 *  selected from the following list:
 *  0 BAR
 *  1 OUGHT
 *  2 ABLE
 *  3 PRI
 *  4 PRES
 *  5 ESE
 *  6 ANTI
 *  7 CALLY
 *  8 ATION
 *  9 EING
 *
 *  Given a number between 0 and 999 each of the three syllables is determined by the 
 *  corresponding digit representation of the number. 
 *  For example, 
 *  The number: 371 generates the name: PRICALLYOUGHT
 *  The number: 40 generates the name: BARPRESBAR
 */

char* CUST_LAST[10] = { "BAR", "OUGHT", "ABLE", "PRI", "PRES", "ESE",
                        "ANTI", "CALLY", "ATION", "EING" };


char* generate_cust_last(int select) {
  
  assert((select >= 0) && (select < 1000));

  int i1, i2, i3;

  i3 = select % 10;
  i2 = ((select % 100) - i3) / 10;
  i1 = (select - (select % 100)) / 100;

  /** FIXME: (ip) C_LAST is char[16] */

  char* s_cust_last = new char[strlen(CUST_LAST[i1]) +
                               strlen(CUST_LAST[i2]) +
                               strlen(CUST_LAST[i3]) + 1];
  

  sprintf(s_cust_last, "%s%s%s\0", 
          CUST_LAST[i1],
          CUST_LAST[i2],
          CUST_LAST[i3]);

  //TRACE(TRACE_ALWAYS, "SEL: [%d]\tC_LAST: [%s]\n", select, s_cust_last);
  
  return (s_cust_last);
}


EXIT_NAMESPACE(workload);
