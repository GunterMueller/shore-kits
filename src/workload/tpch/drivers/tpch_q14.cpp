/* -*- mode:C++; c-basic-offset:4 -*- */

#include "stages.h"
#include "scheduler.h"
#include "workload/tpch/drivers/tpch_q14.h"

#include "workload/common.h"
#include "workload/tpch/tpch_db.h"

using namespace qpipe;

ENTER_NAMESPACE(workload);


/*
 * select
 *     100.00 * sum(case
 *         when p_type like 'PROMO%'
 *         then l_extendedprice*(1-l_discount)
 *         else 0
 *     end) / sum(l_extendedprice * (1 - l_discount)) as promo_revenue
 * from
 *     lineitem,
 *     part
 * where
 *     l_partkey = p_partkey
 *     and l_shipdate >= date '[DATE]'
 *     and l_shipdate < date '[DATE]' + interval '1' month;
 */

struct lineitem_scan_tuple {
    decimal L_EXTENDEDPRICE;
    decimal L_DISCOUNT;
    int L_PARTKEY;
};

struct part_scan_tuple {
    int P_PARTKEY;
    char P_TYPE[STRSIZE(25)];
};

struct join_tuple {
    decimal L_EXTENDEDPRICE;
    decimal L_DISCOUNT;
    char P_TYPE[STRSIZE(25)];
};

struct q14_tuple {
    static int const ALIGN;
    decimal PROMO_SUM;
    decimal TOTAL_SUM;
    char P_TYPE[STRSIZE(25)];
};

/**
 * @brief select P_PARTKEY, P_TYPE from PART
 */

struct part_tscan_filter_t : tuple_filter_t {
    part_tscan_filter_t()
        : tuple_filter_t(sizeof(tpch_part_tuple))
    {
    }
    virtual void project(tuple_t &d, const tuple_t &s) {
        part_scan_tuple* dest = aligned_cast<part_scan_tuple>(d.data);
        tpch_part_tuple* src = aligned_cast<tpch_part_tuple>(s.data);
        dest->P_PARTKEY = src->P_PARTKEY;
        memcpy(dest->P_TYPE, src->P_TYPE, sizeof(dest->P_TYPE));
    }
    virtual part_tscan_filter_t* clone() const {
        return new part_tscan_filter_t(*this);
    }
    virtual c_str to_string() const {
        return "select P_PARTKEY, P_TYPE";
    }
};

/**
 * @brief select L_PARTKEY, L_EXTENDEDPRICE, L_DISCOUNT from LINEITEM
 * where L_SHIPDATE >= [date] and L_SHIPDATE < [date] + 1 month
 */
struct lineitem_tscan_filter_t : tuple_filter_t {

    and_predicate_t _filter;
    time_t date1, date2;
    lineitem_tscan_filter_t()
        : tuple_filter_t(sizeof(tpch_lineitem_tuple))
    {
        size_t offset = offsetof(tpch_lineitem_tuple, L_SHIPDATE);
        predicate_t* p;

        // L_SHIPDATE >= [date]
        date1 = datestr_to_timet("1995-09-01");
        p = new scalar_predicate_t<time_t, greater_equal>(date1, offset);
        _filter.add(p);

        // L_SHIPDATE < [date] + 1 month
        date2 = time_add_month(date1, 1);
        p = new scalar_predicate_t<time_t, less>(date2, offset);
        _filter.add(p);
    }

    virtual void project(tuple_t &d, const tuple_t &s) {
        lineitem_scan_tuple *dest = aligned_cast<lineitem_scan_tuple>(d.data);
        tpch_lineitem_tuple *src = aligned_cast<tpch_lineitem_tuple>(s.data);

        dest->L_EXTENDEDPRICE = src->L_EXTENDEDPRICE;
        dest->L_DISCOUNT = src->L_DISCOUNT;
        dest->L_PARTKEY = src->L_PARTKEY;
    }
    virtual bool select(const tuple_t &t) {
        return _filter.select(t);
    }
    virtual lineitem_tscan_filter_t* clone() const {
        return new lineitem_tscan_filter_t(*this);
    }
    virtual c_str to_string() const {
        char* d1 = timet_to_datestr(date1);
        char* d2 = timet_to_datestr(date2);
        c_str result("select L_EXTENDEDPRICE, L_DISCOUNT, L_PARTKEY "
                     "where L_SHIPDATE >= %s and L_SHIPDATE < %s",
                     d1, d2);
        free(d1);
        free(d2);
        return result;
    }
};

/**
 * @brief join part, lineitem on P_PARTKEY = L_PARTKEY
 */
struct q14_join : tuple_join_t {

    q14_join()
        : tuple_join_t(sizeof(part_scan_tuple),
                       offsetof(part_scan_tuple, P_PARTKEY),
                       sizeof(lineitem_scan_tuple),
                       offsetof(lineitem_scan_tuple, L_PARTKEY),
                       sizeof(int),
                       sizeof(join_tuple))
    {
    }
    
