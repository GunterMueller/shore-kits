/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file:   shore_table.h
 *
 *  @brief:  Base class for tables stored in Shore
 *
 *  @author: Mengzhi Wang, April 2001
 *  @author: Ippokratis Pandis, January 2008
 *
 */


/* Tuple.h contains the base class (table_desc_t) for tables stored in
 * Shore. The table consists of several parts:
 *
 * 1. An array of field_desc, which contains the decription of the
 * fields.  The number of fields is set by the constructor. The schema
 * of the table is not written to the disk.  
 * 
 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
 * @note  Modifications to the schema need rebuilding the whole
 *        database.
 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
 *
 * 2. The primary index of the table. 
 *
 * 3. Secondary indices on the table.  All the secondary indices created
 * on the table are stored as a linked list.
 *
 * 4. Physical id of the current tuple.  (Record id type: rid_t,
 * defined in Shore.)
 *
 * There are methods in tuple_desc for creation, loading of the table
 * and indexes.  Operation on a single tuple, including adding,
 * updating, and index probe is provided as well.
 *
 * The file scan and index scan on the table are implemented through
 * two subclasses, tuple_iter_file_scan and tuple_iter_index_scan,
 * which are inherited from tuple_iter.
 *
 * USAGE:
 *
 * To create a new table, create a class for the table by inheriting
 * publicly from class tuple_desc to take advantage of all the
 * built-in tools. The schema of the table should be set at the
 * constructor of the table.  (See tpcc_table.h for examples.)
 *
 * NOTE:
 *
 * Due to the limitation of Shore implementation, only the last field
 * in indices can be variable length.
 *
 * BUGS:
 *
 * If a new index is created on an existing table, explicit call to
 * load the index is needed.
 *
 * Timestamp field is not fully implemented: no set function.
 *
 * EXTENSIONS:
 *
 * The mapping between SQL types and C types are defined in
 * sql_type_desc.  Modify the class to support more SQL types or
 * change the mapping.  The NUMERIC type is current stored as string;
 * no further understanding is provided yet.  */

/* The disk format of the record looks like this:
 *
 *  +--+-----+------------+-+-+-----+-------------+
 *  |NF| a   | d          | | | b   | c           |
 *  +--+-----+------------+-+-+-----+-------------+
 *                         | |      ^             ^
 *                         | +------+-------------+
 *                         |        |
 *                         +--------+
 *
 * The first part of tuple (NF) is dedicated to the null flags.  There
 * is a bit for each nullable field in null flags to tell whether the
 * data is presented. The space for the null flag is rounded to bytes.
 *
 * All the fixed size fields go first (a and d) and variable length
 * fields are appended after that. (b and c).  For the variable length
 * fields, we don't reserve the space for the full length of the value
 * but allocate as much space as needed.  Therefore, we need offsets
 * to tell the length of the actual values.  So here comes the two
 * additional slots in the middle (between d and b).  In our
 * implementation, we store the offset of the end of b relative to the
 * beginning of the tuple (address of a).  */

#ifndef __SHORE_TABLE_H
#define __SHORE_TABLE_H


#include "util.h"
#include "sm_vas.h"

#include "shore_msg.h"
#include "shore_error.h"
#include "shore_file_desc.h"
#include "shore_tools.h"
#include "shore_field.h"
#include "shore_iter.h"
#include "shore_index.h"

ENTER_NAMESPACE(shore);


class table_scan_iter_impl;


/* ---------------------------------------------------------------
 * @class: table_desc_t
 *
 * @brief: Description of a Shore table.
 *
 * --------------------------------------------------------------- */

/* @@@@ temporarily it holds   @@@ */
/* @@@@ and the current value  @@@ */

class table_desc_t : public file_desc_t {
    friend class table_scan_iter_impl;
   
protected:

    /* ------------------- */
    /* --- table schema -- */
    /* ------------------- */


    field_desc_t*  _desc;                     /* schema - set of field descriptors */
    index_desc_t*  _primary_idx;              /* pointer to primary idx */
    index_desc_t*  _indexes;                  /* indices on the table */
    char           _keydesc[MAX_KEYDESC_LEN]; /* buffer for key descriptions */

