/** -*- mode:C++; c-basic-offset:4 -*- */

#include "core/tuple_fifo.h"
#include "core/tuple_fifo_directory.h"
#include "util/trace.h"
#include "util/acounter.h"
#include "scheduler/os_support.h"
#include <algorithm>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>



ENTER_NAMESPACE(qpipe);



/* debugging */
static int TRACE_MASK_WAITS = TRACE_COMPONENT_MASK_NONE;
static int TRACE_MASK_DISK  = TRACE_COMPONENT_MASK_NONE;
bool FLUSH_TO_DISK_ON_FULL = true;
bool USE_DIRECT_IO = false;
bool WAIT_FOR_UNSHARED_TUPLE_FIFOS_TO_DRAIN = true;



/**
 * @brief Whether we should invoke fsync after we write pages to
 * disk. Note that this is totally un-necessary if we are using DIRECT
 * I/O. The sync should already be done.
 */
static const bool SYNC_AFTER_WRITES = (!USE_DIRECT_IO) && false;



/* Global tuple_fifo statistics */

/* statistics data structures */
static pthread_mutex_t tuple_fifo_stats_mutex = thread_mutex_create();

static struct {
    int open_fifo_count;
    int total_fifos_created;
    int total_fifos_destroyed;
    int total_fifos_experienced_read_wait;
    int total_fifos_experienced_write_wait;
    int total_fifos_experienced_wait;
    int total_fifos_stayed_in_memory;
    int total_fifos_terminated_in_memory;
    int total_fifos_terminated_on_disk;
    int total_fifos_shared;

} _tuple_fifo_stats;




/* statistics methods */

/**
 * @brief Reset global statistics to initial values. This method _is_
 * synchronized.
 */
void tuple_fifo::clear_stats() {
    critical_section_t cs(tuple_fifo_stats_mutex);
    memset(&_tuple_fifo_stats, 0, sizeof(_tuple_fifo_stats));
}



/**
 * @brief Dump stats using TRACE.
 */
void tuple_fifo::trace_stats() {

    TRACE(TRACE_ALWAYS,
          "--- Since the last clear_stats\n");

    TRACE(TRACE_ALWAYS, "%d tuple_fifos created\n",
          _tuple_fifo_stats.total_fifos_created);
    TRACE(TRACE_ALWAYS, "%d tuple_fifos destroyed\n",
          _tuple_fifo_stats.total_fifos_destroyed);
    TRACE(TRACE_ALWAYS, "%d tuple_fifos currently open\n",
          _tuple_fifo_stats.open_fifo_count);

    TRACE(TRACE_ALWAYS, "%d tuple_fifos stayed in memory\n",
          _tuple_fifo_stats.total_fifos_stayed_in_memory);
    TRACE(TRACE_ALWAYS, "%d tuple_fifos experienced read waits\n",
          _tuple_fifo_stats.total_fifos_experienced_read_wait);
    TRACE(TRACE_ALWAYS, "%d tuple_fifos experienced write waits\n",
          _tuple_fifo_stats.total_fifos_experienced_write_wait);
    TRACE(TRACE_ALWAYS, "%d tuple_fifos experienced waits (read or write)\n",
          _tuple_fifo_stats.total_fifos_experienced_wait);

    TRACE(TRACE_ALWAYS, "%d tuple_fifos terminated while in memory\n",
          _tuple_fifo_stats.total_fifos_terminated_in_memory);
    TRACE(TRACE_ALWAYS, "%d tuple_fifos terminated while on disk\n",
          _tuple_fifo_stats.total_fifos_terminated_on_disk);

    TRACE(TRACE_ALWAYS, "%d tuple_fifos shared\n",
          _tuple_fifo_stats.total_fifos_shared);
}



/* Sentinal page pool. */

/**
 * @brief Every page within a tuple_fifo needs to be part of a
 * page_pool that manages its deletion. The sentinal_page_pool
 * allocates a single global "sentinal page" that is never freed. This
 * page is inserted into every tuple_fifo to reduce the number of
 * corner cases it needs to deal with.
 *
 * The sentinal page does not go away when it is "freed".
 */
struct sentinel_page_pool : page_pool {
    
    /* We will use this single buffer for our global page instance */
    char _data[sizeof(qpipe::page)];

