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

/*-------------------------------------------------------------------------
 *
 * Created:		H5Oattribute.c
 *			Dec 11 2006
 *			Quincey Koziol <koziol@hdfgroup.org>
 *
 * Purpose:		Object header attribute routines.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#define H5A_PACKAGE		/*suppress error about including H5Apkg	  */
#define H5O_PACKAGE		/*suppress error about including H5Opkg	  */

/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Apkg.h"		/* Attributes	  			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5MMprivate.h"	/* Memory management			*/
#include "H5Opkg.h"             /* Object headers			*/
#include "H5SMprivate.h"	/* Shared Object Header Messages	*/


/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/

/* User data for iteration when converting attributes to dense storage */
typedef struct {
    H5F_t      *f;              /* Pointer to file for insertion */
    hid_t dxpl_id;              /* DXPL during iteration */
} H5O_iter_cvt_t;

/* User data for iteration when opening an attribute */
typedef struct {
    /* down */
    H5F_t *f;                   /* Pointer to file attribute is in */
    hid_t dxpl_id;              /* DXPL for operation */
    const char *name;           /* Name of attribute to open */

    /* up */
    H5A_t *attr;                /* Attribute data to update object header with */
} H5O_iter_opn_t;

/* User data for iteration when updating an attribute */
typedef struct {
    /* down */
    H5F_t *f;                   /* Pointer to file attribute is in */
    hid_t dxpl_id;              /* DXPL for operation */
    H5A_t *attr;                /* Attribute data to update object header with */

    /* up */
    hbool_t found;              /* Whether the attribute was found */
} H5O_iter_wrt_t;

/* User data for iteration when renaming an attribute */
typedef struct {
    /* down */
    H5F_t *f;                   /* Pointer to file attribute is in */
    hid_t dxpl_id;              /* DXPL for operation */
    const char *old_name;       /* Old name of attribute */
    const char *new_name;       /* New name of attribute */

    /* up */
    hbool_t found;              /* Whether the attribute was found */
} H5O_iter_ren_t;

/* User data for iteration when iterating over attributes */
typedef struct {
    /* down */
    H5F_t *f;                   /* Pointer to file attribute is in */
    hid_t dxpl_id;              /* DXPL for operation */
    hid_t loc_id;               /* ID of object being iterated over */
    unsigned skip;              /* # of attributes to skip over */
    H5A_operator_t op;          /* Callback routine for each attribute */
    void *op_data;              /* User data for callback */

    /* up */
    unsigned count;             /* Count of attributes examined */
} H5O_iter_itr_t;

/* User data for iteration when removing an attribute */
typedef struct {
    /* down */
    H5F_t *f;                   /* Pointer to file attribute is in */
    hid_t dxpl_id;              /* DXPL for operation */
    const char *name;           /* Name of attribute to open */

    /* up */
    hbool_t found;              /* Found attribute to delete */
} H5O_iter_rm_t;


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/


/*********************/
/* Package Variables */
/*********************/


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/



/*-------------------------------------------------------------------------
 * Function:	H5O_attr_to_dense_cb
 *
 * Purpose:	Object header iterator callback routine to convert compact
 *              attributes to dense attributes
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec  4 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_to_dense_cb(H5O_t *oh, H5O_mesg_t *mesg/*in,out*/,
    unsigned UNUSED sequence, unsigned *oh_flags_ptr, void *_udata/*in,out*/)
{
    H5O_iter_cvt_t *udata = (H5O_iter_cvt_t *)_udata;   /* Operator user data */
    H5A_t shared_attr;                  /* Copy of shared attribute */
    H5A_t *attr = NULL;                 /* Pointer to attribute to insert */
    herr_t ret_value = H5_ITER_CONT;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_to_dense_cb)

    /* check args */
    HDassert(oh);
    HDassert(mesg);

    /* Check for shared attribute */
    if(mesg->flags & H5O_MSG_FLAG_SHARED) {
        /* Read the shared attribute in */
        if(NULL == H5O_shared_read(udata->f, udata->dxpl_id, (const H5O_shared_t *)mesg->native, H5O_MSG_ATTR, &shared_attr))
	    HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, H5_ITER_ERROR, "unable to read shared attribute")

        /* Point attribute to insert at shared attribute read in */
        attr = &shared_attr;
    } /* end if */
    else
        attr = (H5A_t *)mesg->native;

    /* Insert attribute into dense storage */
    if(H5A_dense_insert(udata->f, udata->dxpl_id, oh, mesg->flags, attr) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTINSERT, H5_ITER_ERROR, "unable to add to dense storage")

    /* Convert message into a null message in the header */
    /* (don't delete attribute's space in the file though) */
    if(H5O_release_mesg(udata->f, udata->dxpl_id, oh, mesg, FALSE, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTDELETE, H5_ITER_ERROR, "unable to convert into null message")

    /* Indicate that the object header was modified */
    *oh_flags_ptr |= H5AC__DIRTIED_FLAG;

done:
    /* Release copy of shared attribute */
    if(attr == &shared_attr)
        H5O_attr_reset(&shared_attr);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_to_dense_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_create
 *
 * Purpose:	Create a new attribute in the object header.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Friday, December  8, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5O_attr_create(const H5O_loc_t *loc, hid_t dxpl_id, H5A_t *attr)
{
    H5O_t *oh = NULL;                   /* Pointer to actual object header */
    unsigned oh_flags = H5AC__NO_FLAGS_SET;     /* Metadata cache flags for object header */
    unsigned mesg_flags = 0;            /* Flags for storing message */
    htri_t shared_mesg;                 /* Should this message be stored in the Shared Message table? */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_create)
#ifdef QAK
HDfprintf(stderr, "%s: adding attribute, attr->name = '%s'\n", FUNC, attr->name);
#endif /* QAK */

    /* Check arguments */
    HDassert(loc);
    HDassert(attr);

    /* Should this message be written as a SOHM? */
    if((shared_mesg = H5SM_try_share(loc->file, dxpl_id, H5O_ATTR_ID, attr)) > 0) {
        hsize_t attr_rc;                /* Attribute's ref count in shared message storage */

        /* Mark the message as shared */
        mesg_flags |= H5O_MSG_FLAG_SHARED;

        /* Retrieve ref count for shared attribute */
        if(H5SM_get_refcount(loc->file, dxpl_id, H5O_ATTR_ID, &attr->sh_loc, &attr_rc) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't retrieve shared message ref count")

        /* If this is not the first copy of the attribute in the shared message
         *      storage, decrement the reference count on any shared components
         *      of the attribute.  This is done because the shared message
         *      storage's "try delete" call doesn't call the message class's
         *      "delete" callback until the reference count drops to zero.
         *      However, attributes have already increased the reference
         *      count on shared components before passing the attribute
         *      to the shared message code to manage, causing an assymetry
         *      in the reference counting for any shared components.
         *
         *      The alternate solution is to have the shared message's "try
         *      delete" code always call the message class's "delete" callback,
         *      even when the reference count is positive.  This can be done
         *      without an appreciable performance hit (by using H5HF_op() in
         *      the shared message comparison v2 B-tree callback), but it has
         *      the undesirable side-effect of leaving the reference count on
         *      the attribute's shared components artificially (and possibly
         *      misleadingly) high, because there's only one shared attribute
         *      referencing the shared components, not <refcount for the
         *      shared attribute> objects referencing the shared components.
         *
         *      *ick* -QAK, 2007/01/08
         */
        if(attr_rc > 1) {
            if(H5O_attr_delete(loc->file, dxpl_id, attr, TRUE) < 0)
                HGOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, FAIL, "unable to delete attribute")
        } /* end if */
    } /* end if */
    else if(shared_mesg < 0)
	HGOTO_ERROR(H5E_ATTR, H5E_WRITEERROR, FAIL, "error determining if message should be shared")

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_WRITE)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, FAIL, "unable to load object header")

