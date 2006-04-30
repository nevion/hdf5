/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdf.ncsa.uiuc.edu/HDF5/doc/Copyright.html.  If you do not have     *
 * access to either file, you may request a copy from hdfhelp@ncsa.uiuc.edu. *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Programmer:  Quincey Koziol <koziol@ncsa.uiuc.edu>
 *              Tuesday, March  7, 2006
 *
 * Purpose:	Fractal heap free space functions.
 *
 */

/****************/
/* Module Setup */
/****************/

#define H5HF_PACKAGE		/*suppress error about including H5HFpkg  */

/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5HFpkg.h"		/* Fractal heaps			*/
#include "H5Vprivate.h"		/* Vectors and arrays 			*/

/****************/
/* Local Macros */
/****************/

/* Max. height of the skip list holding free list nodes */
#define H5HF_FLIST_DEFAULT_SKIPLIST_HEIGHT     16

/******************/
/* Local Typedefs */
/******************/

/* Structure for storing single free list section */
typedef struct H5HF_flist_single_t {
    void *node;                 /* Pointer to section's node */
    size_t *size_key;           /* Pointer to size key for section */
    haddr_t *addr_key;          /* Pointer to address key for section */
} H5HF_flist_single_t;

/* Free list node for free list sections of the same size */
typedef struct H5HF_flist_node_t {
    size_t sec_size;            /* Size of all sections on list */
    H5SL_t *sec_list;           /* Skip list to hold pointers to actual free list section node */
} H5HF_flist_node_t;


/********************/
/* Package Typedefs */
/********************/

/* Main free list info */
struct H5HF_freelist_t {
    hsize_t tot_space;          /* Total amount of space in free list         */
    size_t sec_count;           /* # of sections on free list                 */
    H5HF_flist_single_t single; /* Section information when free list has only one free section */
    unsigned nbins;             /* Number of bins                             */
    H5SL_operator_t node_free_op;       /* Callback for freeing nodes when free list is destroyed */
    H5SL_t **bins;              /* Pointer to array of lists of free nodes    */
    hbool_t using_bins;         /* Flag to indicate that all nodes are in the bins */
};


/********************/
/* Local Prototypes */
/********************/
static herr_t H5HF_flist_node_free_cb(void *item, void *key, void *op_data);
static herr_t H5HF_flist_init(H5HF_freelist_t *flist);


/*********************/
/* Package Variables */
/*********************/

/* Declare a free list to manage the H5HF_freelist_t struct */
H5FL_DEFINE_STATIC(H5HF_freelist_t);

/* Declare a free list to manage the H5HF_flist_node_t struct */
H5FL_DEFINE_STATIC(H5HF_flist_node_t);

/* Declare a free list to manage the 'H5SL_t *' sequence information */
typedef H5SL_t *H5SL_ptr_t;
H5FL_SEQ_DEFINE_STATIC(H5SL_ptr_t);


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/



/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_init
 *
 * Purpose:	Initialize free list for heap
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, April 18, 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HF_flist_init(H5HF_freelist_t *flist)
{
    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5HF_flist_init)

    /* Check arguments. */
    HDassert(flist);

    /* Set free list parameters */
    flist->tot_space = 0;
    flist->sec_count = 0;
    flist->single.node = NULL;
    flist->single.size_key = NULL;
    flist->single.addr_key = NULL;
    flist->bins = NULL;
    flist->using_bins = FALSE;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5HF_flist_init() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_create
 *
 * Purpose:	Allocate & initialize free list for heap
 *
 * Return:	Success:	Pointer to free list structure
 *
 *		Failure:	NULL
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, March  7, 2006
 *
 *-------------------------------------------------------------------------
 */