    sentinel_page_pool()
        : page_pool(sizeof(qpipe::page))
    {
    }

    virtual void* alloc() {
        return _data;
    }

    virtual void free(void*) {
        /* do nothing */
    }
};



/* sentinal page data structures */

static sentinel_page_pool SENTINEL_POOL;
static qpipe::page* SENTINEL_PAGE = qpipe::page::alloc(1, &SENTINEL_POOL);



/* definitions of exported methods */

/*
 * Creates a new (temporary) file in the BDB bufferpool for the FIFO
 * to use as a backing store. The file will be deleted automatically
 * when the FIFO is destroyed.
 */
void tuple_fifo::init() {

    /* Check on the tuple_fifo directory. */
    tuple_fifo_directory_t::open_once();

    _reader_tid = pthread_self();

    /* Prepare for reading. */
    _set_read_page(SENTINEL_PAGE);

    /* Prepare for writing. */
    /* TODO ... original code was _write_page = SENTINEL_PAGE; */
    _write_page = _alloc_page();
    
    /* update state */
    _state.transition(tuple_fifo_state_t::IN_MEMORY);

    /* Update statistics. */
    critical_section_t cs(tuple_fifo_stats_mutex);
    _tuple_fifo_stats.open_fifo_count++;
    _tuple_fifo_stats.total_fifos_created++;
    cs.exit();
}



/**
 * @brief Should be invoked by the writer before writing tuples.
 */
void tuple_fifo::writer_init() {
    /* set up the _writer_tid field to make debugging easier */
    _writer_tid = pthread_self();
}



/**
 * @brief Function object. Wrapper around p->free() that can be passed
 * to std::for_each.
 */
struct free_page {
    void operator()(qpipe::page* p) {
	p->free();
    }
};



/**
 * @brief Deallocate the pags in this tuple_fifo and add our local
 * statistics to global ones.
 */
void tuple_fifo::destroy() {


    if (!FLUSH_TO_DISK_ON_FULL)
        assert(is_in_memory());
    

    std::for_each(_pages.begin(), _pages.end(), free_page());
    std::for_each(_free_pages.begin(), _free_pages.end(), free_page());


    if (_page_file != -1) {
        close(_page_file);
        c_str filepath = tuple_fifo_directory_t::generate_filepath(_fifo_id);
        if (unlink(filepath.data()))
            THROW2(TupleFifoDirectoryException,
                   "unlink(%s) failed", filepath.data());
    }


    /* update stats */
    critical_section_t cs(tuple_fifo_stats_mutex);
    _tuple_fifo_stats.open_fifo_count--;
    _tuple_fifo_stats.total_fifos_destroyed++;
    bool write_wait = _num_waits_on_insert > 0;
    bool read_wait  = _num_waits_on_remove > 0;
    if (read_wait)
        _tuple_fifo_stats.total_fifos_experienced_read_wait++;
    if (write_wait)
        _tuple_fifo_stats.total_fifos_experienced_write_wait++;
    if (read_wait || write_wait)
        _tuple_fifo_stats.total_fifos_experienced_wait++;
    if (is_in_memory())
        _tuple_fifo_stats.total_fifos_stayed_in_memory++;
    if (is_terminated())
        if (is_in_memory())
            _tuple_fifo_stats.total_fifos_terminated_in_memory++;
        else
            _tuple_fifo_stats.total_fifos_terminated_on_disk++;
    if (is_shared())
        _tuple_fifo_stats.total_fifos_shared++;
    cs.exit();


    TRACE(TRACE_MASK_WAITS & TRACE_ALWAYS,
          "Blocked on insert %.2f\n",
          (double)_num_waits_on_insert/_num_inserted);
    TRACE(TRACE_MASK_WAITS & TRACE_ALWAYS,
          "Blocked on remove %.2f\n",
          (double)_num_waits_on_remove/_num_removed);
}



/**
 *  @brief Only the consumer may call this method. Retrieve a page
 *  of tuples from the buffer in one operation.
 *
 *  WARNING: Do not mix calls to copy_page() and get_tuple() on the
 *  same tuple_fifo.
 *
 *  @param timeout The maximum number of milliseconds to wait for a
 *  tuple.
 *
 *  @throw BufferTerminatedException if the producer has
 *  terminated the buffer.
 */