#ifdef QAK
HDfprintf(stderr, "%s: oh->nattrs = %Hu\n", FUNC, oh->nattrs);
HDfprintf(stderr, "%s: oh->max_compact = %u\n", FUNC, oh->max_compact);
HDfprintf(stderr, "%s: oh->min_dense = %u\n", FUNC, oh->min_dense);
#endif /* QAK */
    /* Check for switching to "dense" attribute storage */
    if(oh->version > H5O_VERSION_1 && oh->nattrs == oh->max_compact &&
            !H5F_addr_defined(oh->attr_fheap_addr)) {
        H5O_iter_cvt_t udata;           /* User data for callback */
        H5O_mesg_operator_t op;         /* Wrapper for operator */
#ifdef QAK
HDfprintf(stderr, "%s: converting attributes to dense storage\n", FUNC);
#endif /* QAK */

        /* Create dense storage for attributes */
        if(H5A_dense_create(loc->file, dxpl_id, oh) < 0)
            HGOTO_ERROR(H5E_OHDR, H5E_CANTINIT, FAIL, "unable to create dense storage for attributes")

        /* Set up user data for callback */
        udata.f = loc->file;
        udata.dxpl_id = dxpl_id;

        /* Iterate over existing attributes, moving them to dense storage */
        op.lib_op = H5O_attr_to_dense_cb;
        if(H5O_msg_iterate_real(loc->file, oh, H5O_MSG_ATTR, TRUE, op, &udata, dxpl_id, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTCONVERT, FAIL, "error converting attributes to dense storage")
    } /* end if */

    /* Increment attribute count */
    oh->nattrs++;

    /* Check for storing attribute with dense storage */
    if(H5F_addr_defined(oh->attr_fheap_addr)) {
        /* Insert attribute into dense storage */
#ifdef QAK
HDfprintf(stderr, "%s: inserting attribute to dense storage\n", FUNC);
#endif /* QAK */
        if(H5A_dense_insert(loc->file, dxpl_id, oh, mesg_flags, attr) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTINSERT, FAIL, "unable to add to dense storage")
    } /* end if */
    else {
        /* Append new message to object header */
        if(H5O_msg_append_real(loc->file, dxpl_id, oh, H5O_MSG_ATTR, mesg_flags, 0, attr, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTINSERT, FAIL, "unable to create new attribute in header")
    } /* end else */

    /* Update the modification time, if any */
    if(H5O_touch_oh(loc->file, dxpl_id, oh, FALSE, &oh_flags) < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "unable to update time on object")

done:
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_create() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_open_cb
 *
 * Purpose:	Object header iterator callback routine to open an
 *              attribute stored compactly.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec 11 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_open_cb(H5O_t UNUSED *oh, H5O_mesg_t *mesg/*in,out*/,
    unsigned UNUSED sequence, unsigned UNUSED *oh_flags_ptr, void *_udata/*in,out*/)
{
    H5O_iter_opn_t *udata = (H5O_iter_opn_t *)_udata;   /* Operator user data */
    herr_t ret_value = H5_ITER_CONT;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_open_cb)

    /* check args */
    HDassert(oh);
    HDassert(mesg);
    HDassert(!udata->attr);

    /* Check for shared message */
    if(mesg->flags & H5O_MSG_FLAG_SHARED) {
        H5A_t shared_attr;             /* Copy of shared attribute */

	/*
	 * If the message is shared then then the native pointer points to an
	 * H5O_MSG_SHARED message.  We use that information to look up the real
	 * message in the global heap or some other object header.
	 */
        if(NULL == H5O_shared_read(udata->f, udata->dxpl_id, (const H5O_shared_t *)mesg->native, H5O_MSG_ATTR, &shared_attr))
	    HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, H5_ITER_ERROR, "unable to read shared attribute")

        /* Check for correct attribute message to modify */
        if(HDstrcmp(shared_attr.name, udata->name) == 0) {
            /* Make a copy of the attribute to return */
            if(NULL == (udata->attr = H5A_copy(NULL, &shared_attr)))
                HGOTO_ERROR(H5E_ATTR, H5E_CANTCOPY, H5_ITER_ERROR, "unable to copy attribute")
        } /* end if */

        /* Release copy of shared attribute */
        H5O_attr_reset(&shared_attr);
    } /* end if */
    else {
        /* Check for correct attribute message to modify */
        if(HDstrcmp(((H5A_t *)mesg->native)->name, udata->name) == 0) {
            /* Make a copy of the attribute to return */
            if(NULL == (udata->attr = H5A_copy(NULL, (H5A_t *)mesg->native)))
                HGOTO_ERROR(H5E_ATTR, H5E_CANTCOPY, H5_ITER_ERROR, "unable to copy attribute")
        } /* end if */
    } /* end else */

    /* Set common info, if we found the correct attribute */
    if(udata->attr) {
        /* Stop iterating */
        ret_value = H5_ITER_STOP;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_open_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_open_by_name
 *
 * Purpose:	Open an existing attribute in an object header.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Monday, December 11, 2006
 *
 *-------------------------------------------------------------------------
 */
H5A_t *
H5O_attr_open_by_name(const H5O_loc_t *loc, const char *name, hid_t dxpl_id)
{
    H5O_t *oh = NULL;                   /* Pointer to actual object header */
    unsigned oh_flags = H5AC__NO_FLAGS_SET;     /* Metadata cache flags for object header */
    H5A_t *ret_value;                   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_open_by_name)

    /* Check arguments */
    HDassert(loc);
    HDassert(name);

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_READ)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, NULL, "unable to load object header")

