/*
 * Copyright � 1999-2001 NCSA
 *                       All rights reserved.
 *
 * Programmer:  Robb Matzke <matzke@llnl.gov>
 *              Monday, August  2, 1999
 *
 * Purpose:	The public header file for the mpio driver.
 */
#ifndef H5FDmpio_H
#define H5FDmpio_H

#include "H5FDpublic.h"
#include "H5Ipublic.h"

#ifdef H5_HAVE_PARALLEL
#   define H5FD_MPIO	(H5FD_mpio_init())
#else
#   define H5FD_MPIO	(-1)
#endif /* H5_HAVE_PARALLEL */

/* Type of I/O for data transfer properties */
typedef enum H5FD_mpio_xfer_t {
    H5FD_MPIO_INDEPENDENT = 0, 		/*zero is the default*/
    H5FD_MPIO_COLLECTIVE
} H5FD_mpio_xfer_t;

/*
 * MPIO-specific data transfer properties. This struct is here only because
 * we need it in special case code throughout the library. Applications
 * please use H5Pset_dxpl_mpio() instead.
 */
typedef struct H5FD_mpio_dxpl_t {
    H5FD_mpio_xfer_t	xfer_mode;	/*collective or independent I/O	*/
} H5FD_mpio_dxpl_t;
    
/* Macros */

#define IS_H5FD_MPIO(f)	/* (H5F_t *f) */				    \
    (H5FD_MPIO==H5F_get_driver_id(f))

#ifdef H5_HAVE_PARALLEL
/*Turn on H5FDmpio_debug if H5F_DEBUG is on */
#ifdef H5F_DEBUG
#ifndef H5FDmpio_DEBUG
#define H5FDmpio_DEBUG
#endif
#endif

/* Function prototypes */
#ifdef __cplusplus
extern "C" {
#endif
H5_DLL hid_t H5FD_mpio_init(void);
H5_DLL herr_t H5Pset_fapl_mpio(hid_t fapl_id, MPI_Comm comm, MPI_Info info);
H5_DLL herr_t H5Pget_fapl_mpio(hid_t fapl_id, MPI_Comm *comm/*out*/,
			MPI_Info *info/*out*/);
H5_DLL herr_t H5Pset_dxpl_mpio(hid_t dxpl_id, H5FD_mpio_xfer_t xfer_mode);
H5_DLL herr_t H5Pget_dxpl_mpio(hid_t dxpl_id, H5FD_mpio_xfer_t *xfer_mode/*out*/);
H5_DLL MPI_Comm H5FD_mpio_communicator(H5FD_t *_file);
H5_DLL herr_t H5FD_mpio_setup(hid_t dxpl_id, MPI_Datatype btype,
    MPI_Datatype ftype, unsigned use_view);
H5_DLL herr_t H5FD_mpio_teardown(hid_t dxpl_id);
H5_DLL herr_t H5FD_mpio_wait_for_left_neighbor(H5FD_t *file);
H5_DLL herr_t H5FD_mpio_signal_right_neighbor(H5FD_t *file);
H5_DLL int H5FD_mpio_mpi_rank(H5FD_t *_file);
H5_DLL int H5FD_mpio_mpi_size(H5FD_t *_file);
#ifdef __cplusplus
}
#endif

#endif /* H5_HAVE_PARALLEL */

#endif