    int            find_field(const char* field_name) const;
    index_desc_t*  find_index(const char* name) { 
        return (_indexes ? _indexes->find_by_name(name) : NULL); 
    }


    // @@@@@@@@@@@@@@@@
    // TODO (ip) In order to be multi-threaded those two
    //           fields should be gone
    // @@@@@@@@@@@@@@@@
 
    rid_t          _rid;                      /* current tuple id */    
    char*          _formatted_data;           /* buffer for tuple in disk format */

    rid_t   rid() const { return _rid; }
    void    set_rid(const rid_t & rid) { _rid = rid; }
    bool    is_rid_valid() const { return _rid != rid_t::null; }



    /* ------------------------------------------------ */
    /* --- helper functions for bulkloading indices --- */
    /* ------------------------------------------------ */

    char* index_keydesc(index_desc_t * index);    /* index key description */
    char* format_key(index_desc_t * index);       /* format the key value */
    char* min_key(index_desc_t * index);
    char* max_key(index_desc_t * index);
    int   key_size(index_desc_t * index) const;   /* length of the formatted key */
    int   maxkeysize(index_desc_t * index) const; /* max key size */


public:

    /* ------------------- */
    /* --- Constructor --- */
    /* ------------------- */

    table_desc_t(const char* name, int fieldcnt)
        : file_desc_t(name, fieldcnt), _formatted_data(NULL), 
          _primary_idx(NULL), _indexes(NULL) 
    {
        // Create placeholders for the field descriptors
	_desc = new field_desc_t[fieldcnt];
    }
    
    virtual ~table_desc_t() {
        if (_desc)
            delete [] _desc;

        if (_formatted_data)
            delete [] _formatted_data;

        if (_indexes)
            delete _indexes;
    }


    /* -------------------------------------------------------- */
    /* --- conversion between disk format and memory format --- */
    /* -------------------------------------------------------- */

    int maxsize() const;  /* maximum requirement for disk format */

    char* buffer() {
	if (!_formatted_data) 
            _formatted_data = new char[maxsize()];
	return (_formatted_data);
    }

    int    size() const;   /* disk space needed for current tuple */
    const char * format(); /* disk format of current tuple */

    bool   load(const char * string); /* load tuple from disk format */ 
    bool   load_keyvalue(const unsigned char* string,
			 index_desc_t * index); /* load key fields */



    /* ------------------------ */
    /* --- set field values --- */
    /* ------------------------ */
    
    void   set_null(int index) {
	assert (index >= 0 && index < _field_count);
	assert(_desc[index].allow_null());
	_desc[index].set_null();
    }

    void   set_value(int index, const int value) {
	assert (index >= 0 && index < _field_count);
	_desc[index].set_int_value(value);
    }

    void   set_value(int index, const short value) {
	assert (index >= 0 && index < _field_count);
	_desc[index].set_smallint_value(value);
    }

    void   set_value(int index, const double value) {
	assert (index >= 0 && index < _field_count);
	_desc[index].set_value(&value, 0);
    }

    void   set_value(int index, const char* string) {
	int len;
	assert (index >= 0 && index < _field_count);
	assert ( _desc );
	assert ( _desc[index].type() == SQL_VARCHAR || 
                 _desc[index].type() == SQL_CHAR );

	len = strlen(string);
	if ( _desc[index].type() == SQL_CHAR )  { 
            if ( len > _desc[index].maxsize() ) {
                len = _desc[index].maxsize();
            }
	}
	_desc[index].set_value(string, len);
    }

    void    set_value(int index, const timestamp_t & time) {
	assert (index >= 0 && index < _field_count);
	_desc[index].set_value(&time, 0);
    }


    /* ------------------------- */
    /* --- find field values --- */
    /* ------------------------- */

    bool  get_value(const int index, int& value) const;
    bool  get_value(const int index, short& value) const;
    bool  get_value(const int index, char* buffer, const int bufsize) const;
    bool  get_value(const int index, double& value) const;
    bool  get_value(const int index, timestamp_t& value) const;