#ifdef QAK
HDfprintf(stderr, "%s: opening attribute to new-style object header\n", FUNC);
HDfprintf(stderr, "%s: oh->nattrs = %Hu\n", FUNC, oh->nattrs);
HDfprintf(stderr, "%s: oh->max_compact = %u\n", FUNC, oh->max_compact);
HDfprintf(stderr, "%s: oh->min_dense = %u\n", FUNC, oh->min_dense);
#endif /* QAK */
    /* Check for opening attribute with dense storage */
    if(H5F_addr_defined(oh->attr_fheap_addr)) {
        /* Open attribute in dense storage */
        if(NULL == (ret_value = H5A_dense_open(loc->file, dxpl_id, oh, name)))
            HGOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, NULL, "can't open attribute")
    } /* end if */
    else {
        H5O_iter_opn_t udata;           /* User data for callback */
        H5O_mesg_operator_t op;         /* Wrapper for operator */

        /* Set up user data for callback */
        udata.f = loc->file;
        udata.dxpl_id = dxpl_id;
        udata.name = name;
        udata.attr = NULL;

        /* Iterate over attributes, to locate correct one to open */
        op.lib_op = H5O_attr_open_cb;
        if(H5O_msg_iterate_real(loc->file, oh, H5O_MSG_ATTR, TRUE, op, &udata, dxpl_id, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, NULL, "error updating attribute")

        /* Check that we found the attribute */
        if(!udata.attr)
            HGOTO_ERROR(H5E_ATTR, H5E_NOTFOUND, NULL, "can't locate attribute")

        /* Get attribute opened from object header */
        HDassert(udata.attr);
        ret_value = udata.attr;
    } /* end else */

done:
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, NULL, "unable to release object header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_open_by_name() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_open_by_idx
 *
 * Purpose:	Callback routine opening an attribute by index
 *
 * Return:	Success:        Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec 18 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_open_by_idx_cb(const H5A_t *attr, void *_ret_attr)
{
    H5A_t **ret_attr = (H5A_t **)_ret_attr;     /* 'User data' passed in */
    herr_t ret_value = H5_ITER_STOP;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_open_by_idx_cb)

    /* check arguments */
    HDassert(attr);
    HDassert(ret_attr);

    /* Copy attribute information */
    if(NULL == (*ret_attr = H5A_copy(NULL, attr)))
        HGOTO_ERROR(H5E_ATTR, H5E_CANTCOPY, H5_ITER_ERROR, "can't copy attribute")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_open_by_idx_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_open_by_idx
 *
 * Purpose:	Open an existing attribute in an object header according to
 *              an index.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Monday, December 18, 2006
 *
 *-------------------------------------------------------------------------
 */
H5A_t *
H5O_attr_open_by_idx(const H5O_loc_t *loc, hsize_t n, hid_t dxpl_id)
{
    H5A_attr_iter_op_t attr_op;         /* Attribute operator */
    H5A_t *ret_value = NULL;            /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_open_by_idx)

    /* Check arguments */
    HDassert(loc);

    /* Build attribute operator info */
    attr_op.op_type = H5A_ATTR_OP_LIB;
    attr_op.u.lib_op = H5O_attr_open_by_idx_cb;

    /* Iterate over attributes to locate correct one */
    if(H5O_attr_iterate((hid_t)-1, loc, dxpl_id, H5_ITER_INC, (unsigned)n, NULL, &attr_op, &ret_value) < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_BADITER, NULL, "can't locate attribute")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_open_by_idx() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_update_shared
 *
 * Purpose:	Update a shared attribute.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Jan  2 2007
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5O_attr_update_shared(H5F_t *f, hid_t dxpl_id, H5A_t *attr, 
    const H5O_shared_t *sh_mesg)
{
    hsize_t attr_rc;                    /* Attribute's ref count in shared message storage */
    htri_t shared_mesg;                 /* Whether the message should be shared */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_update_shared)

    /* check args */
    HDassert(f);
    HDassert(attr);
    HDassert(sh_mesg);

    /* Store new version of message as a SOHM */
    /* (should always work, since we're not changing the size of the attribute) */
    if((shared_mesg = H5SM_try_share(f, dxpl_id, H5O_ATTR_ID, attr)) == 0)
        HGOTO_ERROR(H5E_ATTR, H5E_BADMESG, FAIL, "attribute changed sharing status")
    else if(shared_mesg < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_BADMESG, FAIL, "can't share attribute")

    /* Retrieve shared message storage ref count for new shared attribute */
    if(H5SM_get_refcount(f, dxpl_id, H5O_ATTR_ID, &attr->sh_loc, &attr_rc) < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't retrieve shared message ref count")

    /* If the newly shared attribute needs to share "ownership" of the shared
     *      components (ie. its reference count is 1), increment the reference
     *      count on any shared components of the attribute, so that they won't
     *      be removed from the file.  (Essentially a "copy on write" operation).
     *
     *      *ick* -QAK, 2007/01/08
     */
    if(attr_rc == 1) {
        /* Increment reference count on attribute components */
        if(H5O_attr_link(f, dxpl_id, attr) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_LINKCOUNT, FAIL, "unable to adjust attribute link count")
    } /* end if */

    /* Remove the old attribute from the SOHM storage */
    if(H5SM_try_delete(f, dxpl_id, H5O_ATTR_ID, sh_mesg) < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_CANTFREE, FAIL, "unable to delete shared attribute in shared storage")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_update_shared() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_write_cb
 *
 * Purpose:	Object header iterator callback routine to update an
 *              attribute stored compactly.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec  4 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_write_cb(H5O_t UNUSED *oh, H5O_mesg_t *mesg/*in,out*/,
    unsigned UNUSED sequence, unsigned *oh_flags_ptr, void *_udata/*in,out*/)
{
    H5O_iter_wrt_t *udata = (H5O_iter_wrt_t *)_udata;   /* Operator user data */
    herr_t ret_value = H5_ITER_CONT;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_write_cb)

    /* check args */
    HDassert(oh);
    HDassert(mesg);
    HDassert(!udata->found);

    /* Check for shared message */
    if(mesg->flags & H5O_MSG_FLAG_SHARED) {
        H5A_t shared_attr;             /* Copy of shared attribute */

	/*
	 * If the message is shared then then the native pointer points to an
	 * H5O_MSG_SHARED message.  We use that information to look up the real
	 * message in the global heap or some other object header.
	 */
        if(NULL == H5O_shared_read(udata->f, udata->dxpl_id, (const H5O_shared_t *)mesg->native, H5O_MSG_ATTR, &shared_attr))
	    HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, H5_ITER_ERROR, "unable to read shared attribute")

        /* Check for correct attribute message to modify */
        if(HDstrcmp(shared_attr.name, udata->attr->name) == 0) {
            /* Update the shared attribute in the SOHM storage */
            if(H5O_attr_update_shared(udata->f, udata->dxpl_id, udata->attr, (const H5O_shared_t *)mesg->native) < 0)
                HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, H5_ITER_ERROR, "unable to update attribute in shared storage")

            /* Extract updated shared message info from modified attribute */
            if(NULL == H5O_attr_get_share(udata->attr, (H5O_shared_t *)mesg->native))
                HGOTO_ERROR(H5E_ATTR, H5E_BADMESG, H5_ITER_ERROR, "can't get shared info")

            /* Indicate that we found the correct attribute */
            udata->found = TRUE;
        } /* end if */

        /* Release copy of shared attribute */
        H5O_attr_reset(&shared_attr);
    } /* end if */
    else {
        /* Check for correct attribute message to modify */
        if(HDstrcmp(((H5A_t *)mesg->native)->name, udata->attr->name) == 0) {
            /* Allocate storage for the message's data, if necessary */
            if(((H5A_t *)mesg->native)->data == NULL)
                if(NULL == (((H5A_t *)mesg->native)->data = H5FL_BLK_MALLOC(attr_buf, udata->attr->data_size)))
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, H5_ITER_ERROR, "memory allocation failed")

            /* Copy the data */
            HDmemcpy(((H5A_t *)mesg->native)->data, udata->attr->data, udata->attr->data_size);

            /* Indicate that we found the correct attribute */
            udata->found = TRUE;
        } /* end if */
    } /* end else */

    /* Set common info, if we found the correct attribute */
    if(udata->found) {
        /* Mark message as dirty */
        mesg->dirty = TRUE;

        /* Stop iterating */
        ret_value = H5_ITER_STOP;

        /* Indicate that the object header was modified */
        *oh_flags_ptr |= H5AC__DIRTIED_FLAG;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_write_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_write
 *
 * Purpose:	Write a new value to an attribute.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Monday, December  4, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5O_attr_write(const H5O_loc_t *loc, hid_t dxpl_id, H5A_t *attr)
{
    H5O_t *oh = NULL;                   /* Pointer to actual object header */
    unsigned oh_flags = H5AC__NO_FLAGS_SET;     /* Metadata cache flags for object header */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_write)

    /* Check arguments */
    HDassert(loc);
    HDassert(attr);

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_WRITE)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, FAIL, "unable to load object header")

    /* Check for attributes stored densely */
    if(H5F_addr_defined(oh->attr_fheap_addr)) {
        /* Modify the attribute data in dense storage */
        if(H5A_dense_write(loc->file, dxpl_id, oh, attr) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "error updating attribute")
    } /* end if */
    else {
        H5O_iter_wrt_t udata;           /* User data for callback */
        H5O_mesg_operator_t op;         /* Wrapper for operator */

        /* Set up user data for callback */
        udata.f = loc->file;
        udata.dxpl_id = dxpl_id;
        udata.attr = attr;
        udata.found = FALSE;

        /* Iterate over attributes, to locate correct one to update */
        op.lib_op = H5O_attr_write_cb;
        if(H5O_msg_iterate_real(loc->file, oh, H5O_MSG_ATTR, TRUE, op, &udata, dxpl_id, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "error updating attribute")

        /* Check that we found the attribute */
        if(!udata.found)
            HGOTO_ERROR(H5E_ATTR, H5E_NOTFOUND, FAIL, "can't locate open attribute?")
    } /* end else */

    /* Update the modification time, if any */
    if(H5O_touch_oh(loc->file, dxpl_id, oh, FALSE, &oh_flags) < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "unable to update time on object")

done:
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_write */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_rename_chk_cb
 *
 * Purpose:	Object header iterator callback routine to check for
 *              duplicate name during rename
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec  5 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_rename_chk_cb(H5O_t UNUSED *oh, H5O_mesg_t *mesg/*in,out*/,
    unsigned UNUSED sequence, unsigned UNUSED *oh_flags_ptr, void *_udata/*in,out*/)
{
    H5O_iter_ren_t *udata = (H5O_iter_ren_t *)_udata;   /* Operator user data */
    herr_t ret_value = H5_ITER_CONT;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_rename_chk_cb)

    /* check args */
    HDassert(oh);
    HDassert(mesg);
    HDassert(!udata->found);

    /* Check for shared message */
    if(mesg->flags & H5O_MSG_FLAG_SHARED) {
        H5A_t shared_attr;              /* Copy of shared attribute */

	/*
	 * If the message is shared then then the native pointer points to an
	 * H5O_MSG_SHARED message.  We use that information to look up the real
	 * message in the global heap or some other object header.
	 */
        if(NULL == H5O_shared_read(udata->f, udata->dxpl_id, (const H5O_shared_t *)mesg->native, H5O_MSG_ATTR, &shared_attr))
	    HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, H5_ITER_ERROR, "unable to read shared attribute")

        /* Check for existing attribute with new name */
        if(HDstrcmp(shared_attr.name, udata->new_name) == 0) {
            /* Indicate that we found an existing attribute with the new name*/
            udata->found = TRUE;

            /* Stop iterating */
            ret_value = H5_ITER_STOP;
        } /* end if */

        /* Release copy of shared attribute */
        H5O_attr_reset(&shared_attr);
    } /* end if */
    else {
        /* Check for existing attribute with new name */
        if(HDstrcmp(((H5A_t *)mesg->native)->name, udata->new_name) == 0) {
            /* Indicate that we found an existing attribute with the new name*/
            udata->found = TRUE;

            /* Stop iterating */
            ret_value = H5_ITER_STOP;
        } /* end if */
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_rename_chk_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_rename_mod_cb
 *
 * Purpose:	Object header iterator callback routine to change name of
 *              attribute during rename
 *
 * Note:	This routine doesn't currently allow an attribute to change
 *              its "shared" status, if the name change would cause a size
 *              difference that would put it into a different category.
 *              Something for later...
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec  5 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_rename_mod_cb(H5O_t *oh, H5O_mesg_t *mesg/*in,out*/,
    unsigned UNUSED sequence, unsigned *oh_flags_ptr, void *_udata/*in,out*/)
{
    H5O_iter_ren_t *udata = (H5O_iter_ren_t *)_udata;   /* Operator user data */
    herr_t ret_value = H5_ITER_CONT;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_rename_mod_cb)

    /* check args */
    HDassert(oh);
    HDassert(mesg);
    HDassert(!udata->found);

    /* Check for shared message */
    if(mesg->flags & H5O_MSG_FLAG_SHARED) {
        H5A_t shared_attr;             /* Copy of shared attribute */

	/*
	 * If the message is shared then then the native pointer points to an
	 * H5O_MSG_SHARED message.  We use that information to look up the real
	 * message in the global heap or some other object header.
	 */
        if(NULL == H5O_shared_read(udata->f, udata->dxpl_id, (const H5O_shared_t *)mesg->native, H5O_MSG_ATTR, &shared_attr))
	    HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, H5_ITER_ERROR, "unable to read shared attribute")

        /* Check for correct attribute message to modify */
        if(HDstrcmp(shared_attr.name, udata->old_name) == 0) {
            /* Change the name for the attribute */
            H5MM_xfree(shared_attr.name);
            shared_attr.name = H5MM_xstrdup(udata->new_name);

            /* Mark message as dirty */
            mesg->dirty = TRUE;

            /* Update the shared attribute in the SOHM storage */
            if(H5O_attr_update_shared(udata->f, udata->dxpl_id, &shared_attr, (const H5O_shared_t *)mesg->native) < 0)
                HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, H5_ITER_ERROR, "unable to update attribute in shared storage")

            /* Extract updated shared message info from modified attribute */
            if(NULL == H5O_attr_get_share(&shared_attr, (H5O_shared_t *)mesg->native))
                HGOTO_ERROR(H5E_ATTR, H5E_BADMESG, H5_ITER_ERROR, "can't get shared info")

            /* Indicate that we found the correct attribute */
            udata->found = TRUE;
        } /* end if */

        /* Release copy of shared attribute */
        H5O_attr_reset(&shared_attr);
    } /* end if */
    else {
        /* Find correct attribute message to rename */
        if(HDstrcmp(((H5A_t *)mesg->native)->name, udata->old_name) == 0) {
            /* Change the name for the attribute */
            H5MM_xfree(((H5A_t *)mesg->native)->name);
            ((H5A_t *)mesg->native)->name = H5MM_xstrdup(udata->new_name);

            /* Mark message as dirty */
            mesg->dirty = TRUE;

            /* Check for attribute message changing size */
            if(HDstrlen(udata->new_name) != HDstrlen(udata->old_name)) {
                H5A_t *attr;            /* Attribute to re-add */

                /* Take ownership of the message's native info (the attribute) 
                 *      so any shared objects in the file aren't adjusted (and
                 *      possibly deleted) when the message is released.
                 */
                /* (We do this more complicated sequence of actions because the
                 *      simpler solution of adding the modified attribute first
                 *      and then deleting the old message can re-allocate the
                 *      list of messages during the "add the modified attribute"
                 *      step, invalidating the message pointer we have here - QAK)
                 */
                attr = (H5A_t *)mesg->native;
                mesg->native = NULL;

                /* If the later version of the object header format, decrement attribute */
                /* (must be decremented before call to H5O_release_mesg(),
                 *      so that the sanity checks pass - QAK)
                 */
                if(oh->version > H5O_VERSION_1)
                    oh->nattrs--;

                /* Delete old attribute */
                /* (doesn't decrement the link count on shared components becuase
                 *      the "native" pointer has been reset)
                 */
                if(H5O_release_mesg(udata->f, udata->dxpl_id, oh, mesg, FALSE, FALSE) < 0)
                    HGOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, H5_ITER_ERROR, "unable to release previous attribute")

                /* Increment attribute count */
                /* (must be incremented before call to H5O_msg_append_real(),
                 *      so that the sanity checks pass - QAK)
                 */
                if(oh->version > H5O_VERSION_1)
                    oh->nattrs++;

                /* Append renamed attribute to object header */
                /* (increments the link count on shared components) */
                if(H5O_msg_append_real(udata->f, udata->dxpl_id, oh, H5O_MSG_ATTR, 0, 0, attr, oh_flags_ptr) < 0)
                    HGOTO_ERROR(H5E_ATTR, H5E_CANTINSERT, H5_ITER_ERROR, "unable to relocate renamed attribute in header")

                /* Decrement the link count on shared components */
                /* (to balance all the link count adjustments out) */
                if(H5O_attr_delete(udata->f, udata->dxpl_id, attr, TRUE) < 0)
                    HGOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, H5_ITER_ERROR, "unable to delete attribute")

                /* Release the local copy of the attribute */
                H5O_msg_free_real(H5O_MSG_ATTR, attr);
            } /* end if */

            /* Indicate that we found an existing attribute with the old name */
            udata->found = TRUE;
        } /* end if */
    } /* end else */

    /* Set common info, if we found the correct attribute */
    if(udata->found) {
        /* Stop iterating */
        ret_value = H5_ITER_STOP;

        /* Indicate that the object header was modified */
        *oh_flags_ptr |= H5AC__DIRTIED_FLAG;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_rename_mod_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_rename
 *
 * Purpose:	Rename an attribute.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Tuesday, December  5, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5O_attr_rename(const H5O_loc_t *loc, hid_t dxpl_id, const char *old_name,
    const char *new_name)
{
    H5O_t *oh = NULL;                   /* Pointer to actual object header */
    unsigned oh_flags = H5AC__NO_FLAGS_SET;     /* Metadata cache flags for object header */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_rename)

    /* Check arguments */
    HDassert(loc);
    HDassert(old_name);
    HDassert(new_name);

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_WRITE)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, FAIL, "unable to load object header")

    /* Check for attributes stored densely */
    if(H5F_addr_defined(oh->attr_fheap_addr)) {
        /* Rename the attribute data in dense storage */
        if(H5A_dense_rename(loc->file, dxpl_id, oh, old_name, new_name) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "error updating attribute")
    } /* end if */
    else {
        H5O_iter_ren_t udata;           /* User data for callback */
        H5O_mesg_operator_t op;         /* Wrapper for operator */

        /* Set up user data for callback */
        udata.f = loc->file;
        udata.dxpl_id = dxpl_id;
        udata.old_name = old_name;
        udata.new_name = new_name;
        udata.found = FALSE;

        /* Iterate over attributes, to check if "new name" exists already */
        op.lib_op = H5O_attr_rename_chk_cb;
        if(H5O_msg_iterate_real(loc->file, oh, H5O_MSG_ATTR, TRUE, op, &udata, dxpl_id, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "error updating attribute")

        /* If the new name was found, indicate an error */
        if(udata.found)
            HGOTO_ERROR(H5E_ATTR, H5E_EXISTS, FAIL, "attribute with new name already exists")

        /* Iterate over attributes again, to actually rename attribute with old name */
        op.lib_op = H5O_attr_rename_mod_cb;
        if(H5O_msg_iterate_real(loc->file, oh, H5O_MSG_ATTR, TRUE, op, &udata, dxpl_id, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "error updating attribute")
    } /* end else */

    /* Update the modification time, if any */
    if(H5O_touch_oh(loc->file, dxpl_id, oh, FALSE, &oh_flags) < 0)
        HGOTO_ERROR(H5E_ATTR, H5E_CANTUPDATE, FAIL, "unable to update time on object")

done:
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_rename */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_iterate
 *
 * Purpose:	Iterate over attributes for an object.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Tuesday, December  5, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5O_attr_iterate(hid_t loc_id, const H5O_loc_t *loc, hid_t dxpl_id,
    H5_iter_order_t order, unsigned skip, unsigned *last_attr,
    const H5A_attr_iter_op_t *attr_op, void *op_data)
{
    H5O_t *oh = NULL;                   /* Pointer to actual object header */
    unsigned oh_flags = H5AC__NO_FLAGS_SET;     /* Metadata cache flags for object header */
    H5A_attr_table_t atable = {0, NULL};        /* Table of attributes */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_iterate)

    /* Check arguments */
    HDassert(loc);
    HDassert(attr_op);

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_READ)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, FAIL, "unable to load object header")

    /* Check for attributes stored densely */
    if(oh->version > H5O_VERSION_1 && H5F_addr_defined(oh->attr_fheap_addr)) {
        haddr_t attr_fheap_addr;            /* Address of fractal heap for dense attribute storage */
        haddr_t name_bt2_addr;              /* Address of v2 B-tree for name index on dense attribute storage */

        /* Retrieve the information about dense attribute storage */
        attr_fheap_addr = oh->attr_fheap_addr;
        name_bt2_addr = oh->name_bt2_addr;

        /* Release the object header */
        if(H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")
        oh = NULL;

        /* Iterate over attributes in dense storage */
        if((ret_value = H5A_dense_iterate(loc->file, dxpl_id, loc_id, attr_fheap_addr,
                name_bt2_addr, order, skip, last_attr, attr_op, op_data)) < 0)
            HERROR(H5E_ATTR, H5E_BADITER, "error iterating over attributes");
    } /* end if */
    else {
        /* Build table of attributes for compact storage */
        if(H5A_compact_build_table(loc->file, dxpl_id, oh, H5_INDEX_NAME, H5_ITER_INC, &atable, &oh_flags) < 0)
            HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "error building attribute table")

        /* Release the object header */
        if(H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")
        oh = NULL;

        /* Check for skipping too many attributes */
        if(skip > 0) {
            /* Check for bad starting index */
            if(skip >= atable.nattrs)
                HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid index specified")
        } /* end if */

        /* Iterate over attributes in table */
        if((ret_value = H5A_attr_iterate_table(&atable, skip, last_attr, loc_id, attr_op, op_data)) < 0)
            HERROR(H5E_ATTR, H5E_CANTNEXT, "iteration operator failed");
    } /* end else */

done:
    /* Release resources */
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")
    if(atable.attrs && H5A_attr_release_table(&atable) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_CANTFREE, FAIL, "unable to release attribute table")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_iterate */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_remove_cb
 *
 * Purpose:	Object header iterator callback routine to remove an
 *              attribute stored compactly.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec 11 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_remove_cb(H5O_t *oh, H5O_mesg_t *mesg/*in,out*/,
    unsigned UNUSED sequence, unsigned *oh_flags_ptr, void *_udata/*in,out*/)
{
    H5O_iter_rm_t *udata = (H5O_iter_rm_t *)_udata;   /* Operator user data */
    herr_t ret_value = H5_ITER_CONT;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_remove_cb)

    /* check args */
    HDassert(oh);
    HDassert(mesg);
    HDassert(!udata->found);

    /* Check for shared message */
    if(mesg->flags & H5O_MSG_FLAG_SHARED) {
        H5A_t shared_attr;             /* Copy of shared attribute */

	/*
	 * If the message is shared then then the native pointer points to an
	 * H5O_MSG_SHARED message.  We use that information to look up the real
	 * message in the global heap or some other object header.
	 */
        if(NULL == H5O_shared_read(udata->f, udata->dxpl_id, (const H5O_shared_t *)mesg->native, H5O_MSG_ATTR, &shared_attr))
	    HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, H5_ITER_ERROR, "unable to read shared attribute")

        /* Check for correct attribute message to modify */
        if(HDstrcmp(shared_attr.name, udata->name) == 0)
            /* Indicate that this message is the attribute to be deleted */
            udata->found = TRUE;

        /* Release copy of shared attribute */
        H5O_attr_reset(&shared_attr);
    } /* end if */
    else {
        /* Check for correct attribute message to modify */
        if(HDstrcmp(((H5A_t *)mesg->native)->name, udata->name) == 0)
            /* Indicate that this message is the attribute to be deleted */
            udata->found = TRUE;
    } /* end else */

    /* Check for finding correct message to delete */
    if(udata->found) {
        /* If the later version of the object header format, decrement attribute */
        /* (must be decremented before call to H5O_release_mesg(), in order for
         *      sanity checks to pass - QAK)
         */
        if(oh->version > H5O_VERSION_1)
            oh->nattrs--;

        /* Convert message into a null message (i.e. delete it) */
        if(H5O_release_mesg(udata->f, udata->dxpl_id, oh, mesg, TRUE, TRUE) < 0)
            HGOTO_ERROR(H5E_OHDR, H5E_CANTDELETE, H5_ITER_ERROR, "unable to convert into null message")

        /* Stop iterating */
        ret_value = H5_ITER_STOP;

        /* Indicate that the object header was modified */
        *oh_flags_ptr |= H5AC__DIRTIED_FLAG;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_remove_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_remove
 *
 * Purpose:	Delete an attributes on an object.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Monday, December 11, 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5O_attr_remove(const H5O_loc_t *loc, const char *name, hid_t dxpl_id)
{
    H5O_t *oh = NULL;                   /* Pointer to actual object header */
    unsigned oh_flags = H5AC__NO_FLAGS_SET;     /* Metadata cache flags for object header */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_remove)

    /* Check arguments */
    HDassert(loc);
    HDassert(name);

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_WRITE)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, FAIL, "unable to load object header")

    /* Check for attributes stored densely */
    if(H5F_addr_defined(oh->attr_fheap_addr)) {
        /* Delete attribute from dense storage */
        if(H5A_dense_remove(loc->file, dxpl_id, oh, name) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, FAIL, "unable to delete attribute in dense storage")

        /* Decrement # of attributes on object */
        oh->nattrs--;
    } /* end if */
    else {
        H5O_iter_rm_t udata;            /* User data for callback */
        H5O_mesg_operator_t op;         /* Wrapper for operator */

        /* Set up user data for callback */
        udata.f = loc->file;
        udata.dxpl_id = dxpl_id;
        udata.name = name;
        udata.found = FALSE;

        /* Iterate over attributes, to locate correct one to delete */
        op.lib_op = H5O_attr_remove_cb;
        if(H5O_msg_iterate_real(loc->file, oh, H5O_MSG_ATTR, TRUE, op, &udata, dxpl_id, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, FAIL, "error deleting attribute")

        /* Check that we found the attribute */
        if(!udata.found)
            HGOTO_ERROR(H5E_ATTR, H5E_NOTFOUND, FAIL, "can't locate attribute")
    } /* end else */

    /* Check for shifting from dense storage back to compact storage */
    if(H5F_addr_defined(oh->attr_fheap_addr) && (oh->nattrs == 0 || oh->nattrs < oh->min_dense)) {
        /* Check if there's no more attributes */
        if(oh->nattrs == 0) {
            /* Delete the dense storage */
            if(H5A_dense_delete(loc->file, dxpl_id, oh) < 0)
                HGOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, FAIL, "unable to delete dense attribute storage")
        } /* end if */
        else {
            H5A_attr_table_t atable = {0, NULL};        /* Table of attributes */
            hbool_t can_convert = TRUE;     /* Whether converting to attribute messages is possible */
            size_t u;                       /* Local index */

            /* Build the table of attributes for this object */
            if(H5A_dense_build_table(loc->file, dxpl_id, oh->nattrs, oh->attr_fheap_addr, oh->name_bt2_addr, H5_INDEX_NAME, H5_ITER_NATIVE, &atable) < 0)
                HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "error building attribute table")

            /* Inspect attributes in table for ones that can't be converted back
             * into attribute message form (currently only attributes which
             * can't fit into an object header message)
             */
            for(u = 0; u < oh->nattrs; u++)
                if(H5O_msg_mesg_size(loc->file, H5O_ATTR_ID, &(atable.attrs[u]), (size_t)0) >= H5O_MESG_MAX_SIZE) {
                    can_convert = FALSE;
                    break;
                } /* end if */

            /* If ok, insert attributes as object header messages */
            if(can_convert) {
                /* Iterate over attributes, to put them into header */
                for(u = 0; u < oh->nattrs; u++) {
                    htri_t shared_mesg;     /* Should this message be stored in the Shared Message table? */
                    unsigned mesg_flags = 0;    /* Message flags for attribute */

                    /* Should this message be written as a SOHM? */
                    if((shared_mesg = H5SM_try_share(loc->file, dxpl_id, H5O_ATTR_ID, &(atable.attrs[u]))) > 0)
                        /* Mark the message as shared */
                        mesg_flags |= H5O_MSG_FLAG_SHARED;
                    else if(shared_mesg < 0)
                        HGOTO_ERROR(H5E_OHDR, H5E_WRITEERROR, FAIL, "error determining if message should be shared")

                    /* Insert attribute message into object header */
                    if(H5O_msg_append_real(loc->file, dxpl_id, oh, H5O_MSG_ATTR, mesg_flags, H5O_UPDATE_TIME, &(atable.attrs[u]), &oh_flags) < 0)
                        HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, FAIL, "can't create message")
                } /* end for */

                /* Remove the dense storage */
                if(H5A_dense_delete(loc->file, dxpl_id, oh) < 0)
                    HGOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, FAIL, "unable to delete dense attribute storage")
            } /* end if */

            /* Free attribute table information */
            if(H5A_attr_release_table(&atable) < 0)
                HGOTO_ERROR(H5E_ATTR, H5E_CANTFREE, FAIL, "unable to release attribute table")
        } /* end else */
    } /* end if */

