/* -*- mode:C++; c-basic-offset:4 -*- */

/** @file shore_table.cpp
 *
 *  @brief Implementation of shore_table class
 *
 *  @author: Ippokratis Pandis, January 2008
 *
 */

#include "sm/shore/shore_table.h"

using namespace shore;


/****************************************************************** 
 *
 *  class table_desc_t methods 
 *
 ******************************************************************/


/* ----------------------------------------- */
/* --- create physical table and indexes --- */
/* ----------------------------------------- */


/********************************************************************* 
 *
 *  @fn:    create_table
 *
 *  @brief: Creates the physical table and all the corresponding indexes
 *
 *********************************************************************/

w_rc_t table_desc_t::create_table(ss_m* db)
{
    if (!is_vid_valid() || !is_root_valid())
	W_DO(find_root_iid(db));

    /* create the table */
    W_DO(db->create_file(vid(), _fid, smlevel_3::t_regular));

    /* add table entry to the metadata tree */
    file_info_t file;
    file.set_ftype(FT_REGULAR);
    file.set_fid(_fid);
    W_DO(ss_m::create_assoc(root_iid(),
			    vec_t(name(), strlen(name())),
			    vec_t(&file, sizeof(file_info_t))));
    

    /* create all the indexes of the table */
    index_desc_t* index = _indexes;
    while (index) {
	stid_t iid;

        /* create index */
	W_DO(db->create_index(_vid,
                              (index->is_unique() ? ss_m::t_uni_btree : ss_m::t_btree),
                              ss_m::t_regular,
                              index_keydesc(index),
                              (index->is_relaxed() ? ss_m::t_cc_none : ss_m::t_cc_kvl),
                              iid));
	index->set_fid(iid);

        /* add index entry to the metadata tree */
        if (index->is_primary())
            file.set_ftype(FT_PRIMARY_IDX);
        else
            file.set_ftype(FT_IDX);

	file.set_fid(iid);
	W_DO(db->create_assoc(root_iid(),
                              vec_t(index->name(), strlen(index->name())),
                              vec_t(&file, sizeof(file_info_t))));
				
        /* move to next index */
	index = index->next();
    }
    
    return (RCOK);
}


/****************************************************************** 
 *  
 *  @fn:    create_index
 *
 *  @brief: Create a regular or primary index on the table
 *
 *  @note:  This only creates the index decription for the index in memory. 
 *          Call bulkload_index to create and populate the index on disks.
 *
 ******************************************************************/

bool table_desc_t::create_index(const char* name,
                                const int* fields,
                                const int num,
                                const bool unique,
                                const bool primary,
                                const bool nolock)
{
    index_desc_t* p_index = new index_desc_t(name, num, fields, 
                                             unique, primary, nolock);

    /* check the validity of the index */
    for (int i=0; i<num; i++)  {
        assert(fields[i] >= 0 && fields[i] < _field_count);

        /* only the last field in the index can be variable lengthed */
        // (ip) I am not sure if this is still true  now
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


bool table_desc_t::create_primary_idx(const char* name,
                                      const int* fields,
                                      const int num,
                                      const bool nolock)
{
    index_desc_t* p_index = new index_desc_t(name, num, fields, true, true, nolock);

    /* check the validity of the index */
    for (int i=0; i<num; i++) {
        assert(fields[i] >= 0 && fields[i] < _field_count);

        /* only the last field in the index can be variable lengthed */
        if (_desc[fields[i]].is_variable_length() && i != num-1) {
            assert(false);
        }
    }

    /* link it to the list of indexes */
    if (_indexes == NULL) _indexes = p_index;
    else _indexes->insert(p_index);

    /* make it the primary index */
    _primary_idx = p_index;

    return (true);
}


/* ----------------- */
/* --- debugging --- */
/* ----------------- */

/* For debug use only: print the description for all the field. */
void table_desc_t::print_desc(ostream& os)
{
    os << "Schema for table " << _name << endl;
    os << "Numer of fields: " << _field_count << endl;
    for (int i=0; i<_field_count; i++) {
	_desc[i].print_desc(os);
    }
}


#include <strstream>
char const* db_pretty_print(table_desc_t const* ptdesc, int i=0, char const* s=0) {
    static char data[1024];
    std::strstream inout(data, sizeof(data));
    ((table_desc_t*)ptdesc)->print_desc(inout);
    inout << std::ends;
    return data;
}