    /* --------------------------------------- */
    /* populate the table with data from files */
    /* --------------------------------------- */

    w_rc_t   load_table_from_file(ss_m* db);


    /* ------------------------ */
    /* --- index facilities --- */
    /* ------------------------ */

    index_desc_t* find_index_by_name(const char* name) { 
        return (_indexes ? _indexes->find_by_name(name) : NULL); 
    }
    
    int index_count() { return _indexes->index_count(); } /* # of indexes */

    index_desc_t* primary() { return (_primary_idx); }
    void set_primary(index_desc_t* idx) { 
        assert (idx->is_primary() && idx->is_unique());
        _primary_idx = idx; 
    }


    /* create an index on the table (this only creates the index
     * decription for the index in memory. call bulkload_index to
     * create and populate the index on disks.
     */
    bool    create_index(const char* name,
			 const int* fields,
			 const int num,
			 bool unique=true,
                         bool primary=true);

    bool    create_primary_idx(const char* name,
                               const int* fields,
                               const int num);



    /* bulk-building the specified index on disks */

    w_rc_t  bulkload_index(ss_m* db);
    w_rc_t  bulkload_index(ss_m* db, const char* name);
    w_rc_t  bulkload_index(ss_m* db, index_desc_t* idx);

    /* check consistency between the indexes and table
     *   true:  consistent
     *   false: inconsistent
     */
    w_rc_t   check_index(ss_m* db, index_desc_t* idx);
    bool     check_index(ss_m* db);
    w_rc_t   scan_index(ss_m* db);
    w_rc_t   scan_index(ss_m* db, index_desc_t* idx);


    /* ---------------------- */
    /* --- access methods --- */
    /* ---------------------- */

    /* find the tuple through index */

    /* based on idx id */
    w_rc_t   index_probe(ss_m* db,
                         index_desc_t* idx,
                         lock_mode_t lmode = SH); /* on of SH and EX */

    w_rc_t   index_probe_forupdate(ss_m* db,
                                   index_desc_t* idx)
    {
        return (index_probe(db, idx, EX));
    }

    /* probe primary idx */
    w_rc_t   probe_primary(ss_m* db) 
    {
        assert (_primary_idx);
        return (index_probe(db, _primary_idx));
    }

    /* based on idx name */
    w_rc_t   index_probe(ss_m* db, const char* idx_name) 
    {
        index_desc_t* index = find_index(idx_name);
        assert(index);
        return (index_probe(db, index));
    }

    w_rc_t   index_probe_forupdate(ss_m* db, const char* idx_name) 
    { 
	index_desc_t * index = find_index(idx_name);
	assert(index);
	return index_probe_forupdate(db, index);
    }


    /* ----------------------------- */
    /* --- create physical table --- */
    /* ----------------------------- */

    w_rc_t    create_table(ss_m* db);



    /* -------------------------- */
    /* --- tuple manipulation --- */
    /* -------------------------- */

    w_rc_t    add_tuple(ss_m* db);
    w_rc_t    update_tuple(ss_m* db);
    w_rc_t    delete_tuple(ss_m* db);



    /* ------------------------------------------- */
    /* --- iterators for index and table scans --- */
    /* ------------------------------------------- */

    w_rc_t get_iter_for_file_scan(ss_m* db,
				  table_scan_iter_impl* &iter);

    w_rc_t get_iter_for_index_scan(ss_m* db,
				   index_desc_t* index,
				   index_scan_iter_impl* &iter,
				   scan_index_i::cmp_t c1,
				   const cvec_t & bound1,
				   scan_index_i::cmp_t c2,
				   const cvec_t & bound2,
				   bool need_tuple = false);
    

    /* ----------------------- */
    /* --- debugging tools --- */
    /* ----------------------- */

    w_rc_t print_table(ss_m* );                 /* print the table on screen */

    void   print_desc(ostream & os = cout);     /* print the schema */
    void   print_value(ostream & os = cout);    /* print the tuple value */

}; // EOF: table_desc_t



/* ---------------------------------------------------------------------
 * @class: table_scan_iter_impl
 *
 * @brief: Declaration of a table (file) scan iterator
 *
 * --------------------------------------------------------------------- */