bool tuple_fifo::copy_page(page* dst, int timeout_ms) {
    
    if (!ensure_read_ready(timeout_ms))
        /* how to check for timeout? */
        return false;

    /* no partial pages allowed! */
    assert(_read_iterator == _read_page->begin());

    /* copy over tuples */
    dst->clear();
    size_t capacity = dst->capacity();
    for(size_t i = 0; i < capacity; i++) {
        tuple_t tuple;
        if (!get_tuple(tuple))
            break;
        dst->append_tuple(tuple);
    }

    /* return page */
    _num_removed += dst->tuple_count();
    return true;
}



/**
 * @brief Only the producer may call this method. Notify the
 * tuple_fifo that the caller will be inserting no more data.  The
 * FIFO will close normally once the last tuple has been read; the
 * reader is responsible for deleting it. The writer must not access
 * the FIFO again if send_eof() returns true.
 *
 * @return True if we successfully send and EOF. False if the fifo has
 * been terminated.
 */
bool tuple_fifo::send_eof() {
    try {
        // make sure not to drop a partial page
        _flush_write_page(true);
        return true;
    }
    catch(TerminatedBufferException &e) {
        // oops! reader terminated first
        return false;
    }
}



/**
 * @brief Both producer and consumer may call this method. This method
 * is used to notify the "other side" that we are terminating
 * abnormally. The other side should receive a
 * TerminatedBufferException on the next tuple_fifo operation.
 *
 * If successful, the other end will be notified through an
 * exception at some point in the future and will be responsible
 * for deleting the FIFO. The caller must not access the FIFO
 * again if terminate() returns true.
 *
 * @return True if we successfully terminate. False if the other side
 * has already terminated.
 */
bool tuple_fifo::terminate() {
    
    // * * * BEGIN CRITICAL SECTION * * *
    critical_section_t cs(_lock);


    // did the other end beat me to it? Note: if _done_writing is true
    // the reader is responsible for deleting the buffer; either the
    // the reader doesn't know yet or the writer is making an illegal
    // access. False is the right return value for both cases.
    if(is_terminated() || is_done_writing())
        return false;
    
    // make sure nobody is sleeping (either the reader or writer could
    // be calling this)
    
    if (is_in_memory())
        _state.transition(tuple_fifo_state_t::IN_MEMORY_TERMINATED);
    else
        _state.transition(tuple_fifo_state_t::ON_DISK_TERMINATED);
    
    thread_cond_signal(_reader_notify);
    thread_cond_signal(_writer_notify);
    
    // * * * END CRITICAL SECTION * * *
    return true;
}



void tuple_fifo::set_shared() {

    critical_section_t cs(_lock);
    
    _is_shared = true;
    
    /* The writer may be waiting for the tuple_fifo to drain. Now
       that the tuple_fifo is being shared, the writer may be
       forced to flush pages to disk to avoid deadlock. */
    ensure_writer_running();
}



/* definitions of helper methods */


inline void tuple_fifo::wait_for_reader() {
    _num_waits_on_insert++;
    thread_cond_wait(_writer_notify, _lock);
}



inline void tuple_fifo::ensure_reader_running() {
    thread_cond_signal(_reader_notify);
}



inline bool tuple_fifo::wait_for_writer(int timeout_ms) {
    _num_waits_on_remove++;
    return thread_cond_wait(_reader_notify, _lock, timeout_ms);
}



inline void tuple_fifo::ensure_writer_running() {
    thread_cond_signal(_writer_notify);
}



/**
 * @brief Get a page from the tuple_fifo.
 *
 * @return NULL if the tuple_fifo has been closed. A page otherwise.
 */