done:
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, oh_flags) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_remove */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_count
 *
 * Purpose:	Determine the # of attributes on an object
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Monday, December 11, 2006
 *
 *-------------------------------------------------------------------------
 */
int
H5O_attr_count(const H5O_loc_t *loc, hid_t dxpl_id)
{
    H5O_t *oh = NULL;                   /* Pointer to actual object header */
    int ret_value;                      /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_count)

    /* Check arguments */
    HDassert(loc);

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_READ)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, FAIL, "unable to load object header")

    /* Check for attributes stored densely */
    if(oh->version > H5O_VERSION_1)
        ret_value = (int)oh->nattrs;
    else {
        unsigned u;             /* Local index variable */

        /* Loop over all messages, counting the attributes */
        for(u = ret_value = 0; u < oh->nmesgs; u++)
            if(oh->mesg[u].type == H5O_MSG_ATTR)
                ret_value++;
    } /* end else */

done:
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, H5AC__NO_FLAGS_SET) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_count */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_exists_cb
 *
 * Purpose:	Object header iterator callback routine to check for an
 *              attribute stored compactly, by name.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Dec 11 2006
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_attr_exists_cb(H5O_t UNUSED *oh, H5O_mesg_t *mesg/*in,out*/,
    unsigned UNUSED sequence, unsigned UNUSED *oh_flags_ptr, void *_udata/*in,out*/)
{
    H5O_iter_rm_t *udata = (H5O_iter_rm_t *)_udata;   /* Operator user data */
    herr_t ret_value = H5_ITER_CONT;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_exists_cb)

    /* check args */
    HDassert(mesg);
    HDassert(!udata->found);

    /* Check for shared message */
    if(mesg->flags & H5O_MSG_FLAG_SHARED) {
        H5A_t shared_attr;             /* Copy of shared attribute */

	/*
	 * If the message is shared then then the native pointer points to an
	 * H5O_MSG_SHARED message.  We use that information to look up the real
	 * message in the global heap or some other object header.
	 */
        if(NULL == H5O_shared_read(udata->f, udata->dxpl_id, (const H5O_shared_t *)mesg->native, H5O_MSG_ATTR, &shared_attr))
	    HGOTO_ERROR(H5E_ATTR, H5E_CANTINIT, H5_ITER_ERROR, "unable to read shared attribute")

        /* Check for correct attribute message */
        if(HDstrcmp(shared_attr.name, udata->name) == 0)
            /* Indicate that this message is the attribute sought */
            udata->found = TRUE;

        /* Release copy of shared attribute */
        H5O_attr_reset(&shared_attr);
    } /* end if */
    else {
        /* Check for correct attribute message */
        if(HDstrcmp(((H5A_t *)mesg->native)->name, udata->name) == 0)
            /* Indicate that this message is the attribute sought */
            udata->found = TRUE;
    } /* end else */

    /* Check for finding correct message to delete */
    if(udata->found) {
        /* Stop iterating */
        ret_value = H5_ITER_STOP;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_exists_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5O_attr_exists
 *
 * Purpose:	Determine if an attribute with a particular name exists on an object
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		Monday, December 11, 2006
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5O_attr_exists(const H5O_loc_t *loc, const char *name, hid_t dxpl_id)
{
    H5O_t *oh = NULL;           /* Pointer to actual object header */
    unsigned oh_flags = H5AC__NO_FLAGS_SET;     /* Metadata cache flags for object header */
    htri_t ret_value;           /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5O_attr_exists)

    /* Check arguments */
    HDassert(loc);
    HDassert(name);

    /* Protect the object header to iterate over */
    if(NULL == (oh = (H5O_t *)H5AC_protect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, NULL, NULL, H5AC_READ)))
	HGOTO_ERROR(H5E_ATTR, H5E_CANTLOAD, FAIL, "unable to load object header")

    /* Check for attributes stored densely */
    if(H5F_addr_defined(oh->attr_fheap_addr)) {
        /* Check if attribute exists in dense storage */
        if((ret_value = H5A_dense_exists(loc->file, dxpl_id, oh, name)) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_BADITER, FAIL, "error checking for existence of attribute")
    } /* end if */
    else {
        H5O_iter_rm_t udata;            /* User data for callback */
        H5O_mesg_operator_t op;         /* Wrapper for operator */

        /* Set up user data for callback */
        udata.f = loc->file;
        udata.dxpl_id = dxpl_id;
        udata.name = name;
        udata.found = FALSE;

        /* Iterate over existing attributes, checking for attribute with same name */
        op.lib_op = H5O_attr_exists_cb;
        if(H5O_msg_iterate_real(loc->file, oh, H5O_MSG_ATTR, TRUE, op, &udata, dxpl_id, &oh_flags) < 0)
            HGOTO_ERROR(H5E_ATTR, H5E_BADITER, FAIL, "error checking for existence of attribute")

        /* Check that we found the attribute */
        ret_value = udata.found;
    } /* end else */

done:
    if(oh && H5AC_unprotect(loc->file, dxpl_id, H5AC_OHDR, loc->addr, oh, H5AC__NO_FLAGS_SET) < 0)
        HDONE_ERROR(H5E_ATTR, H5E_PROTECT, FAIL, "unable to release object header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5O_attr_exists */