typedef tuple_iter_t<table_desc_t, scan_file_i> table_scan_iter_t;

class table_scan_iter_impl : public table_scan_iter_t 
{
public:

    /* -------------------- */
    /* --- construction --- */
    /* -------------------- */

    table_scan_iter_impl(ss_m* db, 
                         table_desc_t* table) 
        : table_scan_iter_t(db, table)
    { W_COERCE(open_scan(db)); }
        
    ~table_scan_iter_impl() { close_scan(); }


    /* ------------------------ */
    /* --- fscan operations --- */
    /* ------------------------ */

    w_rc_t open_scan(ss_m* db) {
        if (!_opened) {
            W_DO(_file->check_fid(db));
            _scan = new scan_file_i(_file->fid(), ss_m::t_cc_record);
            _opened = true;
        }
        return RCOK;
    }

    w_rc_t next(ss_m* db, bool& eof) {
        if (!_opened) open_scan(db);
        pin_i* handle;
        W_DO(_scan->next(handle, 0, eof));
        if (!eof) {
            if (!_file->load(handle->body()))
                return RC(se_WRONG_DISK_DATA);
            _file->set_rid(handle->rid());
        }
        return RCOK;
    }

}; // EOF: tuple_iter_t<table_desc_t>
           




/******************************************************************
 * class table_desc_t methods 
 *
 ******************************************************************/


inline int table_desc_t::find_field(const char* field_name) const
{
    for (int i=0; i<_field_count; i++) {
        if (strcmp(field_name, _desc[i].name())==0) return i;
    }
    return -1;
}

inline char* table_desc_t::index_keydesc(index_desc_t* index)
{
    strcpy(_keydesc, "");
    for (int i=0; i<index->field_count(); i++) {
        strcat(_keydesc, _desc[index->key_index(i)].keydesc());
    }
    return _keydesc;
}

inline char* table_desc_t::format_key(index_desc_t* index)
{
    if (!_formatted_data) _formatted_data = new char [maxsize()];

    offset_t    offset = 0;
    for (int i=0; i<index->field_count(); i++) {
        int     ix = index->key_index(i);
        if (!_desc[ix].copy_value(_formatted_data+offset)) return NULL;

        if (_desc[ix].is_variable_length()) {
            offset += _desc[ix].realsize();
        }
        else {
            offset += _desc[ix].maxsize();
        }
    }
    return _formatted_data;
}

inline char* table_desc_t::min_key(index_desc_t* index)
{
    for (int i=0; i<index->field_count(); i++) {
	int field_index = index->key_index(i);
	_desc[field_index].set_min_value();
    }
    return format_key(index);
}

inline char* table_desc_t::max_key(index_desc_t* index)
{
    for (int i=0; i<index->field_count(); i++) {
	int field_index = index->key_index(i);
	_desc[field_index].set_max_value();
    }
    return format_key(index);
}

inline int table_desc_t::key_size(index_desc_t* index) const
{
    int size = 0;
    for (int i=0; i<index->field_count(); i++) {
        int  ix = index->key_index(i);
        if (_desc[ix].is_variable_length()) size += _desc[ix].realsize();
        else size += _desc[ix].maxsize();
    }
    return size;
}

inline int   table_desc_t::maxkeysize(index_desc_t * index) const
{
    int size = 0;
    for (int i=0; i<index->field_count(); i++) {
        size += _desc[index->key_index(i)].maxsize();
    }
    return size;
}




/** @fn    create_index
 *
 *  @brief Create an index on the table
 *
 *  @note  This only creates the index decription for the index in memory. 
 *         Call bulkload_index to create and populate the index on disks.
 */

inline  bool  table_desc_t::create_index(const char* name,
                                         const int* fields,
                                         const int num,
                                         bool unique,
                                         bool primary)
{
    index_desc_t* p_index = new index_desc_t(name, num, fields, unique);

    /* check the validity of the index */
    for (int i=0; i<num; i++)  {
        assert(fields[i] >= 0 && fields[i] < _field_count);

        /* only the last field in the index can be variable lengthed */
        if (_desc[fields[i]].is_variable_length() && i != num-1) {
            assert(false);
        }
    }

    /* link it to the list */
    if (_indexes == NULL) _indexes = p_index;
    else _indexes->insert(p_index);

    /* add as primary */
    if (p_index->is_unique() && p_index->is_primary())
        _primary_idx = p_index;

    return true;
}