void tuple_fifo::_flush_write_page(bool done_writing) {

    /* After the call to send_eof() the write page is NULL. */
    assert(!is_done_writing());
    if (_write_page->empty())
        assert(done_writing);


    // * * * BEGIN CRITICAL SECTION * * *
    critical_section_t cs(_lock);
    _termination_check();


    /* Wait for space to free up if we are using a "no flush"
       policy. */
    if (!FLUSH_TO_DISK_ON_FULL) {
        
        assert(is_in_memory());
        assert(_state.current() == tuple_fifo_state_t::IN_MEMORY);


        /* tuple_fifo stays in memory */
        /* If the buffer is currently full, we must wait for space to
           open up. Once we start waiting, we continue waiting until
           space for '_threshold' pages becomes available. */
        for(size_t threshold=1;
            _available_memory_writes() < threshold; threshold = _threshold) {
            /* wait until something important changes */
            wait_for_reader();
            _termination_check();
        }


        /* Add _write_page to page list unless empty. If _write_page
           is empty, 'done_writing' should be true and we will free
           the page when we call _write_page.done() below. */
        if(!_write_page->empty()) {
            
            /* We can safely add to the page list since we have
               verified that _available_memory_writes() > 0. */
            _pages.push_back(_write_page.release());
            _pages_in_memory++;
            _available_fifo_reads++;

            if (_pages_in_memory == 1)
                /* added new list head page */
                _list_head_page_index = _write_page_index;
            _list_tail_page_index = _write_page_index;
            _write_page_index++;
        }

        
        /* Allocate a new _write_page if necessary. */
        if(done_writing) {
            /* Allocation of a new _write_page is not necessary
               (because we are done writing). Just do state
               transition. */
            _state.transition(tuple_fifo_state_t::IN_MEMORY_DONE_WRITING);
            _write_page.done();
            _write_page_index = -1;
        }
        else {
            /* If is safe to allocate another page. We have made
               sure that there are available writes. */
            _write_page = _alloc_page();
        }
        

        /* wake the reader if necessary */
        if((_available_reads() >= _threshold) || is_done_writing())
            ensure_reader_running();
        
        return;
    }


    /* FLUSH_TO_DISK_ON_FULL is true */


    if (WAIT_FOR_UNSHARED_TUPLE_FIFOS_TO_DRAIN) {

        /* Quick optimization: If the tuple_fifo is not part of a sequence
           of merged packets, we can avoid disk I/O by simply waiting. We
           do not risk deadlock because we are not part of a work sharing
           cycle. */
        
        /* If the buffer is currently full and not shared, we can wait for
           space to open up. Once we start waiting, we continue waiting
           until either the buffer becomes shared or space for
           '_threshold' pages becomes available. */

        bool waited = false;
        for(size_t threshold=1;
            !is_shared() && (_available_memory_writes() < threshold);
            threshold = _threshold) {
            /* wait until something important changes */
            waited = true;
            wait_for_reader();
            _termination_check();
        }
        if (waited)
            assert(is_shared() || (_available_memory_writes() >= _threshold));
        assert(is_shared() || (_available_memory_writes() >= 1));
    }
    

    /* Check for cases in which we don't need to flush to disk. */
    if (_available_memory_writes() >= 1) {
        
        /* Add _write_page to page list unless empty. If _write_page
           is empty, 'done_writing' should be true and we will free
           the page when we call _write_page.done() below. */
        if(!_write_page->empty()) {

            /* We can safely add to the page list since we have
               verified that _available_memory_writes() > 0. */
            _pages.push_back(_write_page.release());
            _pages_in_memory++;
            _available_fifo_reads++;
            
            if (_pages_in_memory == 1)
                /* added new list head page */
                _list_head_page_index = _write_page_index;
            _list_tail_page_index = _write_page_index;
            _write_page_index++;
        }

        
        /* Allocate a new _write_page if necessary. */
        if(done_writing) {
            /* Allocation of a new _write_page is not necessary
               (because we are done writing). Just do state
               transition. */
            if (is_in_memory())
                _state.transition(tuple_fifo_state_t::IN_MEMORY_DONE_WRITING);
            else
                _state.transition(tuple_fifo_state_t::ON_DISK_DONE_WRITING);
            _write_page.done();
            _write_page_index = -1;
        }
        else {
            /* If is safe to allocate another page. We have made
               sure that there are available writes. */
            _write_page = _alloc_page();
        }


        /* wake the reader if necessary */
        if((_available_reads() >= _threshold) || is_done_writing())
            ensure_reader_running();

        
        return;
    }



    /* If we are here, we have no available memory writes
       remaining. We need to flush to disk. */
    if (is_in_memory()) {


        /* Create on disk file. */
        c_str filepath = tuple_fifo_directory_t::generate_filepath(_fifo_id);


#ifdef FOUND_LINUX
        
        int flags = O_CREAT|O_TRUNC|O_RDWR;
        if (USE_DIRECT_IO) flags |= O_DIRECT;
        _page_file = open(filepath.data(), flags, S_IRUSR|S_IWUSR);
        if (_page_file == -1)
            THROW2(FileException, "open(%s) failed", filepath.data());
        
#endif


#ifdef FOUND_SOLARIS
        
        int flags = O_CREAT|O_TRUNC|O_RDWR;
        _page_file = open(filepath.data(), flags, S_IRUSR|S_IWUSR);
        if (_page_file == -1)
            THROW2(FileException, "open(%s) failed", filepath.data());
        if (USE_DIRECT_IO) {
            int directio_ret = directio(_page_file, DIRECTIO_ON);
            if (directio_ret != 0) {
                TRACE(TRACE_ALWAYS,
                      "directio(%d, DIRECTIO_ON) failed: %s\n",
                      _page_file,
                      strerror(errno));
                assert(false);
            }
        }

#endif

        
        TRACE(TRACE_TUPLE_FIFO_FILE, "Created tuple_fifo file %s\n", filepath.data());

        
        /* Write list pages to disk. */
        for (page_list::iterator it = _pages.begin(); it != _pages.end(); ) {
            qpipe::page* p = *it;
            p->write_full_page(_page_file);
            p->clear();
            _free_pages.push_back(p);
            it = _pages.erase(it);
            assert(_pages_in_memory > 0);
            _pages_in_memory--;
        }


        /* If the _write_page is not empty, write it to disk as
           well. If _write_page is empty, 'done_writing' should be
           true and we will free the page when we call
           _write_page.done() below. */
        size_t last_page_written = _list_tail_page_index;
        if(!_write_page->empty()) {
            _write_page->write_full_page(_page_file);
            _write_page->clear();
            last_page_written = _write_page_index;
            _write_page_index++;
        }
        if (SYNC_AFTER_WRITES) {
            int fsync_ret = fsync(_page_file);
            assert(fsync_ret == 0);
        }


        /* Debugging... */
        TRACE(TRACE_TUPLE_FIFO_FILE,
              "FIFO %d: Wrote pages %zd-%zd to disk\n", _fifo_id,
              _list_head_page_index, last_page_written);


        /* Let the reader know where to get pages */
        /* Since we were in memory, the file have been empty. We need
           to initialize the file index values to the proper page
           indices and update the list index values to indicate an
           empty list. */
        assert(_file_head_page_index == -1);
        assert(_file_tail_page_index == -1);
        _file_head_page_index = _list_head_page_index;
        _file_tail_page_index = last_page_written;
        _list_head_page_index = -1;
        _list_tail_page_index = -1;
        _state.transition(tuple_fifo_state_t::ON_DISK);

       
        if (done_writing) {
            _state.transition(tuple_fifo_state_t::ON_DISK_DONE_WRITING);
            _write_page.done();
            _write_page_index = -1;
        }


        /* wake the reader if necessary */
        if((_available_reads() >= _threshold) || is_done_writing())
            ensure_reader_running();

        return;
    }
        


    /* If we are here, we have no available memory writes
       remaining. We need to flush to disk. The disk file already
       exists. */


    /* Flush to disk */
    /* If the reader needs to pull the next page from the list, the
       reader has finished draining the disk file. We can overwrite
       the disk file from the beginning. */
    bool overwrite =
        (_next_read_page_index >= _list_head_page_index) &&
        (_next_read_page_index <= _list_tail_page_index);

    if (overwrite) {
        int seek_ret = lseek(_page_file, 0, SEEK_SET);
        assert(seek_ret != (off_t)-1);
        if (seek_ret == (off_t)-1)
            THROW1(FileException, "seek to END-OF-FILE");
    }
    else {
        /* append */
        int seek_ret = lseek(_page_file, 0, SEEK_END);
        assert(seek_ret != (off_t)-1);
        if (seek_ret == (off_t)-1)
            THROW1(FileException, "seek to END-OF-FILE");
    }


    /* Write list pages to disk. */
    for (page_list::iterator it = _pages.begin(); it != _pages.end(); ) {
        qpipe::page* p = *it;
        p->write_full_page(_page_file);
        p->clear();
        _free_pages.push_back(p);
        it = _pages.erase(it);
        assert(_pages_in_memory > 0);
        _pages_in_memory--;
    }
    

    /* If the _write_page is not empty, write it to disk as well. If
       _write_page is empty, 'done_writing' should be true and we will
       free the page when we call _write_page.done() below. */
    size_t last_page_written = _list_tail_page_index;
    if(!_write_page->empty()) {
        _write_page->write_full_page(_page_file);
        _write_page->clear();
        last_page_written = _write_page_index;
        _write_page_index++;
    }
    if (SYNC_AFTER_WRITES) {
        int fsync_ret = fsync(_page_file);
        assert(fsync_ret == 0);
    }
    
    
    /* Debugging... */
    TRACE(TRACE_TUPLE_FIFO_FILE,
          "FIFO %d: Wrote pages %zd-%zd to disk\n",
          _fifo_id,
          _list_head_page_index,
          last_page_written);
    
    
    /* Let the reader know where to get pages */
    if (overwrite)
        _file_head_page_index = _list_head_page_index;
    _file_tail_page_index = last_page_written;
    _list_head_page_index = -1;
    _list_tail_page_index = -1;
    

    if (done_writing) {
        _state.transition(tuple_fifo_state_t::ON_DISK_DONE_WRITING);
        _write_page.done();
        _write_page_index = -1;
    }
    
    /* wake the reader if necessary */
    if((_available_reads() >= _threshold) || is_done_writing())
        ensure_reader_running();
    
    return;
    // * * * END CRITICAL SECTION * * *
}



