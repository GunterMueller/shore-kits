/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file tpcc_env.c
 *
 *  @brief Useful data structures for the TPC-C database
 *
 *  @author Ippokratis Pandis (ipandis)
 */

#include "workload/tpcc/tpcc_env.h"
#include "workload/tpcc/tpcc_filenames.h"
#include "workload/tpcc/tpcc_compare.h"
#include "workload/tpcc/tpcc_tbl_parsers.h"


using namespace tpcc;


// BDB indexes

// (ip) Should decide which indexes to create

/*
Db* tpch::tpch_lineitem_shipdate = NULL;
Db* tpch::tpch_lineitem_shipdate_idx = NULL;
*/

/* exported data structures */

#define TABLE(name) { TBL_FILENAME_##name, \
                      BDB_FILENAME_##name, \
                      TABLE_ID_##name,     \
                      NULL, \
                      tpcc_bt_compare_fn_##name, \
                      tpch_parse_tbl_##name }

bdb_table_s tpcc::tpch_tables[_TPCH_TABLE_COUNT_] = {
    TABLE(CUSTOMER),
    TABLE(DISTRICT),
    TABLE(HISTORY),
    TABLE(ITEM),
    TABLE(NEW_ORDER),
    TABLE(ORDER),
    TABLE(ORDERLINE),
    TABLE(STOCK),
    TABLE(WAREHOUSE)
};