/** @fn    create_primary_idx
 *
 *  @brief Create the primary index of the table
 */

inline  bool  table_desc_t::create_primary_idx(const char* name,
                                               const int* fields,
                                               const int num)
{
    assert (_primary_idx == NULL);
    index_desc_t* p_index = new index_desc_t(name, num, fields, true, true);

    /* check the validity of the index */
    for (int i=0; i<num; i++)  {
        assert(fields[i] >= 0 && fields[i] < _field_count);

        /* only the last field in the index can be variable lengthed */
        if (_desc[fields[i]].is_variable_length() && i != num-1) {
            assert(0);
        }
    }

    /* link it to the list of indexes */
    if (_indexes == NULL) _indexes = p_index;
    else _indexes->insert(p_index);

    /* make it the primary index */
    _primary_idx = p_index;

    return (true);
}



/* ------------------------- */
/* --- find field values --- */
/* ------------------------- */


inline bool table_desc_t::get_value(const int index,
                                    int & value) const
{
    assert(index >= 0 && index < _field_count);
    if (_desc[index].is_null()) {
        value = 0;
        return false;
    }
    value = _desc[index].get_int_value();
    return true;
}

inline bool table_desc_t::get_value(const int index,
                                    short & value) const
{
    assert(index >= 0 && index < _field_count);
    if (!_desc[index].is_null()) {
        value = _desc[index].get_smallint_value();
        return true;
    }
    return false;
}

inline bool table_desc_t::get_value(const int index,
                                    char * buffer,
                                    const int bufsize) const
{
    assert(index >= 0 && index < _field_count);
    if (!_desc[index].is_null()) {
        int size = MIN(bufsize-1, _desc[index].maxsize());
        _desc[index].get_string_value(buffer, size);
        buffer[size] ='\0';
        return true;
    }
    buffer[0] = '\0';
    return false;
}

inline bool table_desc_t::get_value(const int index,
                                    double & value) const
{
    assert(index >= 0 && index < _field_count);
    if (!_desc[index].is_null()) {
        value = _desc[index].get_float_value();
        return true;
    }
    return false;
}

inline  bool table_desc_t::get_value(const int index,
                                     timestamp_t & value) const
{
    assert(index >= 0 && index < _field_count);
    if (!_desc[index].is_null()) {
        value = _desc[index].get_time_value();
        return true;
    }
    return false;
}

/* return the maximum requirement for the tuple in disk format.
 * only used in allocating _formatted_data.  Should be called
 * only once for each table.
 */
inline int  table_desc_t::maxsize() const
{
    int size = 0;
    int var_count = 0;
    int null_count = 0;
    for (int i=0; i<_field_count; i++) {
        size += _desc[i].maxsize();
        if (_desc[i].allow_null()) null_count++;
        if (_desc[i].is_variable_length()) var_count++;
    }
    return size + var_count*sizeof(offset_t) + null_count/8 + 1;
}

/* return the actual size of the tuple in disk format */
inline int table_desc_t::size() const
{
    int size = 0;
    int null_count = 0;

    /* for a fixed length field, it just takes as much as the
     * space for the value itself to store.
     * for a variable length field, we store as much as the data
     * and the offset to tell the length of the data.
     * Of course, there is a bit for each nullable field.
     */
    for (int i=0; i<_field_count; i++) {
	if (_desc[i].allow_null()) {
	    null_count++;
	    if (_desc[i].is_null()) continue;
	}
	if (_desc[i].is_variable_length()) {
	    size += _desc[i].realsize();
	    size += sizeof(offset_t);
	}
	else size += _desc[i].maxsize();
    }
    if (null_count) size += (null_count >> 3) + 1;
    return size;
}


EXIT_NAMESPACE(shore);

#endif /* __SHORE_TABLE_H */