/**
 * @brief Use a timeout_ms value of 0 to wait until a page appears or
 * the tuple_fifo is closed (normal behavior). Use a negative
 * timeout_ms value to avoid waiting. If the tuple_fifo contains no
 * pages, we return immediately with a value of 0. Use a positive
 * timeout_ms value to wait for a max length of time.
 *
 * @return 1 if we got a page. -1 if the tuple_fifo has been
 * closed. If 'timeout_ms' is negative and this method returns 0, it
 * means the tuple_fifo is empty. If the timeout_ms value is positive
 * and we return 0, it means we timed out.
 */
int tuple_fifo::_get_read_page(int timeout_ms) {

    // * * * BEGIN CRITICAL SECTION * * *
    critical_section_t cs(_lock);
    _termination_check();
    

    /* Determine whether the current _read_page should be freed or
       added to the free list. We want to free pages which we
       temporarily allocated as buffers to read pages off disk. */
    if (_add_read_page_to_free_list) {

        assert(_read_page != SENTINEL_PAGE);

        /* Release the page from the guard so it does not get
           freed. */
        _free_pages.push_back(_read_page.release());

        _set_read_page(SENTINEL_PAGE);
        _read_page_index = -1;

        /* SENTINAL_PAGE should never be added to the free list. */
        _add_read_page_to_free_list = false;
    }



    /* If there are no pages available, wait until either
       '_threshold' pages appear or the tuple_fifo is closed. */
    bool timed_out = false;
    if(timeout_ms >= 0) {
        for(size_t t=1;
                   !timed_out
                && !is_done_writing()
                && (_available_reads() < t);
            t = _threshold) {
            
            if(!wait_for_writer(timeout_ms)) /* check for timeout */
                timed_out = true;
                
            _termination_check();
        }
    }



    /* Special Cases: non-blocking, done writing, and timeout */
    if(_available_reads() == 0) {
        assert(is_done_writing() || (timeout_ms < 0) || timed_out);
        if(is_done_writing())
            return -1;
        if (timeout_ms < 0)
            /* Non-blocking check. Notify that tuple_fifo is empty. */
            return 0;
        /* timed out! */
        return 0;
    }



    /* If we are here, we have pages to read! */
    /* First determine where to find the next page to read. */
    bool page_in_list =
        (_next_read_page_index >= _list_head_page_index) &&
        (_next_read_page_index <= _list_tail_page_index);
    bool page_on_disk =
        (_next_read_page_index >= _file_head_page_index) &&
        (_next_read_page_index <= _file_tail_page_index);
    assert(page_in_list || page_on_disk);



    if (page_in_list) {

        /* page in list */
        /* Make sure list not empty. */
        assert(!_pages.empty());
        assert(_list_head_page_index != -1);
        assert(_list_tail_page_index != -1);

        /* Pull the next page from '_pages' list. */
        _set_read_page(_pages.front());
        _pages.pop_front();
        _add_read_page_to_free_list = true;
        
        _pages_in_memory--;
        _available_fifo_reads--;
        _list_head_page_index++;

        if (_pages.empty()) {
            _list_head_page_index = -1;
            _list_tail_page_index = -1;
        }

        _read_page_index = _next_read_page_index;
        _next_read_page_index++;

        /* Notify the writer if case he is waiting to time-out. */
        if ((_available_memory_writes() >= _threshold) && !is_done_writing())
            ensure_writer_running();

        // * * * END CRITICAL SECTION * * *
        return 1;
    }
    else {

        /* page on disk */
        /* Make sure file not empty. */
        assert(_file_head_page_index != -1);
        assert(_file_tail_page_index != -1);

        
        if (_read_page == SENTINEL_PAGE) {

            /* Allocate a buffer page to read into. Let's try and pull
               this from the _free_pages list. If possible. Pull the
               next page from '_pages' list. */

            bool allocated_using_malloc = _free_pages.empty();
            _read_page = _alloc_page();
            _add_read_page_to_free_list = !allocated_using_malloc;
        }
        else {
            
            /* Otherwise, we have already allocated a page using
               malloc. Just reuse it. */
            _read_page->clear();
        }


        off_t file_page = (_next_read_page_index - _file_head_page_index);
        off_t seek_pos  = file_page * get_default_page_size();
        int seek_ret = lseek(_page_file, seek_pos, SEEK_SET);
        assert(seek_ret != (off_t)-1);
        if (seek_ret == (off_t)-1)
            THROW2(FileException, "seek to %lu", (unsigned long)seek_ret);


        int fread_ret = _read_page->read_full_page(_page_file);
        assert(fread_ret);
        /* Update iterator... */
        _set_read_page(_read_page.release());


        _available_fifo_reads--;
        _read_page_index = _next_read_page_index;
        _next_read_page_index++;
        
        
        if (0) {

            size_t page_size = _read_page->page_size();
            page* pg = _read_page.release();
            unsigned char* pg_bytes = (unsigned char*)pg;

            for (size_t i = 0; i < page_size; i++) {
                printf("%02x", pg_bytes[i]);
                if (i % 2 == 0)
                    printf("\t");
                if (i % 16 == 0)
                    printf("\n");
            } 

            _set_read_page(pg);

            TRACE(TRACE_ALWAYS&TRACE_MASK_DISK, "Read %d %d-byte tuples\n",
                  (int)_read_page->tuple_count(),
                  (int)_read_page->tuple_size());
        }

        
        TRACE(TRACE_TUPLE_FIFO_FILE,
              "FIFO %d: Read page %zd from disk\n",
              _fifo_id,
              _read_page_index);
        return 1;
    }
}



int tuple_fifo_generate_id() {
    static acounter_t next_fifo_id;
    return next_fifo_id.fetch_and_inc();
}



void tuple_fifo::tuple_fifo_state_t::transition(const _tuple_fifo_state_t next) {
    if (_transition_ok(next))
        _value = next;
    else {
        TRACE(TRACE_ALWAYS, "Invalid transition from %s to %s\n",
              to_string().data(),
              state_to_string(next).data());
        assert(0);
    }
}



bool tuple_fifo::tuple_fifo_state_t::_transition_ok(const _tuple_fifo_state_t next) {
    switch(_value) {
    case INVALID:
        /* Only allowed transition should be to IN_MEMORY since we are
           only INVALID in the constructor until we invoke init(). */
        return next == IN_MEMORY;
    case IN_MEMORY:
        return
            (next == IN_MEMORY_DONE_WRITING)
            || (next == ON_DISK)
            || (next == IN_MEMORY_TERMINATED);
    case ON_DISK:
        return
            (next == ON_DISK_DONE_WRITING)
            || (next == ON_DISK_TERMINATED);
    default:
        return false;
    }
}



EXIT_NAMESPACE(qpipe);
