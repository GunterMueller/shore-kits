/* -*- mode:C++; c-basic-offset:4 -*- */

#include "tests/common/tpch_db.h"
#include "tests/common/tpch_tables.h"
#include "tests/common/tpch_compare.h"
#include "engine/bdb_config.h"
#include "qpipe_panic.h"
#include "trace.h"



static bool open_db_table(Db*& table, u_int32_t flags, int (*cmp) (Db*, const Dbt*, const Dbt*), const char* table_name);
static bool close_db_table(Db*& table, const char* table_name);



/**
 *  @brief : Opens the tpch tables
 *
 *  @return true on success. false on error.
 */
bool db_open(u_int32_t flags, u_int32_t db_cache_size_gb, u_int32_t db_cache_size_bytes) {

    // create environment
    try {
        dbenv = new DbEnv(0);
        dbenv->set_errpfx("qpipe");
    }
    catch ( DbException &e) {
        TRACE(TRACE_ALWAYS, "Caught DbException creating new DbEnv object: %s\n", e.what());
        return false;
    }
  
  
    // specify buffer pool size
    try {
        if (dbenv->set_cachesize(db_cache_size_gb, db_cache_size_bytes, 0)) {
            TRACE(TRACE_ALWAYS, "dbenv->set_cachesize() failed!\n");
            return false;
        }
    }
    catch ( DbException &e) {
        TRACE(TRACE_ALWAYS, "Caught DbException setting buffer pool size to %d GB, %d B: %s\n",
              db_cache_size_gb,
              db_cache_size_bytes,
              e.what());
        return false;
    }
  
  
    // set data directory
    try {
        // data directory stores table files
        dbenv->set_data_dir(BDB_DATA_DIRECTORY);
    }
    catch ( DbException &e) {
        TRACE(TRACE_ALWAYS, "Caught DbException setting data directory to \"%s\". Make sure directory exists\n",
              BDB_DATA_DIRECTORY);
        TRACE(TRACE_ALWAYS, "DbException: %s\n", e.what());
        return false;
    }
  
  
    // set temp directory
    try {
        dbenv->set_tmp_dir(BDB_TEMP_DIRECTORY);
    }
    catch ( DbException &e) {
        TRACE(TRACE_ALWAYS, "Caught DbException setting temp directory to \"%s\". Make sure directory exists\n",
              BDB_TEMP_DIRECTORY);
        TRACE(TRACE_ALWAYS, "DbException: %s\n", e.what());
        return false;
    }


    // open home directory
    try {
        // open environment with no transactional support.
        dbenv->open(BDB_HOME_DIRECTORY, DB_CREATE | DB_PRIVATE | DB_THREAD | DB_INIT_MPOOL, 0);
    }
    catch ( DbException &e) {
        TRACE(TRACE_ALWAYS, "Caught DbException opening home directory \"%s\". Make sure directory exists\n",
              BDB_HOME_DIRECTORY);
        TRACE(TRACE_ALWAYS, "DbException: %s\n", e.what());
        return false;
    }
  

    // open tables
    if ( !open_db_table(tpch_customer, flags, tpch_customer_bt_compare_fcn, TABLE_CUSTOMER_NAME) )
        return false;

    if ( !open_db_table(tpch_lineitem, flags, tpch_lineitem_bt_compare_fcn, TABLE_LINEITEM_NAME) )
        return false;
  
    if ( !open_db_table(tpch_nation, flags, tpch_nation_bt_compare_fcn, TABLE_NATION_NAME) )
        return false;

    if ( !open_db_table(tpch_orders, flags, tpch_orders_bt_compare_fcn, TABLE_ORDERS_NAME) )
        return false;

    if ( !open_db_table(tpch_part, flags, tpch_part_bt_compare_fcn, TABLE_PART_NAME) )
        return false;

    if ( !open_db_table(tpch_partsupp, flags, tpch_partsupp_bt_compare_fcn, TABLE_PARTSUPP_NAME) )
        return false;

    if ( !open_db_table(tpch_region, flags, tpch_region_bt_compare_fcn, TABLE_REGION_NAME) )
        return false;
       
    if ( !open_db_table(tpch_supplier, flags, tpch_supplier_bt_compare_fcn, TABLE_SUPPLIER_NAME) )
        return false;


    TRACE(TRACE_ALWAYS, "BerekeleyDB buffer pool set to %d GB, %d B\n",
          db_cache_size_gb,
          db_cache_size_bytes);
    TRACE(TRACE_ALWAYS, "TPCH database open\n");
    return true;
}



bool db_close() {


    bool ret = true;


    // close tables
    ret &= close_db_table(tpch_customer, TABLE_CUSTOMER_NAME);
    ret &= close_db_table(tpch_lineitem, TABLE_LINEITEM_NAME);
    ret &= close_db_table(tpch_nation, TABLE_NATION_NAME);
    ret &= close_db_table(tpch_orders, TABLE_ORDERS_NAME);
    ret &= close_db_table(tpch_part, TABLE_PART_NAME);
    ret &= close_db_table(tpch_partsupp, TABLE_PARTSUPP_NAME);
    ret &= close_db_table(tpch_region, TABLE_REGION_NAME);
    ret &= close_db_table(tpch_supplier, TABLE_SUPPLIER_NAME);

    
    // close environment
    try {    
 	dbenv->close(0);
    }
    catch ( DbException &e) {
	TRACE(TRACE_ALWAYS, "Caught DbException closing environment\n");
        TRACE(TRACE_ALWAYS, "DbException: %s\n", e.what());
        return false;
    }

    return ret;
}



static bool open_db_table(Db*& table, u_int32_t flags, int (*cmp) (Db*, const Dbt*, const Dbt*), const char* table_name) {

    try {
        table = new Db(dbenv, 0);
        table->set_bt_compare(cmp);
        table->open(NULL, table_name, NULL, DB_BTREE, flags, 0644);
    }
    catch ( DbException &e) {
        TRACE(TRACE_ALWAYS, "Caught DbException opening table \"%s\". Make sure database is set up properly\n",
              table_name);
        TRACE(TRACE_ALWAYS, "DbException: %s\n", e.what());
        return false;
    }

    return true;
}



static bool close_db_table(Db*& table, const char* table_name) {

    try {
        table->close(0);
    }
    catch ( DbException &e) {
	TRACE(TRACE_ALWAYS, "Caught DbException closing table \"%s\"\n",
              table_name);
        TRACE(TRACE_ALWAYS, "DbException: %s\n", e.what());
        return false;
    }
    
    return true;
}