H5HF_freelist_t *
H5HF_flist_create(unsigned max_index_bits, H5SL_operator_t node_free_op)
{
    H5HF_freelist_t *flist;             /* New free list structure */
    H5HF_freelist_t *ret_value;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_flist_create)

    /* Check arguments. */

    /*
     * Allocate top free list structure
     */
    if(NULL == (flist = H5FL_MALLOC(H5HF_freelist_t)))
	HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed for fractal heap free list")

    /* Set immutable free list parameters */
    flist->nbins = max_index_bits;
    flist->node_free_op = node_free_op;

    /* Set modifiable free list parameters */
    H5HF_flist_init(flist);

    /* Set return value */
    ret_value = flist;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5HF_flist_create() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_add_bin_node
 *
 * Purpose:	Add a section of free space in a direct block to the free list
 *              bins
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Monday, March 20, 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HF_flist_add_bin_node(H5HF_freelist_t *flist, void *node, size_t *size_key, haddr_t *addr_key)
{
    H5HF_flist_node_t *flist_node = NULL;   /* Pointer to free list node of the correct size */
    unsigned bin;                       /* Bin to put the free space section in */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_flist_add_bin_node)
#ifdef QAK
HDfprintf(stderr, "%s: *size_key = %Zu, *addr_key = %a\n", FUNC, *size_key, *addr_key);
#endif /* QAK */

    /* Check arguments. */
    HDassert(flist);
    HDassert(node);
    HDassert(size_key);
    HDassert(addr_key);

    /* Determine correct bin which holds items of the section's size */
    bin = H5V_log2_gen((hsize_t)*size_key);
    HDassert(bin < flist->nbins);
    if(flist->bins[bin] == NULL) {
        if(NULL == (flist->bins[bin] = H5SL_create(H5SL_TYPE_SIZE, 0.5, H5HF_FLIST_DEFAULT_SKIPLIST_HEIGHT)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTCREATE, FAIL, "can't create skip list for free list nodes")
    } /* end if */
    else {
        /* Check for node list of the correct size already */
        flist_node = H5SL_search(flist->bins[bin], size_key);
    } /* end else */

    /* Check if we need to create a new skip list for nodes of this size */
    if(flist_node == NULL) {
        /* Allocate new free list size node */
        if(NULL == (flist_node = H5FL_MALLOC(H5HF_flist_node_t)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "memory allocation failed for fractal heap free list")

        /* Initialize the free list size node */
        flist_node->sec_size = *size_key;
        if(NULL == (flist_node->sec_list = H5SL_create(H5SL_TYPE_HADDR, 0.5, H5HF_FLIST_DEFAULT_SKIPLIST_HEIGHT)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTCREATE, FAIL, "can't create skip list for free list nodes")

        /* Insert new free list size node into bin's list */
        if(H5SL_insert(flist->bins[bin], flist_node, &flist_node->sec_size) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTINSERT, FAIL, "can't insert free list node into skip list")
    } /* end if */

    /* Insert free list node into correct skip list */
    if(H5SL_insert(flist_node->sec_list, node, addr_key) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTINSERT, FAIL, "can't insert free list node into skip list")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5HF_flist_add_bin_node() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_add
 *
 * Purpose:	Add a section of free space in a direct block to the free list
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, March  7, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_flist_add(H5HF_freelist_t *flist, void *node, size_t *size_key, haddr_t *addr_key)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_flist_add)

#ifdef QAK
HDfprintf(stderr, "%s: *size_key = %Zu, *addr_key = %a\n", FUNC, *size_key, *addr_key);
#endif /* QAK */

    /* Check arguments. */
    HDassert(flist);
    HDassert(node);
    HDassert(size_key);
    HDassert(addr_key);

    /* Check for special cases of # of sections on free list */
#ifdef QAK
HDfprintf(stderr, "%s: flist->sec_count = %Zu\n", FUNC, flist->sec_count);
#endif /* QAK */
    if(flist->sec_count == 0) {
        HDassert(flist->single.node == NULL);

        /* Capture single section's information */
        flist->single.node = node;
        flist->single.size_key = size_key;
        flist->single.addr_key = addr_key;
    } /* end if */
    else {
        /* Have a single section, put it into the bins */
/* XXX: Take out the "&& !flist->using_bins" when bins converted back into single section */
         if(flist->sec_count == 1 && !flist->using_bins) {
            HDassert(flist->single.node);

            /* Check if we should allocate the bins */
            if(flist->bins == NULL)
                /* Allocate the bins for free space sizes */
                if(NULL == (flist->bins = H5FL_SEQ_CALLOC(H5SL_ptr_t, flist->nbins)))
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "memory allocation failed for free list bins")

            /* Insert the current single section into the bins */
            if(H5HF_flist_add_bin_node(flist, flist->single.node, flist->single.size_key, flist->single.addr_key) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTINSERT, FAIL, "can't insert free list node into skip list")
            flist->single.node = NULL;

            /* Using bins for storing nodes now */
            flist->using_bins = TRUE;
        } /* end if */
        HDassert(flist->single.node == NULL);

        /* Put new section into bins */
        if(H5HF_flist_add_bin_node(flist, node, size_key, addr_key) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTINSERT, FAIL, "can't insert free list node into skip list")
    } /* end else */

    /* Increment # of sections on free list */
    flist->sec_count++;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5HF_flist_add() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_find_bin_node
 *
 * Purpose:	Locate a section of free space (in existing free list bins) that
 *              is large enough to fulfill request.
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Monday, March 20, 2006
 *
 *-------------------------------------------------------------------------
 */
static htri_t
H5HF_flist_find_bin_node(H5HF_freelist_t *flist, size_t request, void **node)
{
    H5HF_flist_node_t *flist_node;      /* Free list size node */
    unsigned bin;                       /* Bin to put the free space section in */
    htri_t ret_value = FALSE;           /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_flist_find_bin_node)

    /* Check arguments. */
    HDassert(flist);
    HDassert(request > 0);
    HDassert(node);

    /* Determine correct bin which holds items of at least the section's size */
    bin = H5V_log2_gen((hsize_t)request);
    HDassert(bin < flist->nbins);
    while(bin < flist->nbins && flist->bins[bin] == NULL)
        bin++;

    /* Find the first free space section that is large enough to fulfill request */
    /* (Since the bins use skip lists to track the sizes of the address-ordered
     *  lists, this is actually a "best fit" algorithm)
     */
#ifdef QAK
HDfprintf(stderr, "%s: flist->nbins = %u\n", FUNC, flist->nbins);
HDfprintf(stderr, "%s: bin = %u\n", FUNC, bin);
#endif /* QAK */
    if(bin < flist->nbins)
        do {
            /* Look for large enough free list section in this bin */
            if(flist->bins[bin])
                /* Check for large enough list of sections on list */
                if((flist_node = H5SL_greater(flist->bins[bin], &request))) {
                    /* Take first node off of the list (ie. node w/lowest address) */
                    if(NULL == (*node = H5SL_remove_first(flist_node->sec_list)))
                        HGOTO_ERROR(H5E_HEAP, H5E_CANTDELETE, FAIL, "can't remove free list node from skip list")

                    /* Check for no more nodes on list of that size */
                    if(H5SL_count(flist_node->sec_list) == 0) {
                        H5HF_flist_node_t *tmp_flist_node;      /* Free list size node */

                        /* Remove size tracking list from bin */
                        tmp_flist_node = H5SL_remove(flist->bins[bin], &flist_node->sec_size);
                        if(tmp_flist_node == NULL || tmp_flist_node != flist_node)
                            HGOTO_ERROR(H5E_HEAP, H5E_CANTDELETE, FAIL, "can't remove free list node from skip list")

                        /* Destroy skip list for size tracking node */
                        if(H5SL_close(flist_node->sec_list) < 0)
                            HGOTO_ERROR(H5E_HEAP, H5E_CANTCLOSEOBJ, FAIL, "can't destroy size tracking node's skip list")

                        /* Release free list node */
                        H5FL_FREE(H5HF_flist_node_t, flist_node);
                    } /* end if */

                    HGOTO_DONE(TRUE)
                } /* end if */

            /* Advance to next larger bin */
            bin++;
        } while(bin < flist->nbins);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5HF_flist_find_bin_node() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_find
 *
 * Purpose:	Locate a section of free space (in existing free list) that
 *              is large enough to fulfill request.
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, March  7, 2006
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5HF_flist_find(H5HF_freelist_t *flist, size_t request, void **node)
{
    htri_t ret_value = FALSE;           /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_flist_find)

#ifdef QAK
HDfprintf(stderr, "%s: request = %Zu\n", FUNC, request);
#endif /* QAK */

    /* Check arguments. */
    HDassert(flist);
    HDassert(request > 0);
    HDassert(node);

    /* Check for any sections on free list */
#ifdef QAK
HDfprintf(stderr, "%s: flist->sec_count = %Zu\n", FUNC, flist->sec_count);
#endif /* QAK */
    if(flist->sec_count > 0) {
        /* Check for single section */
/* XXX: Take out the "&& !flist->using_bins" when bins converted back into single section */
        if(flist->sec_count == 1 && !flist->using_bins) {
            HDassert(flist->single.node);

            /* See if single section is large enough */
            if(*(flist->single.size_key) >= request) {
                *node = flist->single.node;
                flist->single.node = NULL;
                ret_value = TRUE;
            } /* end if */
            else
                HGOTO_DONE(FALSE)
        } /* end if */
        else {
            HDassert(flist->single.node == NULL);

            /* Look for node in bins */
            if((ret_value = H5HF_flist_find_bin_node(flist, request, node)) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "can't remove section from bins")
        } /* end else */

        /* Decrement # of sections on free list, if we found an object */
        if(ret_value > 0) {
            flist->sec_count--;
/* XXX: Should check for only one section in bins & convert to single section
 *      This is somewhat hard because we "lose" the the size & address keys
 *      (The address key is actually available, but the size key is gone, unless
 *      we start tracking it.
 *
 *      Drop back to using a "single" node when the bins are empty.
 */
            if(flist->sec_count == 0)
                flist->using_bins = FALSE;
        } /* end if */
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5HF_flist_find() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_node_free_cb
 *
 * Purpose:	Free a size-tracking node for a bin
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Saturday, March 11, 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HF_flist_node_free_cb(void *item, void UNUSED *key, void *op_data)
{
    H5HF_flist_node_t *flist_node = (H5HF_flist_node_t *)item;       /* Temporary pointer to free list node */

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5HF_flist_node_free_cb)

    HDassert(flist_node);

    /* Release the skip list for sections of this size */
    H5SL_destroy(flist_node->sec_list, (H5SL_operator_t)op_data, NULL);

    /* Release free list node */
    H5FL_FREE(H5HF_flist_node_t, flist_node);

    FUNC_LEAVE_NOAPI(0)
}   /* H5HF_flist_node_free_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_reset
 *
 * Purpose:	Reset free list structure by freeing all existing sections
 *              and restoring free list info to initial conditions.
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, April 18, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_flist_reset(H5HF_freelist_t *flist)
{
    unsigned u;                 /* Local index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5HF_flist_reset)

    /* Check arguments. */
    HDassert(flist);

    /* Check for single section to free */
/* XXX: Take out the "&& !flist->using_bins" when bins converted back into single section */
    if(flist->sec_count == 1 && !flist->using_bins) {
        HDassert(flist->single.node != NULL);
        flist->node_free_op(flist->single.node, flist->single.addr_key, NULL);
        flist->single.node = NULL;
    } /* end if */
    HDassert(flist->single.node == NULL);

    /* Release bins for skip lists */
    if(flist->bins) {
        /* Clear out lists of nodes */
        for(u = 0; u < flist->nbins; u++)
            if(flist->bins[u]) {
                H5SL_destroy(flist->bins[u], H5HF_flist_node_free_cb, (void *)flist->node_free_op);
                flist->bins[u] = NULL;
            } /* end if */

        H5FL_SEQ_FREE(H5SL_ptr_t, flist->bins);
    } /* end if */

    /* Reset free list info back to initial state */
    H5HF_flist_init(flist);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5HF_flist_reset() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_flist_free
 *
 * Purpose:	Destroy & deallocate free list structure
 *
 * Return:	Success:	non-negative
 *
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, March  7, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_flist_free(H5HF_freelist_t *flist)
{
    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5HF_flist_free)

    /* Check arguments. */
    HDassert(flist);

    /* Reset free list information */
    H5HF_flist_reset(flist);

    /* Free fractal heap free list info */
    H5FL_FREE(H5HF_freelist_t, flist);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5HF_flist_free() */

