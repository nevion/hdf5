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


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "H5private.h"
#include "h5repack.h"


/*-------------------------------------------------------------------------
 * Function: check_objects
 *
 * Purpose: locate all HDF5 objects in the file and compare with user
 *  supplied list
 *
 * Return: 0, ok, -1 no
 *
 * Programmer: Pedro Vicente, pvn@ncsa.uiuc.edu
 *
 * Date: September, 23, 2003
 *
 *-------------------------------------------------------------------------
 */
int check_objects(const char* fname, 
                  pack_opt_t *options)
{
 hid_t         fid; 
 int           i;
 trav_table_t  *travt=NULL;

/*-------------------------------------------------------------------------
 * open the file 
 *-------------------------------------------------------------------------
 */

 /* disable error reporting */
 H5E_BEGIN_TRY {
 
 /* Open the files */
 if ((fid=H5Fopen(fname,H5F_ACC_RDONLY,H5P_DEFAULT))<0 ){
  printf("h5repack: <%s>: %s\n", fname, H5FOPENERROR );
  exit(1);
 }
 /* enable error reporting */
 } H5E_END_TRY;


/*-------------------------------------------------------------------------
 * get the list of objects in the file
 *-------------------------------------------------------------------------
 */

 /* init table */
 trav_table_init(&travt);

 /* get the list of objects in the file */
 if (h5trav_gettable(fid,travt)<0)
  goto out;

/*-------------------------------------------------------------------------
 * compare with user supplied list
 *-------------------------------------------------------------------------
 */
 
 if (options->verbose)
  printf("Opening file <%s>. Searching for objects to modify...\n",fname);
 
 for ( i = 0; i < options->op_tbl->nelems; i++) 
 {
  char* name=options->op_tbl->objs[i].path;
  if (options->verbose)
   printf(PFORMAT1,"","",name);
  
  /* the input object names are present in the file and are valid */
  if (h5trav_getindext(name,travt)<0)
  {
   printf("\nError: Could not find <%s> in file <%s>. Exiting...\n",
    name,fname);
   goto out;
  }
  if (options->verbose)
   printf("...Found\n");
 }
/*-------------------------------------------------------------------------
 * close
 *-------------------------------------------------------------------------
 */
 H5Fclose(fid);
 trav_table_free(travt);
 return 0;

out:
 H5Fclose(fid);
 trav_table_free(travt);
 return -1;
}



/*-------------------------------------------------------------------------
 * Function: print_objlist
 *
 * Purpose: print list of objects in file
 *
 * Return: void
 *
 * Programmer: Pedro Vicente, pvn@ncsa.uiuc.edu
 *
 * Date: October 23, 2003
 *
 *-------------------------------------------------------------------------
 */
void print_objlist(const char *filename, 
                   int nobjects, 
                   trav_info_t *info )
{
 int i;

 printf("File <%s>: # of entries = %d\n", filename, nobjects );
 for ( i = 0; i < nobjects; i++)
 {
  switch ( info[i].type )
  {
  case H5G_GROUP:
   printf(" %-10s %s\n", "group", info[i].name  );
   break;
  case H5G_DATASET:
   printf(" %-10s %s\n", "dataset", info[i].name );
   break;
  case H5G_TYPE:
   printf(" %-10s %s\n", "datatype", info[i].name );
   break;
  case H5G_LINK:
   printf(" %-10s %s\n", "link", info[i].name );
   break;
  default:
   printf(" %-10s %s\n", "User defined object", info[i].name );
   break;
  }
 }

}