    virtual void join(tuple_t &d, const tuple_t &l, const tuple_t &r) {
        join_tuple* dest = aligned_cast<join_tuple>(d.data);
        part_scan_tuple* left = aligned_cast<part_scan_tuple>(l.data);
        lineitem_scan_tuple* right = aligned_cast<lineitem_scan_tuple>(r.data);

        // cheat and filter out the join key...
        dest->L_EXTENDEDPRICE = right->L_EXTENDEDPRICE;
        dest->L_DISCOUNT = right->L_DISCOUNT;
        memcpy(dest->P_TYPE, left->P_TYPE, sizeof(dest->P_TYPE));
    }

    virtual q14_join* clone() const {
        return new q14_join(*this);
    }

    virtual c_str to_string() const {
        return "PART join LINEITEM";
    }
};

/**
 * @brief 100.00 * sum(case
 *         when p_type like 'PROMO%'
 *         then l_extendedprice*(1-l_discount)
 *         else 0
 *     end) / sum(l_extendedprice * (1 - l_discount)) as promo_revenue
 */
struct q14_aggregate : tuple_aggregate_t {
    default_key_extractor_t _extractor;
    like_predicate_t _filter;

    q14_aggregate()
        : tuple_aggregate_t(sizeof(q14_tuple)),
          _extractor(0, 0),
          _filter("PROMO%", offsetof(join_tuple, P_TYPE))
    {
    }

    virtual key_extractor_t* key_extractor() {
        return &_extractor;
    }
    virtual void aggregate(char* agg_data, const tuple_t &t) {
        q14_tuple* agg = aligned_cast<q14_tuple>(agg_data);
        join_tuple* tuple = aligned_cast<join_tuple>(t.data);

        decimal value = tuple->L_EXTENDEDPRICE*(1 - tuple->L_DISCOUNT);
        agg->TOTAL_SUM += value;
        if(_filter.select(t))
            agg->PROMO_SUM += value;
    }
    virtual void finish(tuple_t &d, const char* agg_data) {
        decimal* dest = aligned_cast<decimal>(d.data);
        q14_tuple* agg = aligned_cast<q14_tuple>(agg_data);
        *dest = 100.*agg->PROMO_SUM/agg->TOTAL_SUM;
    }
    virtual q14_aggregate* clone() const {
        return new q14_aggregate(*this);
    }
    virtual c_str to_string() const {
        return "q14_aggregate";
    }
};




void tpch_q14_driver::submit(void* disp) {

    scheduler::policy_t* dp = (scheduler::policy_t*)disp;
    qpipe::query_state_t* qs = dp->query_state_create();
    
    
    // lineitem scan
    tuple_filter_t* filter = new lineitem_tscan_filter_t();
    tuple_fifo* buffer = new tuple_fifo(sizeof(lineitem_scan_tuple));
    packet_t* lineitem_packet;
    lineitem_packet = new tscan_packet_t("lineitem TSCAN",
                                         buffer,
                                         filter,
                                         tpch_lineitem);
    lineitem_packet->assign_query_state(qs);

    // part scan
    filter = new part_tscan_filter_t();
    buffer = new tuple_fifo(sizeof(part_scan_tuple));
    packet_t* part_packet;
    part_packet = new tscan_packet_t("part TSCAN",
                                     buffer, filter,
                                     tpch_part);
    part_packet->assign_query_state(qs);
    
    // join
    filter = new trivial_filter_t(sizeof(join_tuple));
    buffer = new tuple_fifo(sizeof(join_tuple));
    packet_t* join_packet;
    join_packet = new hash_join_packet_t("part-lineitem HJOIN",
                                         buffer, filter,
                                         part_packet,
                                         lineitem_packet,
                                         new q14_join());
    join_packet->assign_query_state(qs);
    
    // aggregation
    filter = new trivial_filter_t(sizeof(decimal));
    buffer = new tuple_fifo(sizeof(decimal));
    key_extractor_t* extractor = new default_key_extractor_t(0, 0);
    tuple_aggregate_t* aggregate = new q14_aggregate();
    packet_t* agg_packet;
    agg_packet = new aggregate_packet_t("sum AGG",
                                        buffer, filter,
                                        aggregate,
                                        extractor,
                                        join_packet);
    agg_packet->assign_query_state(qs);
    

    // go!
    dispatcher_t::dispatch_packet(agg_packet);
    guard<tuple_fifo> result = agg_packet->output_buffer();
    
    
    tuple_t output;
    while(result->get_tuple(output)) {
        decimal* r = aligned_cast<decimal>(output.data);
        TRACE(TRACE_ALWAYS, "*** Q14 Promo Revenue: %5.2lf\n", r->to_double());
    }
    
    dp->query_state_destroy(qs);
}


EXIT_NAMESPACE(workload);