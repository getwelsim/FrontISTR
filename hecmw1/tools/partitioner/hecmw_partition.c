/*=====================================================================*
 *                                                                     *
 *   Software Name : HEC-MW Library for PC-cluster                     *
 *         Version : 2.3                                               *
 *                                                                     *
 *     Last Update : 2006/06/01                                        *
 *        Category : HEC-MW Utility                                    *
 *                                                                     *
 *            Written by Shin'ichi Ezure (RIST)                        *
 *                                                                     *
 *     Contact address :  IIS,The University of Tokyo RSS21 project    *
 *                                                                     *
 *     "Structural Analysis System for General-purpose Coupling        *
 *      Simulations Using High End Computing Middleware (HEC-MW)"      *
 *                                                                     *
 *=====================================================================*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <math.h>

#include "hecmw_util.h"
#include "hecmw_common.h"
#include "hecmw_io.h"

#include "hecmw_part_define.h"
#include "hecmw_part_struct.h"
#include "hecmw_part_log.h"
#include "hecmw_mesh_hash_sort.h"
#include "hecmw_mesh_edge_info.h"
#include "hecmw_part_get_control.h"
#include "hecmw_partition.h"
#include "hecmw_ucd_print.h"

#ifdef HECMW_PART_WITH_METIS
#  include "metis.h"
#endif


#define INTERNAL  1


#define EXTERNAL  2


#define BOUNDARY  4


#define OVERLAP   8


#define MASK     16


#define MARK     32


#define MY_DOMAIN       1


#define NEIGHBOR_DOMAIN 2


#define MPC_BLOCK       4


#define CANDIDATE       8


#define EPS    (1.0E-12)


#define F_1_2  (0.5)


#define F_6_10 (0.6)


#define QSORT_LOWER 50


#define MASK_BIT( map, bit )  ((map) |= (bit))


#define EVAL_BIT( map, bit )  ((map) & (bit))


#define INV_BIT( map, bit )   ((map) ^= (bit))


#define CLEAR_BIT( map, bit ) ((map) |= (bit)) ; ((map) ^= (bit))


#define DSWAP( a, aa ) atemp=(a);(a)=(aa);(aa)=atemp;


#define ISWAP( b, bb ) btemp=(b);(b)=(bb);(bb)=btemp;


#define RTC_NORMAL 0


#define RTC_ERROR (-1)


#define RTC_WARN   1


#define MAX_NODE_SIZE 20


struct link_unit {

    int id;

    struct link_unit *next;
};


struct link_list {

    int n;

    struct link_unit *list;

    struct link_unit *last;
};


/*================================================================================================*/

static char *
get_dist_file_name( char *header, int domain, char *fname )
{
    char s_domain[HECMW_NAME_LEN+1];

    sprintf( s_domain, "%d", domain );

    strcpy( fname, header );
    strcat( fname, "." );
    strcat( fname, s_domain );

    return fname;
}


static void
free_link_list( struct link_unit *llist )
{
    struct link_unit *p, *q;

    for( p=llist; p; p=q ) {
        q = p->next;
        HECMW_free( p );
    }
    llist = NULL;
}


/*================================================================================================*/

static int
init_struct_global( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    memset( local_mesh->gridfile, 0, HECMW_NAME_LEN+1 );
    local_mesh->hecmw_n_file = 0;
    local_mesh->files        = NULL;
    memset( local_mesh->header, 0, HECMW_HEADER_LEN+1 );

    local_mesh->hecmw_flag_adapt     = 0;
    local_mesh->hecmw_flag_initcon   = 0;
    local_mesh->hecmw_flag_parttype  = 0;
    local_mesh->hecmw_flag_partdepth = 0;
    local_mesh->hecmw_flag_version   = 0;

    local_mesh->zero_temp = 0.0;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_node( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    local_mesh->n_node             = 0;
    local_mesh->n_node_gross       = 0;
    local_mesh->nn_internal        = 0;
    local_mesh->node_internal_list = NULL;

    local_mesh->node           = NULL;
    local_mesh->node_ID        = NULL;
    local_mesh->global_node_ID = NULL;

    local_mesh->n_dof          = 0;
    local_mesh->n_dof_grp      = 0;
    local_mesh->node_dof_index = NULL;
    local_mesh->node_dof_item  = NULL;

    local_mesh->node_val_index = NULL;
    local_mesh->node_val_item  = NULL;

    local_mesh->node_init_val_index = NULL;
    local_mesh->node_init_val_item  = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_elem( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    local_mesh->n_elem             = 0;
    local_mesh->n_elem_gross       = 0;
    local_mesh->ne_internal        = 0;
    local_mesh->elem_internal_list = NULL;

    local_mesh->elem_ID        = NULL;
    local_mesh->global_elem_ID = NULL;

    local_mesh->n_elem_type     = 0;
    local_mesh->elem_type       = NULL;
    local_mesh->elem_type_index = NULL;
    local_mesh->elem_type_item  = NULL;

    local_mesh->elem_node_index = NULL;
    local_mesh->elem_node_item  = NULL;

    local_mesh->section_ID = NULL;

    local_mesh->n_elem_mat_ID     = 0;
    local_mesh->elem_mat_ID_index = NULL;
    local_mesh->elem_mat_ID_item  = NULL;

    local_mesh->elem_mat_int_index = NULL;
    local_mesh->elem_mat_int_val   = NULL;

    local_mesh->elem_val_index = NULL;
    local_mesh->elem_val_item  = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_comm( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    local_mesh->zero        = 0;
    local_mesh->PETOT       = 0;
    local_mesh->PEsmpTOT    = 0;
    local_mesh->my_rank     = 0;
    local_mesh->errnof      = 0;
    local_mesh->n_subdomain = 0;

    local_mesh->n_neighbor_pe = 0;
    local_mesh->neighbor_pe   = NULL;

    local_mesh->import_index = NULL;
    local_mesh->import_item  = NULL;
    local_mesh->export_index = NULL;
    local_mesh->export_item  = NULL;
    local_mesh->shared_index = NULL;
    local_mesh->shared_item  = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_adapt( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    local_mesh->coarse_grid_level       = 0;
    local_mesh->n_adapt                 = 0;
    local_mesh->when_i_was_refined_node = NULL;
    local_mesh->when_i_was_refined_elem = NULL;
    local_mesh->adapt_parent_type       = NULL;
    local_mesh->adapt_type              = NULL;
    local_mesh->adapt_level             = NULL;
    local_mesh->adapt_parent            = NULL;
    local_mesh->adapt_children_index    = NULL;
    local_mesh->adapt_children_item     = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_sect( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }
    if( local_mesh->section == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->section\' is NULL" );
        goto error;
    }

    local_mesh->section->n_sect            = 0;
    local_mesh->section->sect_type         = NULL;
    local_mesh->section->sect_opt          = NULL;
    local_mesh->section->sect_mat_ID_index = NULL;
    local_mesh->section->sect_mat_ID_item  = NULL;
    local_mesh->section->sect_I_index      = NULL;
    local_mesh->section->sect_I_item       = NULL;
    local_mesh->section->sect_R_index      = NULL;
    local_mesh->section->sect_R_item       = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_mat( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }
    if( local_mesh->material == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->material\' is NULL" );
        goto error;
    }

    local_mesh->material->n_mat             = 0;
    local_mesh->material->n_mat_item        = 0;
    local_mesh->material->n_mat_subitem     = 0;
    local_mesh->material->n_mat_table       = 0;
    local_mesh->material->mat_name          = NULL;
    local_mesh->material->mat_item_index    = NULL;
    local_mesh->material->mat_subitem_index = NULL;
    local_mesh->material->mat_table_index   = NULL;
    local_mesh->material->mat_val           = NULL;
    local_mesh->material->mat_temp          = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_mpc( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        return -1;
    }
    if( local_mesh->mpc == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->mpc\' is NULL" );
        goto error;
    }

    local_mesh->mpc->n_mpc     = 0;
    local_mesh->mpc->mpc_index = NULL;
    local_mesh->mpc->mpc_item  = NULL;
    local_mesh->mpc->mpc_dof   = NULL;
    local_mesh->mpc->mpc_val   = NULL;
    local_mesh->mpc->mpc_const = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_amp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    if( local_mesh->amp == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->amp\' is NULL" );
        goto error;
    }

    local_mesh->amp->n_amp               = 0;
    local_mesh->amp->amp_name            = NULL;
    local_mesh->amp->amp_type_definition = NULL;
    local_mesh->amp->amp_type_time       = NULL;
    local_mesh->amp->amp_type_value      = NULL;
    local_mesh->amp->amp_index           = NULL;
    local_mesh->amp->amp_val             = NULL;
    local_mesh->amp->amp_table           = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_node_grp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    if( local_mesh->node_group == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->node_group\' is NULL" );
        goto error;
    }

    local_mesh->node_group->n_grp     = 0;
    local_mesh->node_group->grp_name  = NULL;
    local_mesh->node_group->grp_index = NULL;
    local_mesh->node_group->grp_item  = NULL;

    local_mesh->node_group->n_bc         = 0;
    local_mesh->node_group->bc_grp_ID    = 0;
    local_mesh->node_group->bc_grp_type  = 0;
    local_mesh->node_group->bc_grp_index = 0;
    local_mesh->node_group->bc_grp_dof   = 0;
    local_mesh->node_group->bc_grp_val   = 0;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_elem_grp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    if( local_mesh->elem_group == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->elem_group\' is NULL" );
        goto error;
    }

    local_mesh->elem_group->n_grp     = 0;
    local_mesh->elem_group->grp_name  = NULL;
    local_mesh->elem_group->grp_index = NULL;
    local_mesh->elem_group->grp_item  = NULL;

    local_mesh->elem_group->n_bc         = 0;
    local_mesh->elem_group->bc_grp_ID    = NULL;
    local_mesh->elem_group->bc_grp_type  = NULL;
    local_mesh->elem_group->bc_grp_index = NULL;
    local_mesh->elem_group->bc_grp_val   = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_surf_grp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    if( local_mesh->surf_group == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->surf_group\' is NULL" );
        goto error;
    }

    local_mesh->surf_group->n_grp     = 0;
    local_mesh->surf_group->grp_name  = NULL;
    local_mesh->surf_group->grp_index = NULL;
    local_mesh->surf_group->grp_item  = NULL;

    local_mesh->surf_group->n_bc         = 0;
    local_mesh->surf_group->bc_grp_ID    = NULL;
    local_mesh->surf_group->bc_grp_type  = NULL;
    local_mesh->surf_group->bc_grp_index = NULL;
    local_mesh->surf_group->bc_grp_val   = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
init_struct_contact_pair( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    if( local_mesh->contact_pair == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh->contact_pair\' is NULL" );
        goto error;
    }

    local_mesh->contact_pair->n_pair = 0;
    local_mesh->contact_pair->name = NULL;
    local_mesh->contact_pair->type = NULL;
    local_mesh->contact_pair->slave_grp_id = NULL;
    local_mesh->contact_pair->master_grp_id = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

#if 0
static int
init_struct_local_mesh( struct hecmwST_local_mesh *local_mesh )
{
    int rtc;

    if( local_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'local_mesh\' is NULL" );
        goto error;
    }

    rtc = init_struct_global( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_node( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_elem( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_comm( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_adapt( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_sect( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_mat( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_mpc( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_amp( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_node_grp( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_elem_grp( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = init_struct_surf_grp( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}
#endif
/*================================================================================================*/

static void
clean_struct_global( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;

    init_struct_global( local_mesh );
}


static void
clean_struct_node( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;


    if( local_mesh->node_internal_list ) {
        HECMW_free( local_mesh->node_internal_list );
    }
    if( local_mesh->node ) {
        HECMW_free( local_mesh->node );
    }
    if( local_mesh->node_ID ) {
        HECMW_free( local_mesh->node_ID );
    }
    if( local_mesh->global_node_ID ) {
        HECMW_free( local_mesh->global_node_ID );
    }
    if( local_mesh->node_dof_index ) {
        HECMW_free( local_mesh->node_dof_index );
    }
    if( local_mesh->node_init_val_index ) {
        HECMW_free( local_mesh->node_init_val_index );
    }
    if( local_mesh->node_init_val_item ) {
        HECMW_free( local_mesh->node_init_val_item );
    }

    init_struct_node( local_mesh );
}


static void
clean_struct_elem( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;

    if( local_mesh->elem_internal_list ) {
        HECMW_free( local_mesh->elem_internal_list );
    }
    if( local_mesh->elem_ID ) {
        HECMW_free( local_mesh->elem_ID );
    }
    if( local_mesh->global_elem_ID ) {
        HECMW_free( local_mesh->global_elem_ID );
    }
    if( local_mesh->elem_type ) {
        HECMW_free( local_mesh->elem_type );
    }
    if( local_mesh->elem_type_index ) {
        HECMW_free( local_mesh->elem_type_index );
    }
    if( local_mesh->elem_node_index ) {
        HECMW_free( local_mesh->elem_node_index );
    }
    if( local_mesh->elem_node_item ) {
        HECMW_free( local_mesh->elem_node_item );
    }
    if( local_mesh->section_ID ) {
        HECMW_free( local_mesh->section_ID );
    }
    if( local_mesh->elem_mat_ID_index ) {
        HECMW_free( local_mesh->elem_mat_ID_index );
    }
    if( local_mesh->elem_mat_ID_item ) {
        HECMW_free( local_mesh->elem_mat_ID_item );
    }

    init_struct_elem( local_mesh );
}


static void
clean_struct_comm( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;

    if( local_mesh->neighbor_pe ) {
        HECMW_free( local_mesh->neighbor_pe );
    }
    if( local_mesh->import_index ) {
        HECMW_free( local_mesh->import_index );
    }
    if( local_mesh->import_item ) {
        HECMW_free( local_mesh->import_item );
    }
    if( local_mesh->export_index ) {
        HECMW_free( local_mesh->export_index );
    }
    if( local_mesh->export_item ) {
        HECMW_free( local_mesh->export_item );
    }
    if( local_mesh->shared_index ) {
        HECMW_free( local_mesh->shared_index );
    }
    if( local_mesh->shared_item ) {
        HECMW_free( local_mesh->shared_item );
    }

    init_struct_comm( local_mesh );
}


static void
clean_struct_adapt( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;

    init_struct_adapt( local_mesh );
}


static void
clean_struct_sect( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->section == NULL )  return;

    init_struct_sect( local_mesh );
}


static void
clean_struct_mat( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->material == NULL )  return;

    init_struct_mat( local_mesh );
}


static void
clean_struct_mpc( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->mpc == NULL )  return;

    HECMW_free( local_mesh->mpc->mpc_index );
    HECMW_free( local_mesh->mpc->mpc_item );
    HECMW_free( local_mesh->mpc->mpc_dof );
    HECMW_free( local_mesh->mpc->mpc_val );
    HECMW_free( local_mesh->mpc->mpc_const );

    init_struct_mpc( local_mesh );
}


static void
clean_struct_amp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->amp == NULL )  return;

    init_struct_amp( local_mesh );
}


static void
clean_struct_node_grp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->node_group == NULL )  return;

    if( local_mesh->node_group->grp_index ) {
        HECMW_free( local_mesh->node_group->grp_index );
    }
    if( local_mesh->node_group->grp_item ) {
        HECMW_free( local_mesh->node_group->grp_item );
    }

    init_struct_node_grp( local_mesh );
}


static void
clean_struct_elem_grp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->elem_group == NULL )  return;

    if( local_mesh->elem_group->grp_index ) {
        HECMW_free( local_mesh->elem_group->grp_index );
    }
    if( local_mesh->elem_group->grp_item ) {
        HECMW_free( local_mesh->elem_group->grp_item );
    }

    init_struct_elem_grp( local_mesh );
}


static void
clean_struct_surf_grp( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->surf_group == NULL )  return;

    if( local_mesh->surf_group->grp_index ) {
        HECMW_free( local_mesh->surf_group->grp_index );
    }
    if( local_mesh->surf_group->grp_item ) {
        HECMW_free( local_mesh->surf_group->grp_item );
    }

    init_struct_surf_grp( local_mesh );
}


static void
clean_struct_contact_pair( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;
    if( local_mesh->contact_pair == NULL )  return;

    if( local_mesh->contact_pair->type ) {
        HECMW_free( local_mesh->contact_pair->type );
    }
    if( local_mesh->contact_pair->slave_grp_id ) {
        HECMW_free( local_mesh->contact_pair->slave_grp_id );
    }
    if( local_mesh->contact_pair->master_grp_id ) {
        HECMW_free( local_mesh->contact_pair->master_grp_id );
    }

    init_struct_contact_pair( local_mesh );
}


static void
clean_struct_local_mesh( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;

    clean_struct_global( local_mesh );
    clean_struct_node( local_mesh );
    clean_struct_elem( local_mesh );
    clean_struct_comm( local_mesh );
    clean_struct_adapt( local_mesh );
    clean_struct_sect( local_mesh );
    clean_struct_mat( local_mesh );
    clean_struct_mpc( local_mesh );
    clean_struct_amp( local_mesh );
    clean_struct_node_grp( local_mesh );
    clean_struct_elem_grp( local_mesh );
    clean_struct_surf_grp( local_mesh );
    clean_struct_contact_pair( local_mesh );
}

#if 0
static void
free_struct_local_mesh( struct hecmwST_local_mesh *local_mesh )
{
    if( local_mesh == NULL )  return;

    clean_struct_local_mesh( local_mesh );

    HECMW_free( local_mesh->section );
    HECMW_free( local_mesh->material );
    HECMW_free( local_mesh->mpc );
    HECMW_free( local_mesh->amp );
    HECMW_free( local_mesh->node_group );
    HECMW_free( local_mesh->elem_group );
    HECMW_free( local_mesh->surf_group );
    HECMW_free( local_mesh );
}

/*================================================================================================*/

static struct hecmwST_local_mesh *
alloc_struct_local_mesh( void )
{
    int size;
    struct hecmwST_local_mesh *local_mesh;

    local_mesh = (struct hecmwST_local_mesh *)HECMW_malloc( sizeof(struct hecmwST_local_mesh) );
    if( local_mesh == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        local_mesh->section    = NULL;
        local_mesh->material   = NULL;
        local_mesh->mpc        = NULL;
        local_mesh->amp        = NULL;
        local_mesh->node_group = NULL;
        local_mesh->elem_group = NULL;
        local_mesh->surf_group = NULL;

        init_struct_global( local_mesh );
        init_struct_node( local_mesh );
        init_struct_elem( local_mesh );
        init_struct_comm( local_mesh );
        init_struct_adapt( local_mesh );
    }

    size = sizeof(struct hecmwST_section);
    local_mesh->section = (struct hecmwST_section *)HECMW_malloc( size );
    if( local_mesh->section == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_sect( local_mesh );
    }

    size = sizeof(struct hecmwST_material);
    local_mesh->material = (struct hecmwST_material *)HECMW_malloc( size );
    if( local_mesh->material == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_mat( local_mesh );
    }

    size = sizeof(struct hecmwST_mpc);
    local_mesh->mpc = (struct hecmwST_mpc *)HECMW_malloc( size );
    if( local_mesh->mpc == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_mpc( local_mesh );
    }

    size = sizeof(struct hecmwST_amplitude);
    local_mesh->amp = (struct hecmwST_amplitude *)HECMW_malloc( size );
    if( local_mesh->amp == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_amp( local_mesh );
    }

    size = sizeof(struct hecmwST_node_grp);
    local_mesh->node_group = (struct hecmwST_node_grp *)HECMW_malloc( size );
    if( local_mesh->node_group == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_node_grp( local_mesh );
    }

    size = sizeof(struct hecmwST_elem_grp);
    local_mesh->elem_group = (struct hecmwST_elem_grp *)HECMW_malloc( size );
    if( local_mesh->elem_group == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_elem_grp( local_mesh );
    }

    size = sizeof(struct hecmwST_surf_grp);
    local_mesh->surf_group = (struct hecmwST_surf_grp *)HECMW_malloc( size );
    if( local_mesh->surf_group == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_surf_grp( local_mesh );
    }

    return local_mesh;

error:
    free_struct_local_mesh( local_mesh );

    return NULL;
}
#endif
/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
init_struct_result_data( struct hecmwST_result_data *result_data )
{
    if( result_data == NULL ) {
        HECMW_set_error( errno, "\'result_data\' is NULL" );
        goto error;
    }

    result_data->nn_dof        = NULL;
    result_data->node_label    = NULL;
    result_data->node_val_item = NULL;

    result_data->ne_dof        = NULL;
    result_data->elem_label    = NULL;
    result_data->elem_val_item = NULL;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static void
free_struct_result_data( struct hecmwST_result_data *result_data )
{
    int i;

    if( result_data == NULL )  return;

    HECMW_free( result_data->nn_dof );
    HECMW_free( result_data->ne_dof );

    if( result_data->node_label ) {
        for( i=0; i<result_data->nn_component; i++ ) {
            HECMW_free( result_data->node_label[i] );
        }
	    HECMW_free( result_data->node_label );
    }
    if( result_data->elem_label ) {
        for( i=0; i<result_data->ne_component; i++ ) {
            HECMW_free( result_data->elem_label[i] );
        }
            HECMW_free( result_data->elem_label );
    }

    HECMW_free( result_data->node_val_item );
    HECMW_free( result_data->elem_val_item );

    HECMW_free( result_data );
    result_data = NULL;
}

/*================================================================================================*/

static int
search_eqn_block_idx( const struct hecmwST_local_mesh *mesh )
{
    int i;

    for( i=0; i<mesh->node_group->n_grp; i++ ) {
        if( !strcmp( mesh->node_group->grp_name[i], HECMW_PART_EQUATION_BLOCK_NAME ) )  return i;
    }

    return -1;
}

/*================================================================================================*/
#if 0
static int
eqn_mask_node_imposed_mpc( const struct hecmwST_local_mesh *global_mesh,
                           char *node_flag, int eqn_block_idx )
{
    struct hecmwST_node_grp *grp = global_mesh->node_group;
    int i, j, js;

    for( js=0, i=grp->grp_index[eqn_block_idx]; i<grp->grp_index[eqn_block_idx+1]; i++ ) {
        if( grp->grp_item[i] - js > 1 ) {
            for( j=js; j<grp->grp_item[i]; j++ ) {
                MASK_BIT( node_flag[j], MPC_BLOCK );
            }
        }
        js = grp->grp_item[i];
    }

    return RTC_NORMAL;
}


static int
eqn_mask_node_current_domain( const struct hecmwST_local_mesh *global_mesh,
                              char *node_flag, int current_domain )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {
        if( global_mesh->node_ID[2*i+1] == current_domain ) {
            MASK_BIT( node_flag[i], MY_DOMAIN );
        }
    }

    return RTC_NORMAL;
}


static int
eqn_mask_node_neighbor_domain( const struct hecmwST_local_mesh *global_mesh,
                               char *node_flag, int neighbor_domain )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {
        if( global_mesh->node_ID[2*i+1] == neighbor_domain ) {
            MASK_BIT( node_flag[i], NEIGHBOR_DOMAIN );
        }
    }

    return RTC_NORMAL;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
eqn_even_belong_pe_of_mpc_node( struct hecmwST_local_mesh *global_mesh, int eqn_block_idx )
{
    struct hecmwST_node_grp *grp = global_mesh->node_group;
    int npe, npe_min;
    int i, j, js;

    for( js=0, i=grp->grp_index[eqn_block_idx]; i<grp->grp_index[eqn_block_idx+1]; i++ ) {
        if( grp->grp_item[i] - js > 1 ) {
            npe_min = global_mesh->n_subdomain;
            for( j=js; j<grp->grp_item[i]; j++ ) {
                npe     = global_mesh->node_ID[2*j+1];
                npe_min = (npe < npe_min) ? npe : npe_min;
            }

            for( j=js; j<grp->grp_item[i]; j++ ) {
                global_mesh->node_ID[2*j+1] = npe_min;
            }
        }
        js = grp->grp_item[i];
    }

    return RTC_NORMAL;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
eqn_count_n_node_all( const struct hecmwST_local_mesh *global_mesh, int *n_node_all )
{
    int i, j;

    memset( n_node_all, 0, sizeof(int)*global_mesh->n_subdomain );

    for( i=0; i<global_mesh->n_subdomain; i++ ) {
        for( j=0; j<global_mesh->n_node; j++ ) {
            if( global_mesh->node_ID[2*j+1] == i ) {
                n_node_all[i]++;
            }
        }
    }

    return RTC_NORMAL;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
eqn_mask_neighbor_domain( const struct hecmwST_local_mesh *global_mesh,
                          const char *node_flag, char *domain_flag )
{
    int node;
    int npe[MAX_NODE_SIZE];
    int evalsum;
    int js, je;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        js = global_mesh->elem_node_index[i];
        je = global_mesh->elem_node_index[i+1];
        for( evalsum=0, j=js; j<je; j++ ) {
            node     = global_mesh->elem_node_item[j];
            npe[j-js]= global_mesh->node_ID[2*(node-1)+1];
            evalsum += EVAL_BIT( node_flag[node-1], MY_DOMAIN );
        }

        if( evalsum ) {
            for( j=0; j<je-js; j++ ) {
                MASK_BIT( domain_flag[npe[j]], NEIGHBOR_DOMAIN );
            }
        }
    }

    return RTC_NORMAL;
}


static int
eqn_count_neighbor_domain( const struct hecmwST_local_mesh *global_mesh, char *domain_flag,
                           int *n_neighbor_pe_all, int current_domain )
{
    int i;

    CLEAR_BIT( domain_flag[current_domain], NEIGHBOR_DOMAIN );
    for( i=0; i<global_mesh->n_subdomain; i++ ) {
        if( EVAL_BIT( domain_flag[i], NEIGHBOR_DOMAIN ) ) {
            n_neighbor_pe_all[current_domain]++;
        }
    }

    return RTC_NORMAL;
}


static int
eqn_set_neighbor_domain( const struct hecmwST_local_mesh *global_mesh, const char *domain_flag,
                         const int *n_neighbor_pe_all, int *neighbor_pe_all, int current_domain )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_subdomain; i++ ) {
        if( EVAL_BIT( domain_flag[i], NEIGHBOR_DOMAIN ) ) {
            neighbor_pe_all[(current_domain*global_mesh->n_subdomain)+counter++] = i;
        }
    }
    HECMW_assert( counter == n_neighbor_pe_all[current_domain] );

    return RTC_NORMAL;
}


static int
eqn_create_neighbor_info( struct hecmwST_local_mesh *global_mesh, char *node_flag,
                          int *n_neighbor_pe_all, int *neighbor_pe_all )
{
    char *domain_flag = NULL;
    int rtc;
    int i, j;

    HECMW_assert( global_mesh );
    HECMW_assert( node_flag );
    HECMW_assert( n_neighbor_pe_all );
    HECMW_assert( neighbor_pe_all );

    domain_flag = (char *)HECMW_malloc( sizeof(char)*global_mesh->n_subdomain );
    if( domain_flag == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<global_mesh->n_subdomain; i++ ) {
        for( j=0; j<global_mesh->n_subdomain; j++ ) {
            CLEAR_BIT( domain_flag[j], NEIGHBOR_DOMAIN );
        }
        for( j=0; j<global_mesh->n_node; j++ ) {
            CLEAR_BIT( node_flag[j], MY_DOMAIN );
            CLEAR_BIT( node_flag[j], NEIGHBOR_DOMAIN );
            CLEAR_BIT( node_flag[j], CANDIDATE );
        }

        rtc = eqn_mask_node_current_domain( global_mesh, node_flag, i );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = eqn_mask_neighbor_domain( global_mesh, node_flag, domain_flag );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = eqn_count_neighbor_domain( global_mesh, domain_flag, n_neighbor_pe_all, i );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = eqn_set_neighbor_domain( global_mesh, domain_flag,
                                       n_neighbor_pe_all, neighbor_pe_all, i );
        if( rtc != RTC_NORMAL )  goto error;
    }

    HECMW_free( domain_flag );

    return RTC_NORMAL;

error:
    HECMW_free( domain_flag );

    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
eqn_diffusion_equation( const struct hecmwST_local_mesh *global_mesh,
                        int *n_neighbor_pe_all, int *neighbor_pe_all,
                        int *n_node_all, double *move_indicator )
{
    int n_neighbor_pe_max = global_mesh->n_subdomain;
    int n_node_ave = ( (double)global_mesh->n_node / global_mesh->n_subdomain );
    double move_indicator_ini, res, resid;
    double *a = NULL;
    double *b = NULL;
    double *d = NULL;
    int i, j, k;

    for( i=0; i<global_mesh->n_subdomain; i++ ) {
        n_neighbor_pe_max = ( n_neighbor_pe_all[i] > n_neighbor_pe_max ) ?
                              n_neighbor_pe_all[i] : n_neighbor_pe_max;
    }

    a = (double *)HECMW_malloc( sizeof(double)*global_mesh->n_subdomain*n_neighbor_pe_max );
    if( a == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_subdomain*n_neighbor_pe_max; i++ ) {
            a[i] = 0.0;
        }
    }

    b = (double *)HECMW_malloc( sizeof(double)*global_mesh->n_subdomain );
    if( b == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_subdomain; i++ ) {
            b[i] = n_node_all[i] - n_node_ave;
        }
    }

    d = (double *)HECMW_malloc( sizeof(double)*global_mesh->n_subdomain );
    if( d == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_subdomain; i++ ) {
            d[i] = 0.0;
        }
    }

    for( i=0; i<global_mesh->n_subdomain; i++ ) {
        for( j=0; j<n_neighbor_pe_all[i]; j++ ) {
            d[i]                     +=  1.0;
            a[i*n_neighbor_pe_max+j]  = -1.0;
        }
    }

    for( i=0; i<global_mesh->n_node; i++ ) {
        resid = 0.0;

        for( j=1; j<global_mesh->n_subdomain; j++ ) {
            for( res=b[j], k=0; k<n_neighbor_pe_all[j]; k++ ) {
                res -= a[j*n_neighbor_pe_max+k] *
                         move_indicator[neighbor_pe_all[j*global_mesh->n_subdomain+k]];
            }

            move_indicator_ini = move_indicator[j];
            move_indicator[j]  = move_indicator_ini + ( res / d[j] - move_indicator_ini );
            resid += pow( (move_indicator[j] - move_indicator_ini), 2 );
        }

        resid = sqrt( resid );
        if( resid < EPS ) break;
    }

    HECMW_free( a );
    HECMW_free( b );
    HECMW_free( d );

    return RTC_NORMAL;

error:
    HECMW_free( a );
    HECMW_free( b );
    HECMW_free( d );

    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
eqn_mask_node_candidate( const struct hecmwST_local_mesh *global_mesh, char *node_flag )
{
    int node;
    int evalsum_m, evalsum_n;
    int js, je;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        js = global_mesh->elem_node_index[i];
        je = global_mesh->elem_node_index[i+1];
        for( evalsum_m=0, evalsum_n=0, j=js; j<je; j++ ) {
            node       = global_mesh->elem_node_item[j];
            evalsum_m += EVAL_BIT( node_flag[node-1], MY_DOMAIN );
            evalsum_n += EVAL_BIT( node_flag[node-1], NEIGHBOR_DOMAIN );
        }

        if( evalsum_m && evalsum_n ) {
            for( j=js; j<je; j++ ) {
                node = global_mesh->elem_node_item[j];
                if( !EVAL_BIT( node_flag[node-1], MPC_BLOCK ) ) {
                    MASK_BIT( node_flag[node-1], CANDIDATE );
                }
            }
        }
    }

    return RTC_NORMAL;
}


static int
eqn_repart_move_node( const struct hecmwST_local_mesh *global_mesh, char *node_flag,
                      int current_domain, int neighbor_domain, int move_direction, int n_move )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {


        if( move_direction > 0 ) {
            if( EVAL_BIT( node_flag[i], MY_DOMAIN ) && EVAL_BIT( node_flag[i], CANDIDATE ) ) {
                global_mesh->node_ID[2*i+1] = neighbor_domain;
                n_move--;

                INV_BIT( node_flag[i], MY_DOMAIN );
                INV_BIT( node_flag[i], NEIGHBOR_DOMAIN );

                if( n_move == 0 )  return n_move;
            }
        }


        if( move_direction < 0 ) {
            if( EVAL_BIT( node_flag[i], NEIGHBOR_DOMAIN ) && EVAL_BIT( node_flag[i], CANDIDATE ) ) {
                global_mesh->node_ID[2*i+1] = current_domain;
                n_move--;

                INV_BIT( node_flag[i], NEIGHBOR_DOMAIN );
                INV_BIT( node_flag[i], MY_DOMAIN );

                if( n_move == 0 )  return n_move;
            }
        }
    }

    return n_move;
}


static int
eqn_repart_move_node_sub( const struct hecmwST_local_mesh *global_mesh, char *node_flag,
                          int current_domain, int neighbor_domain, int move_direction, int n_move )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {


        if( move_direction > 0 ) {
            if( EVAL_BIT( node_flag[i], MY_DOMAIN ) &&
               !EVAL_BIT( node_flag[i], MPC_BLOCK ) ) {
                global_mesh->node_ID[2*i+1] = neighbor_domain;
                n_move--;

                INV_BIT( node_flag[i], MY_DOMAIN );
                INV_BIT( node_flag[i], NEIGHBOR_DOMAIN );

                if( n_move == 0 )  return n_move;
            }
        }


        if( move_direction < 0 ) {
            if( EVAL_BIT( node_flag[i], NEIGHBOR_DOMAIN ) &&
               !EVAL_BIT( node_flag[i], MPC_BLOCK ) ) {
                global_mesh->node_ID[2*i+1] = current_domain;
                n_move--;

                INV_BIT( node_flag[i], MY_DOMAIN );
                INV_BIT( node_flag[i], NEIGHBOR_DOMAIN );

                if( n_move == 0 )  return n_move;
            }
        }

    }

    return n_move;
}


static int
eqn_repartition( struct hecmwST_local_mesh *global_mesh, char *node_flag,
                 int *n_neighbor_pe_all, int *neighbor_pe_all, int *n_node_all,
                 double *move_indicator )
{
    int current_domain, neighbor_domain;
    double move_indicator_current, move_indicator_neighbor;
    int move_direction, n_move;
    int rtc;
    int i, j, k, ke;

    for( i=0; i<global_mesh->n_subdomain; i++ ) {
        for( j=0; j<n_neighbor_pe_all[i]; j++ ) {
            current_domain  = i;
            neighbor_domain = neighbor_pe_all[global_mesh->n_subdomain*i+j];


            for( k=0; k<global_mesh->n_node; k++ )  {
                CLEAR_BIT( node_flag[k], MY_DOMAIN );
                CLEAR_BIT( node_flag[k], NEIGHBOR_DOMAIN );
                CLEAR_BIT( node_flag[k], CANDIDATE );
            }


            rtc = eqn_mask_node_current_domain( global_mesh, node_flag, current_domain );
            if( rtc != RTC_NORMAL )  goto error;


            rtc = eqn_mask_node_neighbor_domain( global_mesh, node_flag, neighbor_domain );
            if( rtc != RTC_NORMAL )  goto error;


            if( neighbor_domain > current_domain ) {
                move_indicator_current  = move_indicator[current_domain];
                move_indicator_neighbor = move_indicator[neighbor_domain];

                move_direction = ( move_indicator_current - move_indicator_neighbor < 0.0 ) ?
                    (int)( move_indicator_current - move_indicator_neighbor - F_6_10 ) :
                    (int)( move_indicator_current - move_indicator_neighbor + F_6_10 );

                if( !move_direction ) continue;
                n_move = ( move_direction < 0 ) ? -move_direction : +move_direction;

                rtc = eqn_mask_node_candidate( global_mesh, node_flag );
                if( rtc != RTC_NORMAL )  goto error;

                ke = n_move;
                for( k=0; k<ke; k++ ) {
                    n_move = eqn_repart_move_node( global_mesh, node_flag,
                                                   current_domain, neighbor_domain,
                                                   move_direction, n_move );
                    if( !n_move ) break;
                }

                if( n_move > 0 ) {
                    n_move = eqn_repart_move_node_sub( global_mesh, node_flag,
                                                       current_domain, neighbor_domain,
                                                       move_direction, n_move );
                }
            }
        }
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
eqn_block( struct hecmwST_local_mesh *global_mesh )
{
    int *n_neighbor_pe_all = NULL;
    int *neighbor_pe_all = NULL;
    int *n_node_all = NULL;
    double *move_indicator = NULL;
    char *node_flag = NULL;
    int eqn_block_idx;
    int size;
    int rtc;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->mpc );
    HECMW_assert( global_mesh->node_group );


    eqn_block_idx = search_eqn_block_idx( global_mesh );


    if( eqn_block_idx < 0 ) {


        if( global_mesh->mpc->n_mpc == 0 ) {
            return RTC_NORMAL;


        } else if ( global_mesh->mpc->n_mpc > 0 ) {
            HECMW_log( HECMW_LOG_WARN,  HECMW_strmsg( HECMW_PART_W_NO_EQUATIONBLOCK ) );
            return RTC_WARN;
        }
    }

    node_flag = (char *)HECMW_calloc( global_mesh->n_node, sizeof(char) );
    if( node_flag == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }


    rtc = eqn_mask_node_imposed_mpc( global_mesh, node_flag, eqn_block_idx );
    if( rtc != RTC_NORMAL )  goto error;


    rtc = eqn_even_belong_pe_of_mpc_node( global_mesh, eqn_block_idx );
    if( rtc != RTC_NORMAL )  goto error;


    n_node_all = (int *)HECMW_calloc( global_mesh->n_subdomain, sizeof(int) );
    if( n_node_all == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = eqn_count_n_node_all( global_mesh, n_node_all );
    if( rtc != RTC_NORMAL )  goto error;


    n_neighbor_pe_all = (int *)HECMW_calloc( global_mesh->n_subdomain, sizeof(int) );
    if( n_neighbor_pe_all == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    size = global_mesh->n_subdomain * global_mesh->n_subdomain;
    neighbor_pe_all = (int *)HECMW_calloc( size, sizeof(int) );
    if( neighbor_pe_all == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = eqn_create_neighbor_info( global_mesh, node_flag, n_neighbor_pe_all, neighbor_pe_all );
    if( rtc != RTC_NORMAL )  goto error;


    move_indicator = (double *)HECMW_malloc( sizeof(double)*global_mesh->n_subdomain );
    if( move_indicator == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_subdomain; i++ ) {
            move_indicator[i] = 0.0;
        }
    }

    rtc = eqn_diffusion_equation( global_mesh,
                                  n_neighbor_pe_all, neighbor_pe_all, n_node_all, move_indicator );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = eqn_repartition( global_mesh, node_flag,
                           n_neighbor_pe_all, neighbor_pe_all, n_node_all, move_indicator );
    if( rtc != RTC_NORMAL )  goto error;


    rtc = eqn_count_n_node_all( global_mesh, n_node_all );
    if( rtc != RTC_NORMAL )  goto error;

    HECMW_free( node_flag );
    HECMW_free( n_neighbor_pe_all );
    HECMW_free( neighbor_pe_all );
    HECMW_free( n_node_all );
    HECMW_free( move_indicator );

    return RTC_NORMAL;

error:
    HECMW_free( node_flag );
    HECMW_free( n_neighbor_pe_all );
    HECMW_free( neighbor_pe_all );
    HECMW_free( n_node_all );
    HECMW_free( move_indicator );

    return RTC_ERROR;
}
#endif
/*================================================================================================*/

static int
quick_sort( int no, int n, double *arr, int *brr, int *istack )
{
    double a, atemp;
    int b, btemp;
    int i, ir, j, k, l;
    int jstack = 0;
    int nstack;

    nstack = no;
    l      = 0;
    ir     = n-1;

    for( ; ; ) {
        if( ir-l < QSORT_LOWER ) {
            for( j=l+1; j<=ir; j++ ) {
                a = arr[j];
                b = brr[j];
                for( i=j-1; i>=l; i-- ) {
                    if( arr[i] <= a ) break;
                    arr[i+1] = arr[i];
                    brr[i+1] = brr[i];
                }
                arr[i+1] = a;
                brr[i+1] = b;
            }

            if( !jstack ) return 0;

            ir = istack[jstack];
            l  = istack[jstack-1];
            jstack -= 2;

        } else {

            k = (l+ir) >> 1;

            DSWAP( arr[k], arr[l+1] )
            ISWAP( brr[k], brr[l+1] )

            if( arr[l] > arr[ir] ) {
                DSWAP( arr[l], arr[ir] )
                ISWAP( brr[l], brr[ir] )
            }

            if( arr[l+1] > arr[ir] ) {
                DSWAP( arr[l+1], arr[ir] )
                ISWAP( brr[l+1], brr[ir] )
            }

            if( arr[l] > arr[l+1] ) {
                DSWAP( arr[l], arr[l+1] )
                ISWAP( brr[l], brr[l+1] )
            }

            i = l+1;
            j = ir;
            a = arr[l+1];
            b = brr[l+1];

            for( ; ; ) {
                do i++; while( arr[i] < a );
                do j--; while( arr[j] > a );

                if( j < i ) break;

                DSWAP( arr[i], arr[j] )
                ISWAP( brr[i], brr[j] )
            }

            arr[l+1] = arr[j];
            arr[j]   = a;
            brr[l+1] = brr[j];
            brr[j]   = b;

            jstack += 2;

            if( jstack > nstack ) {
                HECMW_set_error( HECMW_PART_E_STACK_OVERFLOW, "" );
                return -1;
            }

            if( ir-i+1 >= j-l ) {
                istack[jstack]   = ir;
                istack[jstack-1] = i;
                ir = j-1;
            } else {
                istack[jstack]   = j-1;
                istack[jstack-1] = l;
                l = i;
            }
        }
    }
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
rcb_partition( int n, const double *coord, int *wnum, const struct hecmw_part_cont_data *cont_data )
{
    double *value;
    int *id, *stack;
    int rtc;
    int counter;
    int i, j, k;

    id = (int *)HECMW_malloc( sizeof(int)*n );
    if( id == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    stack = (int *)HECMW_malloc( sizeof(int)*n );
    if( stack == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    value = (double *)HECMW_malloc( sizeof(double)*n );
    if( value == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<cont_data->n_rcb_div; i++ ) {
        for( j=0; j<pow(2, i); j++ ) {
            counter=0;

            switch( cont_data->rcb_axis[i] ) {
            case HECMW_PART_RCB_X_AXIS:  /* X-axis */
                for( k=0; k<n; k++ ) {
                    if( wnum[2*k+1] == j ) {
                        id[counter]    = k;
                        value[counter] = coord[3*k];
                        counter++;
                    }
                }
                break;

            case HECMW_PART_RCB_Y_AXIS:  /* Y-axis */
                for( k=0; k<n; k++ ) {
                    if( wnum[2*k+1] == j ) {
                        id[counter]    = k;
                        value[counter] = coord[3*k+1];
                        counter++;
                    }
                }
                break;

            case HECMW_PART_RCB_Z_AXIS:  /* Z-axis */
                for( k=0; k<n; k++ ) {
                    if( wnum[2*k+1] == j ) {
                        id[counter]    = k;
                        value[counter] = coord[3*k+2];
                        counter++;
                    }
                }
                break;

            default:
                HECMW_set_error( HECMW_PART_E_INVALID_RCB_DIR, "" );
                goto error;
            }

            /* quick sort */
            rtc = quick_sort( n, counter, value, id, stack );
            if( rtc != RTC_NORMAL )  goto error;

            /* belonging domain of node */
            for( k=0; k<counter*F_1_2; k++ ) {
                wnum[2*id[k]+1] = j + (int)pow(2, i);
            }
        }
    }

    HECMW_free( id );
    HECMW_free( stack );
    HECMW_free( value );

    return RTC_NORMAL;

error:
    HECMW_free( id );
    HECMW_free( stack );
    HECMW_free( value );

    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
calc_gravity( const struct hecmwST_local_mesh *global_mesh, double *coord )
{
    double coord_x, coord_y, coord_z;
    int node;
    int js, je;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        js = global_mesh->elem_node_index[i];
        je = global_mesh->elem_node_index[i+1];

        for( coord_x=0.0, coord_y=0.0, coord_z=0.0, j=js; j<je; j++ ) {
            node     = global_mesh->elem_node_item[j];

            coord_x += global_mesh->node[3*(node-1)  ];
            coord_y += global_mesh->node[3*(node-1)+1];
            coord_z += global_mesh->node[3*(node-1)+2];
        }

        coord[3*i  ] = coord_x / ( je - js );
        coord[3*i+1] = coord_y / ( je - js );
        coord[3*i+2] = coord_z / ( je - js );
    }

    return RTC_NORMAL;
}


static int
rcb_partition_eb( struct hecmwST_local_mesh *global_mesh,
                  const struct hecmw_part_cont_data *cont_data )
{
    double *coord = NULL;
    int rtc;

    coord = (double *)HECMW_malloc( sizeof(double)*global_mesh->n_elem*3 );
    if( coord == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }


    rtc = calc_gravity( global_mesh, coord );
    if( rtc != RTC_NORMAL )  goto error;


    rtc = rcb_partition( global_mesh->n_elem, coord, global_mesh->elem_ID, cont_data );
    if( rtc != RTC_NORMAL )  goto error;

    HECMW_free( coord );

    return RTC_NORMAL;

error:
    HECMW_free( coord );

    return RTC_ERROR;
}

/*================================================================================================*/

static int
create_node_graph_link_list( const struct hecmwST_local_mesh *global_mesh,
                             const struct hecmw_part_edge_data *edge_data,
                             struct link_list **graph )
{
    int node1, node2;
    int i;

    for( i=0; i<edge_data->n_edge; i++ ) {
        node1 = edge_data->edge_node_item[2*i];
        node2 = edge_data->edge_node_item[2*i+1];

        /* node 1 */
        graph[node1-1]->last->next = (struct link_unit *)HECMW_malloc( sizeof( struct link_unit ) );
        if( graph[node1-1]->last->next == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        }

        graph[node1-1]->n               += 1;
        graph[node1-1]->last->next->id   = node2;
        graph[node1-1]->last->next->next = NULL;
        graph[node1-1]->last             = graph[node1-1]->last->next;

        /* node 2 */
        graph[node2-1]->last->next = (struct link_unit *)HECMW_malloc( sizeof( struct link_unit ) );
        if( graph[node2-1]->last->next == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        }

        graph[node2-1]->n               += 1;
        graph[node2-1]->last->next->id   = node1;
        graph[node2-1]->last->next->next = NULL;
        graph[node2-1]->last             = graph[node2-1]->last->next;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
create_node_graph_compress( const struct hecmwST_local_mesh *global_mesh,
                            struct link_list **graph,
                            int *node_graph_index, int *node_graph_item )
{
    int counter;
    int i, j;
    struct link_unit *p;

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        node_graph_index[i+1] = node_graph_index[i] + graph[i]->n;

        for( p=graph[i]->list, j=0; j<graph[i]->n; j++ ) {
            p                          = p->next;
            node_graph_item[counter++] = p->id - 1;
        }
    }

    return RTC_NORMAL;
}


static int
create_node_graph( const struct hecmwST_local_mesh *global_mesh,
                   const struct hecmw_part_edge_data *edge_data,
                   int *node_graph_index, int *node_graph_item )
{
    struct link_list **graph = NULL;
    int rtc;
    int i;

    graph = (struct link_list **)HECMW_malloc( sizeof( struct link_list *)*global_mesh->n_node );
    if( graph == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_node; i++ ) {
            graph[i] = NULL;
        }
    }
    for( i=0; i<global_mesh->n_node; i++ ) {
        graph[i] = (struct link_list *)HECMW_malloc( sizeof( struct link_list ) );
        if( graph[i] == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        } else {
            graph[i]->list = NULL;
        }
    }
    for( i=0; i<global_mesh->n_node; i++ ) {
        graph[i]->list = (struct link_unit *)HECMW_malloc( sizeof( struct link_unit ) );
        if( graph[i]->list == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        } else {
            graph[i]->n          = 0;
            graph[i]->list->next = NULL;
            graph[i]->last       = graph[i]->list;
        }
    }


    rtc = create_node_graph_link_list( global_mesh, edge_data, graph );
    if( rtc != RTC_NORMAL )  goto error;


    rtc = create_node_graph_compress( global_mesh, graph, node_graph_index, node_graph_item );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<global_mesh->n_node; i++ ) {
        free_link_list( graph[i]->list );
        HECMW_free( graph[i] );
    }
    HECMW_free( graph );

    return RTC_NORMAL;

error:
    if( graph ) {
        for( i=0; i<global_mesh->n_node; i++ ) {
            if( graph[i] ) {
                free_link_list( graph[i]->list );
                HECMW_free( graph[i] );
            }
        }
        HECMW_free( graph );
    }

    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
set_node_belong_elem( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmw_part_node_data *node_data )
{
    int node, counter;
    struct link_list **node_list = NULL;
    struct link_unit *p;
    int size;
    int i, j;

    node_data->node_elem_index = NULL;
    node_data->node_elem_item  = NULL;

    node_list = (struct link_list **)HECMW_malloc( sizeof(struct link_list *)*global_mesh->n_node );
    if( node_list == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_node; i++ ) {
            node_list[i] = NULL;
        }
    }
    for( i=0; i<global_mesh->n_node; i++ ) {
        node_list[i] = (struct link_list *)HECMW_malloc( sizeof(struct link_list) );
        if( node_list[i] == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        } else {
            node_list[i]->list = NULL;
        }
    }
    for( i=0; i<global_mesh->n_node; i++ ) {
        node_list[i]->list = (struct link_unit *)HECMW_malloc( sizeof(struct link_unit) );
        if( node_list[i]->list == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        } else {
            node_list[i]->n          = 0;
            node_list[i]->list->next = NULL;
            node_list[i]->last       = node_list[i]->list;
        }
    }


    for( i=0; i<global_mesh->n_elem; i++ ) {
        for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
            node = global_mesh->elem_node_item[j];

            size = sizeof(struct link_list);
            node_list[node-1]->last->next = (struct link_unit *)HECMW_malloc( size );
            if( node_list[node-1]->last->next == NULL ) {
                HECMW_set_error( errno, "" );
                goto error;
            }

            node_list[node-1]->last       = node_list[node-1]->last->next;
            node_list[node-1]->last->id   = i + 1;
            node_list[node-1]->last->next = NULL;
            node_list[node-1]->n         += 1;
        }
    }


    node_data->node_elem_index = (int *)HECMW_calloc( global_mesh->n_node+1, sizeof(int) );
    if( node_data->node_elem_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    for( i=0; i<global_mesh->n_node; i++ ) {
        node_data->node_elem_index[i+1] = node_data->node_elem_index[i] + node_list[i]->n;
    }

    size = sizeof(int) * node_data->node_elem_index[global_mesh->n_node];
    node_data->node_elem_item = (int *)HECMW_malloc( size );
    if( node_data->node_elem_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        for( p=node_list[i]->list, j=0; j<node_list[i]->n; j++ ) {
            p                                    = p->next;
            node_data->node_elem_item[counter++] = p->id;
        }
        HECMW_assert( counter == node_data->node_elem_index[i+1] );
    }

    for( i=0; i<global_mesh->n_node; i++ ) {
        free_link_list( node_list[i]->list );
        HECMW_free( node_list[i] );
    }
    HECMW_free( node_list );

    return RTC_NORMAL;

error:
    if( node_list ) {
        for( i=0; i<global_mesh->n_node; i++ ) {
            if( node_list[i] ) {
                free_link_list( node_list[i]->list );
                HECMW_free( node_list[i] );
            }
        }
        HECMW_free( node_list );
    }

    HECMW_free( node_data->node_elem_index );
    HECMW_free( node_data->node_elem_item );
    node_data->node_elem_index = NULL;
    node_data->node_elem_item  = NULL;

    return RTC_ERROR;
}


static int
create_elem_graph_link_list( const struct hecmwST_local_mesh *global_mesh,
                             const struct hecmw_part_node_data *node_data,
                             struct link_list **graph )
{
    char *elem_flag = NULL;
    int elem, node;
    int size;
    int counter;
    int i, j, k;

    elem_flag = (char *)HECMW_malloc( sizeof(char)*global_mesh->n_elem );
    if( elem_flag == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        memset( elem_flag, 0, sizeof(char)*global_mesh->n_elem );
        MASK_BIT( elem_flag[i], MASK );

        for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
            node = global_mesh->elem_node_item[j];

            for( k=node_data->node_elem_index[node-1]; k<node_data->node_elem_index[node]; k++ ) {
                elem = node_data->node_elem_item[k];

                if( ! EVAL_BIT( elem_flag[elem-1], MASK ) ) {
                    MASK_BIT( elem_flag[elem-1], MASK );

                    size = sizeof(struct link_unit);
                    graph[i]->last->next = (struct link_unit *)HECMW_malloc( size );
                    if( graph[i]->last->next == NULL ) {
                        HECMW_set_error( errno, "" );
                        goto error;
                    }

                    graph[i]->n                += 1;
                    graph[i]->last->next->id   = elem;
                    graph[i]->last->next->next = NULL;
                    graph[i]->last             = graph[i]->last->next;
                    counter++;
                }
            }
        }
    }

    HECMW_free( elem_flag );

    return counter;

error:
    HECMW_free( elem_flag );

    return -1;
}


static int
create_elem_graph_compress( const struct hecmwST_local_mesh *global_mesh,
                            struct link_list **graph,
                            int *elem_graph_index, int *elem_graph_item )
{
    struct link_unit *p;
    int counter;
    int i, j;

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        elem_graph_index[i+1] = elem_graph_index[i] + graph[i]->n;

        for( p=graph[i]->list, j=0; j<graph[i]->n; j++ ) {
            p                          = p->next;
            elem_graph_item[counter++] = p->id - 1;
        }
    }
    HECMW_assert( elem_graph_index[global_mesh->n_elem] == counter );

    return RTC_NORMAL;
}


static int *
create_elem_graph( const struct hecmwST_local_mesh *global_mesh, int *elem_graph_index )
{
    struct hecmw_part_node_data *node_data = NULL;
    struct link_list **graph = NULL;
    int *elem_graph_item = NULL;
    int n_graph;
    int rtc;
    int i;


    node_data = (struct hecmw_part_node_data *)HECMW_malloc( sizeof(struct hecmw_part_node_data) );
    if( node_data == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        node_data->node_elem_index = NULL;
        node_data->node_elem_item  = NULL;
    }

    rtc = set_node_belong_elem( global_mesh, node_data );
    if( rtc != RTC_NORMAL )  goto error;


    graph = (struct link_list **)HECMW_malloc( sizeof( struct link_list *)*global_mesh->n_elem );
    if( graph == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_elem; i++ ) {
            graph[i] = NULL;
        }
    }
    for( i=0; i<global_mesh->n_elem; i++ ) {
        graph[i] = (struct link_list *)HECMW_malloc( sizeof( struct link_list ) );
        if( graph[i] == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        } else {
            graph[i]->list = NULL;
        }
    }
    for( i=0; i<global_mesh->n_elem; i++ ) {
        graph[i]->list = (struct link_unit *)HECMW_malloc( sizeof( struct link_unit ) );
        if( graph[i]->list == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        } else {
            graph[i]->n          = 0;
            graph[i]->list->next = NULL;
            graph[i]->last       = graph[i]->list;
        }
    }

    n_graph = create_elem_graph_link_list( global_mesh, node_data, graph );
    if( n_graph < 0 )  goto error;


    elem_graph_item = (int *)HECMW_malloc( sizeof(int)*n_graph );
    if( elem_graph_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_elem_graph_compress( global_mesh, graph, elem_graph_index, elem_graph_item );
    if( rtc != RTC_NORMAL )  goto error;


    HECMW_free( node_data->node_elem_index );
    HECMW_free( node_data->node_elem_item );
    HECMW_free( node_data );
    for( i=0; i<global_mesh->n_elem; i++ ) {
        free_link_list( graph[i]->list );
        HECMW_free( graph[i] );
    }
    HECMW_free( graph );

    return elem_graph_item;

error:
    if( node_data ) {
        HECMW_free( node_data->node_elem_index );
        HECMW_free( node_data->node_elem_item );
        HECMW_free( node_data );
    }
    if( graph ) {
        for( i=0; i<global_mesh->n_elem; i++ ) {
            if( graph[i] ) {
                free_link_list( graph[i]->list );
                HECMW_free( graph[i] );
            }
        }
        HECMW_free( graph );
    }
    HECMW_free( elem_graph_item );

    return NULL;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
pmetis_interface( const int n_vertex, const int n_domain, int *xadj, int *adjncy, int *part )
{
#ifdef HECMW_PART_WITH_METIS
    int n=n_vertex;              /* number of vertices */
    int *vwgt=NULL;              /* weight for vertices */
    int *adjwgt=NULL;            /* weight for edges */
    int wgtflag=0;               /* flag of weight for edges */
    int numflag=0;               /* flag of stating number of index */
    int nparts=n_domain;         /* number of sub-domains */
    int options[5]={0,0,0,0,0};  /* options for pMETIS */
#endif
    int edgecut=0;               /* number of edge-cut */

#ifdef HECMW_PART_WITH_METIS
    /* pMETIS */
    METIS_PartGraphRecursive( &n, xadj, adjncy, vwgt, adjwgt,
                              &wgtflag, &numflag, &nparts, options, &edgecut, part );
#endif

    return edgecut;
}


static int
kmetis_interface( const int n_vertex, const int n_domain, int *xadj, int *adjncy, int *part )
{
#ifdef HECMW_PART_WITH_METIS
    int n=n_vertex;              /* number of vertices */
    int *vwgt=NULL;              /* weight for vertices */
    int *adjwgt=NULL;            /* weight for edges */
    int wgtflag=0;               /* flag of weight for edges */
    int numflag=0;               /* flag of stating number of index */
    int nparts=n_domain;         /* number of sub-domains */
    int options[5]={0,0,0,0,0};  /* options for kMETIS */
#endif
    int edgecut=0;               /* number of edge-cut */

#ifdef HECMW_PART_WITH_METIS
    METIS_PartGraphKway( &n, xadj, adjncy, vwgt, adjwgt,
                         &wgtflag, &numflag, &nparts, options, &edgecut, part );
#endif

    return edgecut;
}


static int
metis_partition_nb( struct hecmwST_local_mesh *global_mesh,
                    const struct hecmw_part_cont_data *cont_data,
                    const struct hecmw_part_edge_data *edge_data )
{
    int n_edgecut;
    int *node_graph_index = NULL;  /* index for nodal graph */
    int *node_graph_item = NULL;   /* member of nodal graph */
    int *belong_domain = NULL;
    int rtc;
    int i;


    node_graph_index = (int *)HECMW_calloc( global_mesh->n_node+1, sizeof(int) );
    if( node_graph_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    node_graph_item = (int *)HECMW_malloc( sizeof(int)*edge_data->n_edge*2 );
    if( node_graph_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_node_graph( global_mesh, edge_data, node_graph_index, node_graph_item );
    if( rtc != RTC_NORMAL )  goto error;


    belong_domain = (int *)HECMW_calloc( global_mesh->n_node, sizeof(int) );
    if( belong_domain == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    switch( cont_data->method ) {
    case HECMW_PART_METHOD_PMETIS:  /* pMETIS */
        n_edgecut = pmetis_interface( global_mesh->n_node, global_mesh->n_subdomain,
                                      node_graph_index, node_graph_item, belong_domain );
        if( n_edgecut < 0 )  goto error;
        break;

    case HECMW_PART_METHOD_KMETIS:  /* kMETIS */
        n_edgecut = kmetis_interface( global_mesh->n_node, global_mesh->n_subdomain,
                                      node_graph_index, node_graph_item, belong_domain );
        if( n_edgecut < 0 )  goto error;
        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PMETHOD, "" );
        goto error;
    }

    for( i=0; i<global_mesh->n_node; i++ ) {
        global_mesh->node_ID[2*i+1] = belong_domain[i];
    }

    HECMW_free( node_graph_index );
    HECMW_free( node_graph_item );
    HECMW_free( belong_domain );

    return n_edgecut;

error:
    HECMW_free( node_graph_index );
    HECMW_free( node_graph_item );
    HECMW_free( belong_domain );

    return -1;
}


static int
metis_partition_eb( struct hecmwST_local_mesh *global_mesh,
                    const struct hecmw_part_cont_data *cont_data,
                    int *elem_graph_index, int *elem_graph_item )
{
    int n_edgecut;
    int *belong_domain = NULL;
    int i;

    belong_domain = (int *)HECMW_calloc( global_mesh->n_elem, sizeof(int) );
    if( belong_domain == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    switch( cont_data->method ) {
    case HECMW_PART_METHOD_PMETIS:  /* pMETIS */
        n_edgecut = pmetis_interface( global_mesh->n_elem, global_mesh->n_subdomain,
                                      elem_graph_index, elem_graph_item, belong_domain );
        if( n_edgecut < 0 )  goto error;
        break;

    case HECMW_PART_METHOD_KMETIS:  /* kMETIS */
        n_edgecut = kmetis_interface( global_mesh->n_elem, global_mesh->n_subdomain,
                                      elem_graph_index, elem_graph_item, belong_domain );
        if( n_edgecut < 0 )  goto error;
        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PMETHOD, "" );
        goto error;
    }

    for( i=0; i<global_mesh->n_elem; i++ ) {
        global_mesh->elem_ID[2*i+1] = belong_domain[i];
    }

    HECMW_free( belong_domain );

    return n_edgecut;

error:
    HECMW_free( belong_domain );

    return -1;
}


/*------------------------------------------------------------------------------------------------*/

static int
set_node_belong_domain_nb( struct hecmwST_local_mesh *global_mesh,
                           const struct hecmw_part_cont_data *cont_data )
{
    struct hecmw_part_edge_data *edge_data = NULL;
    int n_edgecut;
    int rtc;
    int i;


    edge_data = (struct hecmw_part_edge_data *)HECMW_malloc( sizeof(struct hecmw_part_edge_data) );
    if( edge_data == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        edge_data->n_edge         = 0;
        edge_data->edge_node_item = NULL;
    }

    rtc = HECMW_mesh_edge_info( global_mesh, edge_data );
    if( rtc != 0 )  goto error;


    switch( cont_data->method ) {
    case HECMW_PART_METHOD_RCB:     /* RCB */
        rtc = rcb_partition( global_mesh->n_node, global_mesh->node,
                             global_mesh->node_ID, cont_data );
        if( rtc != RTC_NORMAL )  goto error;

        for( n_edgecut=0, i=0; i<edge_data->n_edge; i++ ) {
            if( global_mesh->node_ID[2*(edge_data->edge_node_item[2*i]  -1)+1] !=
                global_mesh->node_ID[2*(edge_data->edge_node_item[2*i+1]-1)+1] ) {
                n_edgecut++;
            }
        }

        break;

    case HECMW_PART_METHOD_KMETIS:  /* kMETIS */
    case HECMW_PART_METHOD_PMETIS:  /* pMETIS */
        n_edgecut = metis_partition_nb( global_mesh, cont_data, edge_data );
        if( n_edgecut < 0 )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PMETHOD, "" );
        goto error;
    }


    rtc = HECMW_part_set_log_n_edgecut( edge_data->n_edge, n_edgecut );
    if( rtc != RTC_NORMAL )  goto error;


    /* commented out by K.Goto; begin */
    /* rtc = eqn_block( global_mesh ); */
    /* if( rtc != RTC_NORMAL )  goto error; */
    /* commented out by K.Goto; end */

    HECMW_free( edge_data->edge_node_item );
    HECMW_free( edge_data );

    return RTC_NORMAL;

error:
    if( edge_data ) {
        HECMW_free( edge_data->edge_node_item );
    }
    HECMW_free( edge_data );

    return RTC_ERROR;
}


static int
set_node_belong_domain_eb( struct hecmwST_local_mesh *global_mesh )
{
    int node;
    int i, j;

    for( i=0; i<global_mesh->n_node; i++ ) {
        global_mesh->node_ID[2*i+1] = global_mesh->n_subdomain;
    }

    for( i=0; i<global_mesh->n_elem; i++ ) {
        for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
            node = global_mesh->elem_node_item[j];
            if( global_mesh->elem_ID[2*i+1] < global_mesh->node_ID[2*(node-1)+1] ) {
                global_mesh->node_ID[2*(node-1)+1] = global_mesh->elem_ID[2*i+1];
            }
        }
    }

    return RTC_NORMAL;
}


static int
set_local_node_id( struct hecmwST_local_mesh *global_mesh )
{
    int counter, sum;
    int i, j;

    for( sum=0, i=0; i<global_mesh->n_subdomain; i++ ) {
        for( counter=0, j=0; j<global_mesh->n_node; j++ ) {
            if( global_mesh->node_ID[2*j+1] == i ) {
                global_mesh->node_ID[2*j] = ++counter;
                sum++;
            }
        }
    }
    HECMW_assert( sum == global_mesh->n_node );

    return RTC_NORMAL;
}


static int
wnumbering_node( struct hecmwST_local_mesh *global_mesh,
                 const struct hecmw_part_cont_data *cont_data )
{
    int rtc;
    int i;

    HECMW_free( global_mesh->node_ID );
    global_mesh->node_ID = (int *)HECMW_malloc( sizeof(int)*global_mesh->n_node*2 );
    if( global_mesh->node_ID == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_node; i++ ) {
            global_mesh->node_ID[2*i]   = i+1;
            global_mesh->node_ID[2*i+1] = 0;
        }
    }

    if( global_mesh->n_subdomain == 1 )  return RTC_NORMAL;


    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:  /* for node-based partitioning */
        rtc = set_node_belong_domain_nb( global_mesh, cont_data );
        if( rtc != RTC_NORMAL )  goto error;
        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:  /* for element-based partitioning */
        rtc = set_node_belong_domain_eb( global_mesh );
        if( rtc != RTC_NORMAL )  goto error;
        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "" );
        goto error;
    }


    rtc = set_local_node_id( global_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
set_elem_belong_domain_nb( struct hecmwST_local_mesh *global_mesh )
{
    int node, node_domain, min_domain;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        min_domain = global_mesh->n_subdomain;
        for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
            node        = global_mesh->elem_node_item[j];
            node_domain = global_mesh->node_ID[2*(node-1)+1];
            if( node_domain < min_domain ) {
                min_domain = node_domain;
            }
        }
        global_mesh->elem_ID[2*i+1] = min_domain;
    }

    return RTC_NORMAL;
}


static int
count_edge_for_eb( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmw_part_edge_data *elem_data,
                   int *elem_graph_index, int *elem_graph_item )
{
    int rtc;
    int i, j;

    rtc = HECMW_mesh_hsort_edge_init( global_mesh->n_node, global_mesh->n_elem );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        for( j=elem_graph_index[i]; j<elem_graph_index[i+1]; j++ ) {
            rtc = HECMW_mesh_hsort_edge( i+1, elem_graph_item[j]+1 );
            if( rtc < 0 )  goto error;
        }
    }

    elem_data->n_edge = HECMW_mesh_hsort_edge_get_n( );
    if( elem_data->n_edge < 0 )  goto error;

    elem_data->edge_node_item = HECMW_mesh_hsort_edge_get_v( );
    if( elem_data->edge_node_item == NULL )  goto error;

    HECMW_mesh_hsort_edge_final( );

    return RTC_NORMAL;

error:
    HECMW_mesh_hsort_edge_final( );

    return RTC_ERROR;
}


static int
set_elem_belong_domain_eb( struct hecmwST_local_mesh *global_mesh,
                           const struct hecmw_part_cont_data *cont_data )
{
    int n_edgecut = 0;
    int *elem_graph_index = NULL;
    int *elem_graph_item = NULL;
    struct hecmw_part_edge_data *elem_data = NULL;
    int rtc;
    int i;


    elem_graph_index = (int *)HECMW_calloc( global_mesh->n_elem+1, sizeof(int) );
    if( elem_graph_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    elem_data = (struct hecmw_part_edge_data *)HECMW_malloc( sizeof(struct hecmw_part_edge_data) );
    if( elem_data == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        elem_data->n_edge         = 0;
        elem_data->edge_node_item = NULL;
    }

    elem_graph_item = create_elem_graph( global_mesh, elem_graph_index );
    if( elem_graph_item == NULL )  goto error;

    rtc = count_edge_for_eb( global_mesh, elem_data, elem_graph_index, elem_graph_item );
    if( rtc != RTC_NORMAL )  goto error;


    switch( cont_data->method ) {
    case HECMW_PART_METHOD_RCB:     /* RCB */
        rtc = rcb_partition_eb( global_mesh, cont_data );
        if( rtc != RTC_NORMAL )  goto error;

        for( n_edgecut=0, i=0; i<elem_data->n_edge; i++ ) {
            if( global_mesh->elem_ID[2*(elem_data->edge_node_item[2*i  ]-1)+1] !=
                global_mesh->elem_ID[2*(elem_data->edge_node_item[2*i+1]-1)+1] ) {
                n_edgecut++;
            }
        }

        break;

    case HECMW_PART_METHOD_PMETIS:  /* pMETIS */
    case HECMW_PART_METHOD_KMETIS:  /* kMETIS */
        n_edgecut = metis_partition_eb( global_mesh, cont_data, elem_graph_index, elem_graph_item );
        if( n_edgecut < 0 )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PMETHOD, "" );
        goto error;
    }


    rtc = HECMW_part_set_log_n_edgecut( elem_data->n_edge, n_edgecut );
    if( rtc != RTC_NORMAL )  goto error;

    HECMW_free( elem_graph_index );
    HECMW_free( elem_graph_item );
    HECMW_free( elem_data->edge_node_item );
    HECMW_free( elem_data );

    return RTC_NORMAL;

error:
    HECMW_free( elem_graph_index );
    HECMW_free( elem_graph_item );
    if( elem_data ) {
        HECMW_free( elem_data->edge_node_item );
    }
    HECMW_free( elem_data );

    return RTC_ERROR;
}


static int
set_local_elem_id( struct hecmwST_local_mesh *global_mesh )
{
    int counter, sum;
    int i, j;

    for( sum=0, i=0; i<global_mesh->n_subdomain; i++ ) {
        for( counter=0, j=0; j<global_mesh->n_elem; j++ ) {
            if( global_mesh->elem_ID[2*j+1] == i ) {
                global_mesh->elem_ID[2*j] = ++counter;
                sum++;
            }
        }
    }
    HECMW_assert( sum == global_mesh->n_elem );

    return RTC_NORMAL;
}


static int
wnumbering_elem( struct hecmwST_local_mesh *global_mesh,
                 const struct hecmw_part_cont_data *cont_data )
{
    int rtc;
    int i;

    HECMW_free( global_mesh->elem_ID );
    global_mesh->elem_ID = (int *)HECMW_malloc( sizeof(int)*global_mesh->n_elem*2 );
    if( global_mesh->elem_ID == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<global_mesh->n_elem; i++ ) {
            global_mesh->elem_ID[2*i]   = i+1;
            global_mesh->elem_ID[2*i+1] = 0;
        }
    }

    if( global_mesh->n_subdomain == 1 )  return RTC_NORMAL;


    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:  /* for node-based partitioning */
        rtc = set_elem_belong_domain_nb( global_mesh );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:  /* for element-based partitioning */
        rtc = set_elem_belong_domain_eb( global_mesh, cont_data );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "" );
        goto error;
    }


    rtc = set_local_elem_id( global_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
wnumbering( struct hecmwST_local_mesh *global_mesh, const struct hecmw_part_cont_data *cont_data )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( cont_data );


    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Starting double numbering..." );
    }


    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:  /* for node-based partitioning */
        rtc = wnumbering_node( global_mesh, cont_data );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = wnumbering_elem( global_mesh, cont_data );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:  /* for element-based partitioning */
        rtc = wnumbering_elem( global_mesh, cont_data );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = wnumbering_node( global_mesh, cont_data );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "" );
        goto error;
    }


    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Double numbering done" );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*==================================================================================================


  create neighboring domain & communication information


==================================================================================================*/

static int
mask_node_by_domain( const struct hecmwST_local_mesh *global_mesh,
                     char *node_flag, int current_domain )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {
        ( global_mesh->node_ID[2*i+1] == current_domain ) ?
            MASK_BIT( node_flag[i], INTERNAL ) :
            MASK_BIT( node_flag[i], EXTERNAL );
    }

    return RTC_NORMAL;
}


static int
mask_elem_by_domain( const struct hecmwST_local_mesh *global_mesh,
                     char *elem_flag, int current_domain )
{
    int i;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        ( global_mesh->elem_ID[2*i+1] == current_domain ) ?
            MASK_BIT( elem_flag[i], INTERNAL ) :
            MASK_BIT( elem_flag[i], EXTERNAL );
    }

    return RTC_NORMAL;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
mask_overlap_elem( const struct hecmwST_local_mesh *global_mesh,
                   const char *node_flag, char *elem_flag )
{
    int node, evalsum_int, evalsum_ext;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        evalsum_int = 0;
        evalsum_ext = 0;
        for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
            node = global_mesh->elem_node_item[j];
            EVAL_BIT( node_flag[node-1], INTERNAL ) ? evalsum_int++ : evalsum_ext++;
        }

        if( evalsum_int && evalsum_ext ) {
            MASK_BIT( elem_flag[i], OVERLAP );
            MASK_BIT( elem_flag[i], BOUNDARY );
        }
    }

    return RTC_NORMAL;
}


static int
mask_boundary_node( const struct hecmwST_local_mesh *global_mesh,
                    char *node_flag, const char *elem_flag )
{
    int node;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], BOUNDARY ) ) {
            for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
                node = global_mesh->elem_node_item[j];
                MASK_BIT( node_flag[node-1], OVERLAP );
                MASK_BIT( node_flag[node-1], BOUNDARY );
            }
        }
    }

    return RTC_NORMAL;
}


static int
mask_additional_overlap_elem( const struct hecmwST_local_mesh *global_mesh,
                              const char *node_flag, char *elem_flag )
{
    int node, evalsum;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        evalsum = 0;
        for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
            node = global_mesh->elem_node_item[j];
            evalsum += ( EVAL_BIT( node_flag[node-1], BOUNDARY ) );
        }

        if( evalsum ) {
            MASK_BIT( elem_flag[i], OVERLAP );
            MASK_BIT( elem_flag[i], BOUNDARY );
        }
    }

    return RTC_NORMAL;
}


static int
mask_mesh_status_nb( const struct hecmwST_local_mesh *global_mesh,
                     char *node_flag, char *elem_flag, int current_domain )
{
    int rtc;
    int i;

    for( i=0; i<global_mesh->n_node; i++ ){
        CLEAR_BIT( node_flag[i], INTERNAL );
        CLEAR_BIT( node_flag[i], EXTERNAL );
        CLEAR_BIT( node_flag[i], BOUNDARY );
    }
    for( i=0; i<global_mesh->n_elem; i++ ){
        CLEAR_BIT( elem_flag[i], INTERNAL );
        CLEAR_BIT( elem_flag[i], EXTERNAL );
        CLEAR_BIT( elem_flag[i], BOUNDARY );
    }

    rtc = mask_node_by_domain( global_mesh, node_flag, current_domain );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = mask_elem_by_domain( global_mesh, elem_flag, current_domain );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = mask_overlap_elem( global_mesh, node_flag, elem_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = mask_boundary_node( global_mesh, node_flag, elem_flag );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=1; i<global_mesh->hecmw_flag_partdepth; i++ ) {
        rtc = mask_additional_overlap_elem( global_mesh, node_flag, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_boundary_node( global_mesh, node_flag, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
mask_overlap_node_mark( const struct hecmwST_local_mesh *global_mesh,
                        char *node_flag, const char *elem_flag )
{
    int node;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {


        if( EVAL_BIT( elem_flag[i], INTERNAL ) ) {
            for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
                node = global_mesh->elem_node_item[j];
                MASK_BIT( node_flag[node-1], MARK );
            }


        } else {
            for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
                node = global_mesh->elem_node_item[j];
                MASK_BIT( node_flag[node-1], MASK );
            }
        }
    }

    return RTC_NORMAL;
}


static int
mask_overlap_node_inner( const struct hecmwST_local_mesh *global_mesh, char *node_flag )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], MARK ) && EVAL_BIT( node_flag[i], MASK ) ) {
            MASK_BIT( node_flag[i], OVERLAP );
            MASK_BIT( node_flag[i], BOUNDARY );
        }
    }

    return RTC_NORMAL;
}


static int
mask_overlap_node( const struct hecmwST_local_mesh *global_mesh,
                   char *node_flag, const char *elem_flag )
{
    int rtc;
    int i;

    rtc = mask_overlap_node_mark( global_mesh, node_flag, elem_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = mask_overlap_node_inner( global_mesh, node_flag );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<global_mesh->n_node; i++ ) {
        CLEAR_BIT( node_flag[i], MASK );
        CLEAR_BIT( node_flag[i], MARK );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
mask_boundary_elem( const struct hecmwST_local_mesh *global_mesh,
                    const char *node_flag, char *elem_flag )
{
    int node, evalsum;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        evalsum = 0;
        for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
            node = global_mesh->elem_node_item[j];
            if( EVAL_BIT( node_flag[node-1], BOUNDARY ) )  evalsum++;
        }

        if( evalsum ) {
            MASK_BIT( elem_flag[i], OVERLAP );
            MASK_BIT( elem_flag[i], BOUNDARY );
        }
    }

    return RTC_NORMAL;
}

#if 0

static int
mask_additional_overlap_node( const struct hecmwST_local_mesh *global_mesh,
                              char *node_flag, char *elem_flag )
{
    int node;
    int i, j;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], BOUNDARY ) ) {
            for( j=global_mesh->elem_node_index[i]; j<global_mesh->elem_node_index[i+1]; j++ ) {
                node = global_mesh->elem_node_item[j];
                MASK_BIT( node_flag[node-1], OVERLAP );
                MASK_BIT( node_flag[node-1], BOUNDARY );
            }
        }
    }

    return RTC_NORMAL;
}
#endif


static int
mask_mesh_status_eb( const struct hecmwST_local_mesh *global_mesh,
                     char *node_flag, char *elem_flag, int current_domain )
{
    int rtc;
    int i;

    for( i=0; i<global_mesh->n_node; i++ ){
        CLEAR_BIT( node_flag[i], INTERNAL );
        CLEAR_BIT( node_flag[i], EXTERNAL );
        CLEAR_BIT( node_flag[i], BOUNDARY );
    }
    for( i=0; i<global_mesh->n_elem; i++ ){
        CLEAR_BIT( elem_flag[i], INTERNAL );
        CLEAR_BIT( elem_flag[i], EXTERNAL );
        CLEAR_BIT( elem_flag[i], BOUNDARY );
    }

    rtc = mask_node_by_domain( global_mesh, node_flag, current_domain );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = mask_elem_by_domain( global_mesh, elem_flag, current_domain );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = mask_overlap_node( global_mesh, node_flag, elem_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = mask_boundary_elem( global_mesh, node_flag, elem_flag );
    if( rtc != RTC_NORMAL )  goto error;

#if 0
    for( i=1; i<global_mesh->hecmw_flag_partdepth; i++ ) {
        rtc = mask_additional_overlap_node( global_mesh, node_flag, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_boundary_elem( global_mesh, node_flag, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;
    }
#endif

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
mask_neighbor_domain_nb( const struct hecmwST_local_mesh *global_mesh,
                         const char *node_flag, char *domain_flag )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], EXTERNAL ) && EVAL_BIT( node_flag[i], BOUNDARY ) ) {
            MASK_BIT( domain_flag[global_mesh->node_ID[2*i+1]], MASK );
        }
    }

    return RTC_NORMAL;
}


static int
mask_neighbor_domain_eb( const struct hecmwST_local_mesh *global_mesh,
                         const char *elem_flag, char *domain_flag )
{
    int i;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], EXTERNAL ) && EVAL_BIT( elem_flag[i], BOUNDARY ) ) {
            MASK_BIT( domain_flag[global_mesh->elem_ID[2*i+1]], MASK );
        }
    }

    return RTC_NORMAL;
}


static int
count_neighbor_domain( const struct hecmwST_local_mesh *global_mesh, const char *domain_flag )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_subdomain; i++ ) {
        if( EVAL_BIT( domain_flag[i], MASK ) )  counter++;
    }

    return counter;
}


static int
set_neighbor_domain( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh, const char *domain_flag )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_subdomain; i++ ) {
        if( EVAL_BIT( domain_flag[i], MASK ) ) {
            local_mesh->neighbor_pe[counter++] = i;
        }
    }

    return counter;
}


static int
create_neighbor_info( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh,
                      char *node_flag, char *elem_flag, int current_domain )
{
    int rtc;
    char *domain_flag = NULL;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_flag );
    HECMW_assert( elem_flag );

    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Starting creation of neighboring domain information..." );
    }

    local_mesh->n_neighbor_pe = 0;
    local_mesh->neighbor_pe   = NULL;


    domain_flag = (char *)HECMW_calloc( global_mesh->n_subdomain, sizeof(char) );
    if( domain_flag == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:  /* for node-based partitioning */
        rtc = mask_mesh_status_nb( global_mesh, node_flag, elem_flag, current_domain );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_neighbor_domain_nb( global_mesh, node_flag, domain_flag );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:  /* for element-based partitioning */
        rtc = mask_mesh_status_eb( global_mesh, node_flag, elem_flag, current_domain );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_neighbor_domain_eb( global_mesh, elem_flag, domain_flag );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "" );
        goto error;
    }


    local_mesh->n_neighbor_pe = count_neighbor_domain( global_mesh, domain_flag );
    if( local_mesh->n_neighbor_pe < 0 ) {
        HECMW_set_error( HECMW_PART_E_NNEIGHBORPE_LOWER, "" );
        goto error;
    }


    if( local_mesh->n_neighbor_pe == 0 ) {
        local_mesh->neighbor_pe = NULL;
        HECMW_free( domain_flag );
        return RTC_NORMAL;
    }

    local_mesh->neighbor_pe = (int *)HECMW_malloc( sizeof(int)*local_mesh->n_neighbor_pe );
    if( local_mesh->neighbor_pe == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    rtc = set_neighbor_domain( global_mesh, local_mesh, domain_flag );
    HECMW_assert( rtc == local_mesh->n_neighbor_pe );


    HECMW_free( domain_flag );

    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Creation of neighboring domain information done" );
    }

    return RTC_NORMAL;

error:
    HECMW_free( domain_flag );
    HECMW_free( local_mesh->neighbor_pe );
    local_mesh->n_neighbor_pe = 0;
    local_mesh->neighbor_pe   = NULL;

    return RTC_ERROR;
}

/*================================================================================================*/

static int
mask_comm_node( const struct hecmwST_local_mesh *global_mesh,
                char *node_flag_current, char *node_flag_neighbor )
{
    int i;

    for( i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag_current[i], BOUNDARY ) &&
            EVAL_BIT( node_flag_neighbor[i], BOUNDARY ) ) {
            MASK_BIT( node_flag_current[i], MASK );
        }
    }

    return RTC_NORMAL;
}


static int
mask_comm_elem( const struct hecmwST_local_mesh *global_mesh,
                char *elem_flag_current, char *elem_flag_neighbor )
{
    int i;

    for( i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag_current[i], BOUNDARY ) &&
            EVAL_BIT( elem_flag_neighbor[i], BOUNDARY ) ) {
            MASK_BIT( elem_flag_current[i], MASK );
        }
    }

    return RTC_NORMAL;
}


static int
count_masked_comm_node( const struct hecmwST_local_mesh *global_mesh,
                        const char *node_flag, int domain )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], MASK ) && global_mesh->node_ID[2*i+1] == domain )  counter++;
    }

    return counter;
}


static int
count_masked_comm_elem( const struct hecmwST_local_mesh *global_mesh,
                        const char *elem_flag, int domain )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], MASK ) && global_mesh->elem_ID[2*i+1] == domain )  counter++;
    }

    return counter;
}


static int
count_masked_shared_node( const struct hecmwST_local_mesh *global_mesh, const char *node_flag )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], MASK ) )  counter++;
    }

    return counter;
}


static int
count_masked_shared_elem( const struct hecmwST_local_mesh *global_mesh, const char *elem_flag )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], MASK ) )  counter++;
    }

    return counter;
}


static int
create_comm_node_pre( const struct hecmwST_local_mesh *global_mesh,
                      const char *node_flag, int **comm_node, int neighbor_idx, int domain )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], MASK ) && global_mesh->node_ID[2*i+1] == domain ) {
            comm_node[neighbor_idx][counter++] = i + 1;
        }
    }

    return counter;
}


static int
create_comm_elem_pre( const struct hecmwST_local_mesh *global_mesh,
                      const char *elem_flag, int **comm_elem, int neighbor_idx, int domain )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], MASK ) && global_mesh->elem_ID[2*i+1] == domain ) {
            comm_elem[neighbor_idx][counter++] = i + 1;
        }
    }

    return counter;
}


static int
create_shared_node_pre( const struct hecmwST_local_mesh *global_mesh,
                        const char *node_flag, int **shared_node, int neighbor_idx )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], MASK ) ) {
            shared_node[neighbor_idx][counter++] = i + 1;
        }
    }

    return counter;
}


static int
create_shared_elem_pre( const struct hecmwST_local_mesh *global_mesh,
                        const char *elem_flag, int **shared_elem, int neighbor_idx )
{
    int counter;
    int i;

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], MASK ) ) {
            shared_elem[neighbor_idx][counter++] = i + 1;
        }
    }

    return counter;
}


static int
create_comm_item( int n_neighbor_pe, int **comm_item_pre, int *comm_index, int *comm_item )
{
    int i, j, js, je;

    for( i=0; i<n_neighbor_pe; i++ ) {
        js = comm_index[i];
        je = comm_index[i+1];

        for( j=0; j<je-js; j++ ) {
            comm_item[js+j] = comm_item_pre[i][j];
        }
    }

    return RTC_NORMAL;
}

/*------------------------------------------------------------------------------------------------*/

static int
create_import_info_nb( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const char *node_flag, int **import_node,
                       int neighbor_idx, int neighbor_domain )
{
    int n_import_node, rtc;


    n_import_node = count_masked_comm_node( global_mesh, node_flag, neighbor_domain );
    HECMW_assert( n_import_node >= 0 );


    local_mesh->import_index[neighbor_idx+1] = local_mesh->import_index[neighbor_idx]+n_import_node;


    import_node[neighbor_idx] = (int *)HECMW_malloc( sizeof(int)*n_import_node );
    if( import_node[neighbor_idx] == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_node_pre( global_mesh,
                                node_flag, import_node, neighbor_idx, neighbor_domain );
    HECMW_assert( rtc == n_import_node );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
create_export_info_nb( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const char *node_flag, int **export_node,
                       int neighbor_idx, int current_domain, int neighbor_domain )
{
    int n_export_node, rtc;


    n_export_node = count_masked_comm_node( global_mesh, node_flag, current_domain );
    HECMW_assert( n_export_node >= 0 );


    local_mesh->export_index[neighbor_idx+1] = local_mesh->export_index[neighbor_idx]+n_export_node;


    export_node[neighbor_idx] = (int *)HECMW_malloc( sizeof(int)*n_export_node );
    if( export_node[neighbor_idx] == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_node_pre( global_mesh, node_flag, export_node, neighbor_idx, current_domain );
    HECMW_assert( rtc == n_export_node );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
create_shared_info_nb( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const char *elem_flag, int **shared_elem,
                       int neighbor_idx, int neighbor_domain )
{
    int n_shared_elem, rtc;


    n_shared_elem = count_masked_shared_elem( global_mesh, elem_flag );
    HECMW_assert( n_shared_elem >= 0 );


    local_mesh->shared_index[neighbor_idx+1] = local_mesh->shared_index[neighbor_idx]+n_shared_elem;


    shared_elem[neighbor_idx] = (int *)HECMW_malloc( sizeof(int)*n_shared_elem );
    if( shared_elem[neighbor_idx] == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_shared_elem_pre( global_mesh, elem_flag, shared_elem, neighbor_idx );
    HECMW_assert( rtc == n_shared_elem );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
create_comm_info_nb( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh,
                     char *node_flag, char *elem_flag, int current_domain )
{
    char *node_flag_neighbor = NULL;
    char *elem_flag_neighbor = NULL;
    int **import_node = NULL;
    int **export_node = NULL;
    int **shared_elem = NULL;
    int neighbor_domain;
    int size;
    int rtc;
    int i, j;

    local_mesh->import_index = NULL;
    local_mesh->export_index = NULL;
    local_mesh->shared_index = NULL;
    local_mesh->import_item = NULL;
    local_mesh->export_item = NULL;
    local_mesh->shared_item = NULL;

    import_node = (int **)HECMW_malloc( sizeof(int *)*local_mesh->n_neighbor_pe );
    if( import_node == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            import_node[i] = NULL;
        }
    }
    export_node = (int **)HECMW_malloc( sizeof(int *)*local_mesh->n_neighbor_pe );
    if( export_node == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            export_node[i] = NULL;
        }
    }
    shared_elem = (int **)HECMW_malloc( sizeof(int *)*local_mesh->n_neighbor_pe );
    if( shared_elem == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            shared_elem[i] = NULL;
        }
    }

    local_mesh->import_index = (int *)HECMW_calloc( local_mesh->n_neighbor_pe+1, sizeof(int) );
    if( local_mesh->import_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    local_mesh->export_index = (int *)HECMW_calloc( local_mesh->n_neighbor_pe+1, sizeof(int) );
    if( local_mesh->export_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    local_mesh->shared_index = (int *)HECMW_calloc( local_mesh->n_neighbor_pe+1, sizeof(int) );
    if( local_mesh->shared_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    node_flag_neighbor = (char *)HECMW_malloc( sizeof(char)*global_mesh->n_node );
    if( node_flag_neighbor == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    elem_flag_neighbor = (char *)HECMW_malloc( sizeof(char)*global_mesh->n_elem );
    if( elem_flag_neighbor == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        neighbor_domain = local_mesh->neighbor_pe[i];

        for( j=0; j<global_mesh->n_node; j++ ) {
            CLEAR_BIT( node_flag[j], MASK );
            CLEAR_BIT( node_flag[j], MARK );
        }
        for( j=0; j<global_mesh->n_elem; j++ ) {
            CLEAR_BIT( elem_flag[j], MASK );
            CLEAR_BIT( elem_flag[j], MARK );
        }

        memset( node_flag_neighbor, 0, sizeof(char)*global_mesh->n_node );
        memset( elem_flag_neighbor, 0, sizeof(char)*global_mesh->n_elem );

        rtc = mask_mesh_status_nb( global_mesh,
                                   node_flag_neighbor, elem_flag_neighbor, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_comm_node( global_mesh, node_flag, node_flag_neighbor );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_comm_elem( global_mesh, elem_flag, elem_flag_neighbor );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = create_import_info_nb( global_mesh, local_mesh, node_flag,
                                     import_node, i, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = create_export_info_nb( global_mesh, local_mesh, node_flag,
                                     export_node, i, current_domain, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = create_shared_info_nb( global_mesh, local_mesh, elem_flag,
                                     shared_elem, i, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;
    }

    HECMW_free( node_flag_neighbor );
    HECMW_free( elem_flag_neighbor );
    node_flag_neighbor = NULL;
    elem_flag_neighbor = NULL;


    size = sizeof(int) * local_mesh->import_index[local_mesh->n_neighbor_pe];
    local_mesh->import_item = (int *)HECMW_malloc( size );
    if( local_mesh->import_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_item( local_mesh->n_neighbor_pe, import_node,
                            local_mesh->import_index, local_mesh->import_item );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        HECMW_free( import_node[i] );
    }
    HECMW_free( import_node );
    import_node = NULL;


    size = sizeof(int) * local_mesh->export_index[local_mesh->n_neighbor_pe];
    local_mesh->export_item = (int *)HECMW_malloc( size );
    if( local_mesh->export_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_item( local_mesh->n_neighbor_pe, export_node,
                            local_mesh->export_index, local_mesh->export_item );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        HECMW_free( export_node[i] );
    }
    HECMW_free( export_node );
    export_node = NULL;


    size = sizeof(int) * local_mesh->shared_index[local_mesh->n_neighbor_pe];
    local_mesh->shared_item = (int *)HECMW_malloc( size );
    if( local_mesh->shared_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_item( local_mesh->n_neighbor_pe, shared_elem,
                            local_mesh->shared_index, local_mesh->shared_item );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        HECMW_free( shared_elem[i] );
    }
    HECMW_free( shared_elem );
    shared_elem = NULL;

    return RTC_NORMAL;

error:
    if( import_node ) {
        int i;
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            HECMW_free( import_node[i] );
        }
        HECMW_free( import_node );
    }
    if( export_node ) {
        int i;
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            HECMW_free( export_node[i] );
        }
        HECMW_free( export_node );
    }
    if( shared_elem ) {
        int i;
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            HECMW_free( shared_elem[i] );
        }
        HECMW_free( shared_elem );
    }
    HECMW_free( node_flag_neighbor );
    HECMW_free( elem_flag_neighbor );
    HECMW_free( local_mesh->import_index );
    HECMW_free( local_mesh->export_index );
    HECMW_free( local_mesh->shared_index );
    HECMW_free( local_mesh->import_item );
    HECMW_free( local_mesh->export_item );
    HECMW_free( local_mesh->shared_item );

    local_mesh->import_index = NULL;
    local_mesh->export_index = NULL;
    local_mesh->shared_index = NULL;
    local_mesh->import_item = NULL;
    local_mesh->export_item = NULL;
    local_mesh->shared_item = NULL;

    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
create_import_info_eb( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const char *elem_flag, int **import_elem,
                       int neighbor_idx, int neighbor_domain )
{
    int n_import_elem, rtc;


    n_import_elem = count_masked_comm_elem( global_mesh, elem_flag, neighbor_domain );
    HECMW_assert( n_import_elem >= 0 );


    local_mesh->import_index[neighbor_idx+1] = local_mesh->import_index[neighbor_idx]+n_import_elem;


    import_elem[neighbor_idx] = (int *)HECMW_malloc( sizeof(int)*n_import_elem );
    if( import_elem[neighbor_idx] == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_elem_pre( global_mesh, elem_flag, import_elem,
                                neighbor_idx, neighbor_domain );
    HECMW_assert( rtc == n_import_elem );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
create_export_info_eb( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const char *elem_flag, int **export_elem,
                       int neighbor_idx, int current_domain, int neighbor_domain )
{
    int n_export_elem, rtc;


    n_export_elem = count_masked_comm_elem( global_mesh, elem_flag, current_domain );
    HECMW_assert( n_export_elem >= 0 );


    local_mesh->export_index[neighbor_idx+1] = local_mesh->export_index[neighbor_idx]+n_export_elem;


    export_elem[neighbor_idx] = (int *)HECMW_malloc( sizeof(int)*n_export_elem );
    if( export_elem[neighbor_idx] == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_elem_pre( global_mesh, elem_flag, export_elem, neighbor_idx, current_domain );
    HECMW_assert( rtc == n_export_elem );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
create_shared_info_eb( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const char *node_flag, int **shared_node,
                       int neighbor_idx, int neighbor_domain )
{
    int n_shared_node, rtc;


    n_shared_node = count_masked_shared_node( global_mesh, node_flag );
    HECMW_assert( n_shared_node >= 0 );


    local_mesh->shared_index[neighbor_idx+1] = local_mesh->shared_index[neighbor_idx]+n_shared_node;


    shared_node[neighbor_idx] = (int *)HECMW_malloc( sizeof(int)*n_shared_node );
    if( shared_node[neighbor_idx] == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_shared_node_pre( global_mesh, node_flag, shared_node, neighbor_idx );
    HECMW_assert( rtc == n_shared_node );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
create_comm_info_eb( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh,
                     char *node_flag, char *elem_flag, int current_domain )
{
    char *node_flag_neighbor = NULL;
    char *elem_flag_neighbor = NULL;
    int **import_elem = NULL;
    int **export_elem = NULL;
    int **shared_node = NULL;
    int neighbor_domain;
    int size;
    int rtc;
    int i, j;

    /* allocation */
    local_mesh->import_index = NULL;
    local_mesh->export_index = NULL;
    local_mesh->shared_index = NULL;
    local_mesh->import_item = NULL;
    local_mesh->export_item = NULL;
    local_mesh->shared_item = NULL;

    import_elem = (int **)HECMW_malloc( sizeof(int *)*local_mesh->n_neighbor_pe );
    if( import_elem == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            import_elem[i] = NULL;
        }
    }
    export_elem = (int **)HECMW_malloc( sizeof(int *)*local_mesh->n_neighbor_pe );
    if( export_elem == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            export_elem[i] = NULL;
        }
    }
    shared_node = (int **)HECMW_malloc( sizeof(int *)*local_mesh->n_neighbor_pe );
    if( shared_node == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            shared_node[i] = NULL;
        }
    }

    local_mesh->import_index = (int *)HECMW_calloc( local_mesh->n_neighbor_pe+1, sizeof(int) );
    if( local_mesh->import_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    local_mesh->export_index = (int *)HECMW_calloc( local_mesh->n_neighbor_pe+1, sizeof(int) );
    if( local_mesh->export_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    local_mesh->shared_index = (int *)HECMW_calloc( local_mesh->n_neighbor_pe+1, sizeof(int) );
    if( local_mesh->shared_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    node_flag_neighbor = (char *)HECMW_malloc( sizeof(char)*global_mesh->n_node );
    if( node_flag_neighbor == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    elem_flag_neighbor = (char *)HECMW_malloc( sizeof(char)*global_mesh->n_elem );
    if( elem_flag_neighbor == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    /* create communication table */
    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        neighbor_domain = local_mesh->neighbor_pe[i];

        for( j=0; j<global_mesh->n_node; j++ ) {
            CLEAR_BIT( node_flag[j], MASK );
            CLEAR_BIT( node_flag[j], MARK );
        }
        for( j=0; j<global_mesh->n_elem; j++ ) {
            CLEAR_BIT( elem_flag[j], MASK );
            CLEAR_BIT( elem_flag[j], MARK );
        }

        memset( node_flag_neighbor, 0, sizeof(char)*global_mesh->n_node );
        memset( elem_flag_neighbor, 0, sizeof(char)*global_mesh->n_elem );

        /* mask boundary node & element */
        rtc = mask_mesh_status_eb( global_mesh,
                                   node_flag_neighbor, elem_flag_neighbor, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_comm_node( global_mesh, node_flag, node_flag_neighbor );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = mask_comm_elem( global_mesh, elem_flag, elem_flag_neighbor );
        if( rtc != RTC_NORMAL )  goto error;

        /* create import element information (preliminary) */
        rtc = create_import_info_eb( global_mesh, local_mesh, elem_flag,
                                     import_elem, i, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;

        /* create export element information (preliminary) */
        rtc = create_export_info_eb( global_mesh, local_mesh, elem_flag,
                                     export_elem, i, current_domain, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;

        /* create shared node information (preliminary) */
        rtc = create_shared_info_eb( global_mesh, local_mesh, node_flag,
                                     shared_node, i, neighbor_domain );
        if( rtc != RTC_NORMAL )  goto error;
    }

    HECMW_free( node_flag_neighbor );
    HECMW_free( elem_flag_neighbor );
    node_flag_neighbor = NULL;
    elem_flag_neighbor = NULL;

    /* create import element information */
    size = sizeof(int) * local_mesh->import_index[local_mesh->n_neighbor_pe];
    local_mesh->import_item = (int *)HECMW_malloc( size );
    if( local_mesh->import_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_item( local_mesh->n_neighbor_pe, import_elem,
                            local_mesh->import_index, local_mesh->import_item );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        HECMW_free( import_elem[i] );
    }
    HECMW_free( import_elem );
    import_elem = NULL;

    /* create export node information */
    size = sizeof(int) * local_mesh->export_index[local_mesh->n_neighbor_pe];
    local_mesh->export_item = (int *)HECMW_malloc( size );
    if( local_mesh->export_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_item( local_mesh->n_neighbor_pe, export_elem,
                            local_mesh->export_index, local_mesh->export_item );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        HECMW_free( export_elem[i] );
    }
    HECMW_free( export_elem );
    export_elem = NULL;

    /* create shared element information */
    size = sizeof(int) * local_mesh->shared_index[local_mesh->n_neighbor_pe];
    local_mesh->shared_item = (int *)HECMW_malloc( size );
    if( local_mesh->shared_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = create_comm_item( local_mesh->n_neighbor_pe, shared_node,
                            local_mesh->shared_index, local_mesh->shared_item );
    if( rtc != RTC_NORMAL )  goto error;

    for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
        HECMW_free( shared_node[i] );
    }
    HECMW_free( shared_node );
    shared_node = NULL;

    return RTC_NORMAL;

error:
    if( import_elem ) {
        int i;
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            HECMW_free( import_elem[i] );
        }
        HECMW_free( import_elem );
    }
    if( export_elem ) {
        int i;
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            HECMW_free( export_elem[i] );
        }
        HECMW_free( export_elem );
    }
    if( shared_node ) {
        int i;
        for( i=0; i<local_mesh->n_neighbor_pe; i++ ) {
            HECMW_free( shared_node[i] );
        }
        HECMW_free( shared_node );
    }
    HECMW_free( node_flag_neighbor );
    HECMW_free( elem_flag_neighbor );
    HECMW_free( local_mesh->import_index );
    HECMW_free( local_mesh->export_index );
    HECMW_free( local_mesh->shared_index );
    HECMW_free( local_mesh->import_item );
    HECMW_free( local_mesh->export_item );
    HECMW_free( local_mesh->shared_item );

    local_mesh->import_index = NULL;
    local_mesh->export_index = NULL;
    local_mesh->shared_index = NULL;
    local_mesh->import_item = NULL;
    local_mesh->export_item = NULL;
    local_mesh->shared_item = NULL;

    return RTC_ERROR;
}

/*================================================================================================*/

static int
create_comm_info( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh,
                  char *node_flag, char *elem_flag, int current_domain )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_flag );
    HECMW_assert( elem_flag );

    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Starting creation of interface table..." );
    }

    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:  /* for node-based partitioning */
        rtc = create_comm_info_nb( global_mesh, local_mesh, node_flag, elem_flag, current_domain );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:  /* for element-based partitioning */
        rtc = create_comm_info_eb( global_mesh, local_mesh, node_flag, elem_flag, current_domain );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "" );
        goto error;
    }

    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Creation of interface table done" );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


/*==================================================================================================

   create distributed mesh information

==================================================================================================*/

static int
set_node_global2local_internal( const struct hecmwST_local_mesh *global_mesh,
                                struct hecmwST_local_mesh *local_mesh,
                                int *node_global2local, const char *node_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( node_flag );
    HECMW_assert( global_mesh->n_node > 0 );

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], INTERNAL ) ) {
            node_global2local[i] = ++counter;
        }
    }
    local_mesh->nn_internal = counter;

    return RTC_NORMAL;
}


static int
set_node_global2local_external( const struct hecmwST_local_mesh *global_mesh,
                                struct hecmwST_local_mesh *local_mesh,
                                int *node_global2local, const char *node_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( node_flag );
    HECMW_assert( global_mesh->n_node > 0 );

    for( counter=local_mesh->nn_internal, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], EXTERNAL ) && EVAL_BIT( node_flag[i], BOUNDARY ) ) {
            node_global2local[i] = ++counter;
        }
    }
    local_mesh->n_node = counter;
    local_mesh->n_node_gross = counter;

    HECMW_assert( local_mesh->n_node > 0 );

    return RTC_NORMAL;
}


static int
set_node_global2local_all( const struct hecmwST_local_mesh *global_mesh,
                           struct hecmwST_local_mesh *local_mesh,
                           int *node_global2local, const char *node_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( node_flag );
    HECMW_assert( global_mesh->n_node > 0 );

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], INTERNAL ) || EVAL_BIT( node_flag[i], BOUNDARY ) ) {
            node_global2local[i] = ++counter;
        }
    }
    local_mesh->n_node = counter;
    local_mesh->n_node_gross = counter;

    HECMW_assert( local_mesh->n_node > 0 );

    return RTC_NORMAL;
}


static int
const_nn_internal( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh, const char *node_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_flag );
    HECMW_assert( global_mesh->n_node > 0 );

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], INTERNAL ) )  counter++;
    }
    local_mesh->nn_internal = counter;

    return 0;
}


static int
const_node_internal_list( const struct hecmwST_local_mesh *global_mesh,
                          struct hecmwST_local_mesh *local_mesh,
                          int *node_global2local, const char *node_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( node_flag );
    HECMW_assert( global_mesh->n_node > 0 );

    if( local_mesh->nn_internal == 0 ) {
        local_mesh->node_internal_list = NULL;
        return RTC_NORMAL;
    }

    local_mesh->node_internal_list = (int *)HECMW_malloc( sizeof(int)*local_mesh->nn_internal );
    if( local_mesh->node_internal_list == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( EVAL_BIT( node_flag[i], INTERNAL ) ) {
            local_mesh->node_internal_list[counter++] = node_global2local[i];
        }
    }
    HECMW_assert( counter == local_mesh->nn_internal );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
set_node_global2local( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       int *node_global2local, const char *node_flag )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( node_flag );

    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:

        rtc = set_node_global2local_internal( global_mesh, local_mesh,
                                              node_global2local, node_flag );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = set_node_global2local_external( global_mesh, local_mesh,
                                              node_global2local, node_flag );
        if( rtc != RTC_NORMAL )  goto error;


        local_mesh->node_internal_list = NULL;

        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:

        rtc = const_nn_internal( global_mesh, local_mesh, node_flag );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = set_node_global2local_all( global_mesh, local_mesh, node_global2local, node_flag );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = const_node_internal_list( global_mesh, local_mesh, node_global2local, node_flag );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "%d", global_mesh->hecmw_flag_parttype );
        goto error;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
set_node_local2global( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const int *node_global2local, int *node_local2global )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( node_local2global );
    HECMW_assert( global_mesh->n_node > 0 );

    for( counter=0, i=0; i<global_mesh->n_node; i++ ) {
        if( node_global2local[i] ) {
            node_local2global[node_global2local[i]-1] = i+1;
            counter++;
        }
    }
    HECMW_assert( counter == local_mesh->n_node );

    return RTC_NORMAL;
}

/*------------------------------------------------------------------------------------------------*/

static int
set_elem_global2local_internal( const struct hecmwST_local_mesh *global_mesh,
                                struct hecmwST_local_mesh *local_mesh,
                                int *elem_global2local, const char *elem_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( elem_global2local );
    HECMW_assert( elem_flag );
    HECMW_assert( global_mesh->n_elem );

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], INTERNAL ) ) {
            elem_global2local[i] = ++counter;
        }
    }
    local_mesh->ne_internal = counter;

    return RTC_NORMAL;
}


static int
set_elem_global2local_external( const struct hecmwST_local_mesh *global_mesh,
                                struct hecmwST_local_mesh *local_mesh,
                                int *elem_global2local, const char *elem_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( elem_global2local );
    HECMW_assert( elem_flag );
    HECMW_assert( global_mesh->n_elem );

    for( counter=local_mesh->ne_internal, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], EXTERNAL ) && EVAL_BIT( elem_flag[i], BOUNDARY ) ) {
            elem_global2local[i] = ++counter;
        }
    }
    local_mesh->n_elem = counter;
    local_mesh->n_elem_gross = counter;

    HECMW_assert( local_mesh->n_elem > 0 );

    return RTC_NORMAL;
}


static int
set_elem_global2local_all( const struct hecmwST_local_mesh *global_mesh,
                           struct hecmwST_local_mesh *local_mesh,
                           int *elem_global2local, const char *elem_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( elem_global2local );
    HECMW_assert( elem_flag );
    HECMW_assert( global_mesh->n_elem > 0 );

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], INTERNAL ) || EVAL_BIT( elem_flag[i], BOUNDARY ) ) {
            elem_global2local[i] = ++counter;
        }
    }
    local_mesh->n_elem = counter;
    local_mesh->n_elem_gross = counter;

    HECMW_assert( local_mesh->n_elem > 0 );

    return RTC_NORMAL;
}


static int
const_ne_internal( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh, const char *elem_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh->n_elem > 0 );

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], INTERNAL ) )  counter++;
    }
    local_mesh->ne_internal = counter;

    return RTC_NORMAL;
}


static int
const_elem_internal_list( const struct hecmwST_local_mesh *global_mesh,
                          struct hecmwST_local_mesh *local_mesh,
                          int *elem_global2local, const char *elem_flag )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( elem_global2local );
    HECMW_assert( elem_flag );
    HECMW_assert( global_mesh->n_elem > 0 );

    if( local_mesh->ne_internal == 0 ) {
        local_mesh->elem_internal_list = NULL;
        return RTC_NORMAL;
    }

    local_mesh->elem_internal_list = (int *)HECMW_malloc( sizeof(int)*local_mesh->ne_internal );
    if( local_mesh->elem_internal_list == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( EVAL_BIT( elem_flag[i], INTERNAL ) ) {
            local_mesh->elem_internal_list[counter++] = elem_global2local[i];
        }
    }

    HECMW_assert( counter == local_mesh->ne_internal );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
set_elem_global2local( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       int *elem_global2local, const char *elem_flag )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( elem_global2local );
    HECMW_assert( elem_flag );

    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:  /* for node-based partitioning */

        rtc = const_ne_internal( global_mesh, local_mesh, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = set_elem_global2local_all( global_mesh, local_mesh, elem_global2local, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = const_elem_internal_list( global_mesh, local_mesh, elem_global2local, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:  /* for element-based partitioning */

        rtc = set_elem_global2local_internal( global_mesh, local_mesh,
                                              elem_global2local, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;


        rtc = set_elem_global2local_external( global_mesh, local_mesh,
                                              elem_global2local, elem_flag );
        if( rtc != RTC_NORMAL )  goto error;


        local_mesh->elem_internal_list = NULL;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "%d", global_mesh->hecmw_flag_parttype );
        goto error;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*------------------------------------------------------------------------------------------------*/

static int
set_elem_local2global( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh,
                       const int *elem_global2local, int *elem_local2global )
{
    int counter;
    int i;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( elem_global2local );
    HECMW_assert( elem_local2global );
    HECMW_assert( global_mesh->n_elem > 0 );

    for( counter=0, i=0; i<global_mesh->n_elem; i++ ) {
        if( elem_global2local[i] ) {
            elem_local2global[elem_global2local[i]-1] = i+1;
            counter++;
        }
    }
    HECMW_assert( counter == local_mesh->n_elem );

    return RTC_NORMAL;
}

/*================================================================================================*/

static int
const_gridfile( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    strcpy( local_mesh->gridfile, global_mesh->gridfile );

    return RTC_NORMAL;
}


static int
const_hecmw_n_file( const struct hecmwST_local_mesh *global_mesh,
                    struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->hecmw_n_file = global_mesh->hecmw_n_file;

    return RTC_NORMAL;
}


static int
const_files( const struct hecmwST_local_mesh *global_mesh,
             struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->files = global_mesh->files;

    return RTC_NORMAL;
}


static int
const_header( const struct hecmwST_local_mesh *global_mesh,
              struct hecmwST_local_mesh *local_mesh )
{
    strcpy( local_mesh->header, global_mesh->header );

    return RTC_NORMAL;
}


static int
const_hecmw_flag_adapt( const struct hecmwST_local_mesh *global_mesh,
                        struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->hecmw_flag_adapt = global_mesh->hecmw_flag_adapt;

    return RTC_NORMAL;
}


static int
const_hecmw_flag_initcon( const struct hecmwST_local_mesh *global_mesh,
                          struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->hecmw_flag_initcon = global_mesh->hecmw_flag_initcon;

    return RTC_NORMAL;
}


static int
const_hecmw_flag_parttype( const struct hecmwST_local_mesh *global_mesh,
                           struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->hecmw_flag_parttype = global_mesh->hecmw_flag_parttype;

    return RTC_NORMAL;
}


static int
const_hecmw_flag_partdepth( const struct hecmwST_local_mesh *global_mesh,
                            struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->hecmw_flag_partdepth = global_mesh->hecmw_flag_partdepth;

    return RTC_NORMAL;
}


static int
const_hecmw_flag_version( const struct hecmwST_local_mesh *global_mesh,
                          struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->hecmw_flag_version  = global_mesh->hecmw_flag_version;

    return RTC_NORMAL;
}


static int
const_zero_temp( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->zero_temp = global_mesh->zero_temp;

    return RTC_NORMAL;
}


static int
const_global_info( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );

    rtc = const_gridfile( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_hecmw_n_file( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_files( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_header( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_hecmw_flag_adapt( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_hecmw_flag_initcon( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_hecmw_flag_parttype( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_hecmw_flag_partdepth( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_hecmw_flag_version( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_zero_temp( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_n_dof( const struct hecmwST_local_mesh *global_mesh,
             struct hecmwST_local_mesh *local_mesh )
{
    HECMW_assert( global_mesh->n_dof > 0 );

    local_mesh->n_dof = global_mesh->n_dof;

    HECMW_assert( local_mesh->n_dof > 0 );

    return RTC_NORMAL;
}


static int
const_n_dof_grp( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh )
{
    HECMW_assert( global_mesh->n_dof_grp );

    local_mesh->n_dof_grp = global_mesh->n_dof_grp;

    HECMW_assert( global_mesh->n_dof_grp );

    return RTC_NORMAL;
}


static int
const_node_dof_index( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh, const char *node_flag )
{
    int counter;
    int i, j;

    HECMW_assert( local_mesh->n_dof_grp > 0 );
    HECMW_assert( global_mesh->node_dof_index );

    local_mesh->node_dof_index = (int *)HECMW_calloc( local_mesh->n_dof_grp+1, sizeof(int) );
    if( local_mesh->node_dof_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<global_mesh->n_dof_grp; i++ ) {
        for( j=global_mesh->node_dof_index[i]; j<global_mesh->node_dof_index[i+1]; j++ ) {
            if( EVAL_BIT( node_flag[j], INTERNAL ) )  counter++;
        }
        local_mesh->node_dof_index[i+1] = counter;
    }
    HECMW_assert( local_mesh->node_dof_index[local_mesh->n_dof_grp] == local_mesh->nn_internal );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_node_dof_item( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    HECMW_assert( global_mesh->node_dof_item );

    local_mesh->node_dof_item = global_mesh->node_dof_item;

    return 0;
}


static int
const_node( const struct hecmwST_local_mesh *global_mesh,
            struct hecmwST_local_mesh *local_mesh, const int *node_local2global )
{
    int i;

    HECMW_assert( local_mesh->n_node > 0 );
    HECMW_assert( global_mesh->node );

    local_mesh->node = (double *)HECMW_malloc( sizeof(double)*local_mesh->n_node*3 );
    if( local_mesh->node == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_node; i++ ) {
        local_mesh->node[3*i]   = global_mesh->node[3*(node_local2global[i]-1)];
        local_mesh->node[3*i+1] = global_mesh->node[3*(node_local2global[i]-1)+1];
        local_mesh->node[3*i+2] = global_mesh->node[3*(node_local2global[i]-1)+2];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_node_id( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh, const int *node_local2global )
{
    int i;

    HECMW_assert( local_mesh->n_node > 0 );
    HECMW_assert( global_mesh->node_ID );

    local_mesh->node_ID = (int *)HECMW_malloc( sizeof(int)*local_mesh->n_node*2 );
    if( local_mesh->node_ID == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_node; i++ ) {
        local_mesh->node_ID[2*i]   = global_mesh->node_ID[2*(node_local2global[i]-1)];
        local_mesh->node_ID[2*i+1] = global_mesh->node_ID[2*(node_local2global[i]-1)+1];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_global_node_id( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh, const int *node_local2global )
{
    int i;

    HECMW_assert( local_mesh->n_node > 0 );
    HECMW_assert( global_mesh->global_node_ID );

    local_mesh->global_node_ID = (int *)HECMW_malloc( sizeof(int)*local_mesh->n_node );
    if( local_mesh->global_node_ID == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_node; i++ ) {
        local_mesh->global_node_ID[i] = global_mesh->global_node_ID[node_local2global[i]-1];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_node_init_val_index( const struct hecmwST_local_mesh *global_mesh,
                           struct hecmwST_local_mesh *local_mesh, const int *node_local2global )
{
    int old_idx;
    int i;

    HECMW_assert( local_mesh->hecmw_flag_initcon );
    HECMW_assert( local_mesh->n_node > 0 );
    HECMW_assert( global_mesh->node_init_val_index );

    local_mesh->node_init_val_index = (int *)HECMW_calloc( local_mesh->n_node+1, sizeof(int) );
    if( local_mesh->node_init_val_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_node; i++ ) {
        old_idx = node_local2global[i] - 1;

        local_mesh->node_init_val_index[i+1] = local_mesh->node_init_val_index[i] +
                                                 global_mesh->node_init_val_index[old_idx+1] -
                                                 global_mesh->node_init_val_index[old_idx];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_node_init_val_item( const struct hecmwST_local_mesh *global_mesh,
                          struct hecmwST_local_mesh *local_mesh, const int *node_local2global )
{
    int size;
    int counter;
    int i, j, gstart, gend, lstart, lend;

    HECMW_assert( local_mesh->hecmw_flag_initcon );
    HECMW_assert( local_mesh->n_node > 0 );
    HECMW_assert( local_mesh->node_init_val_index );
    HECMW_assert( global_mesh->node_init_val_item );

    if( local_mesh->node_init_val_index[local_mesh->n_node] == 0 ) {
        local_mesh->node_init_val_item = NULL;
        return 0;
    }

    size = sizeof(double) * local_mesh->node_init_val_index[local_mesh->n_node];
    local_mesh->node_init_val_item = (double *)HECMW_malloc( size );
    if( local_mesh->node_init_val_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<local_mesh->n_node; i++ ) {
        gstart = global_mesh->node_init_val_index[node_local2global[i]-1];
        gend   = global_mesh->node_init_val_index[node_local2global[i]];
        lstart = local_mesh->node_init_val_index[i];
        lend   = local_mesh->node_init_val_index[i+1];

        HECMW_assert( gend - gstart == lend - lstart );

        for( j=0; j<lend-lstart; j++ ) {
            local_mesh->node_init_val_item[lstart+j] = global_mesh->node_init_val_item[gstart+j];
            counter++;
        }
        HECMW_assert( counter == local_mesh->node_init_val_index[i+1] );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_node_info( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh,
                 const int *node_local2global, const char *node_flag )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_local2global );
    HECMW_assert( node_flag );

    rtc = const_n_dof( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_n_dof_grp( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_dof_index( global_mesh, local_mesh, node_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_dof_item( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node( global_mesh, local_mesh, node_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_id( global_mesh, local_mesh, node_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_global_node_id( global_mesh, local_mesh, node_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    if( local_mesh->hecmw_flag_initcon ) {
        rtc = const_node_init_val_index( global_mesh, local_mesh, node_local2global );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = const_node_init_val_item( global_mesh, local_mesh, node_local2global );
        if( rtc != RTC_NORMAL )  goto error;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_n_elem_type( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh )
{
    HECMW_assert( global_mesh->n_elem_type > 0 );

    local_mesh->n_elem_type = global_mesh->n_elem_type;

    HECMW_assert( local_mesh->n_elem_type > 0 );

    return RTC_NORMAL;
}


static int
const_elem_type( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh, const int *elem_local2global )
{
    int i;

    HECMW_assert( local_mesh->n_elem > 0 );
    HECMW_assert( global_mesh->elem_type );

    local_mesh->elem_type = (int *)HECMW_malloc( sizeof(int)*local_mesh->n_elem );
    if( local_mesh->elem_type == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_elem; i++ ) {
        local_mesh->elem_type[i] = global_mesh->elem_type[elem_local2global[i]-1];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_type_index( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh, const int *elem_global2local )
{
    int counter;
    int i, j;

    HECMW_assert( local_mesh->n_elem_type > 0 );
    HECMW_assert( global_mesh->n_elem_type > 0 );
    HECMW_assert( global_mesh->elem_type_index );

    local_mesh->elem_type_index = (int *)HECMW_calloc( local_mesh->n_elem_type+1, sizeof(int) );
    if( local_mesh->elem_type_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<global_mesh->n_elem_type; i++ ) {
        for( j=global_mesh->elem_type_index[i]; j<global_mesh->elem_type_index[i+1]; j++ ) {
            if( elem_global2local[j] )  counter++;
        }
        local_mesh->elem_type_index[i+1] = counter;
    }
    HECMW_assert( local_mesh->elem_type_index[local_mesh->n_elem_type] == local_mesh->n_elem );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_type_item( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh )
{
    HECMW_assert( global_mesh->elem_type_item );

    local_mesh->elem_type_item = global_mesh->elem_type_item;

    return RTC_NORMAL;
}


static int
const_elem_node_index( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh, const int *elem_local2global )
{
    int old_idx;
    int i;

    HECMW_assert( local_mesh->n_elem > 0 );
    HECMW_assert( global_mesh->elem_node_index );

    local_mesh->elem_node_index = (int *)HECMW_calloc( local_mesh->n_elem+1, sizeof(int) );
    if( local_mesh->elem_node_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_elem; i++ ) {
        old_idx = elem_local2global[i] - 1;

        local_mesh->elem_node_index[i+1] = local_mesh->elem_node_index[i] +
                                             global_mesh->elem_node_index[old_idx+1] -
                                             global_mesh->elem_node_index[old_idx];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_node_item( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh,
                      const int *node_global2local, const int *elem_local2global )
{
    int node;
    int size;
    int counter;
    int i, j, gstart, gend, lstart, lend;

    HECMW_assert( local_mesh->n_elem > 0 );
    HECMW_assert( local_mesh->elem_node_index );
    HECMW_assert( local_mesh->elem_node_index[local_mesh->n_elem] > 0 );
    HECMW_assert( global_mesh->elem_node_item );

    size = sizeof(int) * local_mesh->elem_node_index[local_mesh->n_elem];
    local_mesh->elem_node_item = (int *)HECMW_malloc( size );
    if( local_mesh->elem_node_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<local_mesh->n_elem; i++ ) {
        gstart = global_mesh->elem_node_index[elem_local2global[i]-1];
        gend   = global_mesh->elem_node_index[elem_local2global[i]];
        lstart = local_mesh->elem_node_index[i];
        lend   = local_mesh->elem_node_index[i+1];

        for( j=0; j<lend-lstart; j++ ) {
            node = global_mesh->elem_node_item[gstart+j];
            local_mesh->elem_node_item[lstart+j] = node_global2local[node-1];
            counter++;
        }
        HECMW_assert( counter == local_mesh->elem_node_index[i+1] );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_id( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh, const int *elem_local2global )
{
    int i;

    HECMW_assert( local_mesh->n_elem > 0 );
    HECMW_assert( global_mesh->elem_ID );

    local_mesh->elem_ID = (int *)HECMW_malloc( sizeof(int)*local_mesh->n_elem*2 );
    if( local_mesh->elem_ID == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_elem; i++ ) {
        local_mesh->elem_ID[2*i]   = global_mesh->elem_ID[2*(elem_local2global[i]-1)];
        local_mesh->elem_ID[2*i+1] = global_mesh->elem_ID[2*(elem_local2global[i]-1)+1];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_global_elem_id( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh, const int *elem_local2global )
{
    int i;

    HECMW_assert( local_mesh->n_elem );
    HECMW_assert( global_mesh->global_elem_ID );

    local_mesh->global_elem_ID = (int *)HECMW_malloc( sizeof(int)*local_mesh->n_elem );
    if( local_mesh->global_elem_ID == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_elem; i++ ) {
        local_mesh->global_elem_ID[i] = global_mesh->global_elem_ID[elem_local2global[i]-1];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_section_id( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh, const int *elem_local2global )
{
    int i;

    HECMW_assert( local_mesh->n_elem );
    HECMW_assert( global_mesh->section_ID );

    local_mesh->section_ID = (int *)HECMW_malloc( sizeof(int)*local_mesh->n_elem );
    if( local_mesh->section_ID == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_elem; i++ ) {
        local_mesh->section_ID[i] = global_mesh->section_ID[elem_local2global[i]-1];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_mat_id_index( const struct hecmwST_local_mesh *global_mesh,
                         struct hecmwST_local_mesh *local_mesh, const int *elem_local2global )
{
    int old_idx;
    int i;

    HECMW_assert( local_mesh->n_elem > 0 );
    HECMW_assert( global_mesh->elem_mat_ID_index );

    local_mesh->elem_mat_ID_index = (int *)HECMW_calloc( local_mesh->n_elem+1, sizeof(int) );
    if( local_mesh->elem_mat_ID_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<local_mesh->n_elem; i++ ) {
        old_idx = elem_local2global[i] - 1;

        local_mesh->elem_mat_ID_index[i+1] = local_mesh->elem_mat_ID_index[i] +
                                               global_mesh->elem_mat_ID_index[old_idx+1] -
                                               global_mesh->elem_mat_ID_index[old_idx];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_n_elem_mat_id( struct hecmwST_local_mesh *local_mesh )
{
    HECMW_assert( local_mesh->n_elem > 0 );
    HECMW_assert( local_mesh->elem_mat_ID_index );

    local_mesh->n_elem_mat_ID = local_mesh->elem_mat_ID_index[local_mesh->n_elem];

    return RTC_NORMAL;
}


static int
const_elem_mat_id_item( const struct hecmwST_local_mesh *global_mesh,
                        struct hecmwST_local_mesh *local_mesh, const int *elem_local2global )
{
    int size;
    int counter;
    int i, j, gstart, gend, lstart, lend;

    HECMW_assert( local_mesh->n_elem > 0 );
    HECMW_assert( local_mesh->elem_mat_ID_index[local_mesh->n_elem] >= 0 );

    if( local_mesh->elem_mat_ID_index[local_mesh->n_elem] == 0 ) {
        local_mesh->elem_mat_ID_item = NULL;
        return RTC_NORMAL;
    }

    size = sizeof(int)*local_mesh->elem_mat_ID_index[local_mesh->n_elem];
    local_mesh->elem_mat_ID_item = (int *)HECMW_malloc( size );
    if( local_mesh->elem_mat_ID_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<local_mesh->n_elem; i++ ) {
        gstart = global_mesh->elem_mat_ID_index[elem_local2global[i]-1];
        gend   = global_mesh->elem_mat_ID_index[elem_local2global[i]];
        lstart = local_mesh->elem_mat_ID_index[i];
        lend   = local_mesh->elem_mat_ID_index[i+1];

        HECMW_assert( lend - lstart == gend - gstart );

        for( j=0; j<lend-lstart; j++ ) {
            local_mesh->elem_mat_ID_item[lstart+j] = global_mesh->elem_mat_ID_item[gstart+j];
            counter++;
        }
        HECMW_assert( counter == local_mesh->elem_mat_ID_index[i+1] );
    }
    HECMW_assert( counter == local_mesh->n_elem_mat_ID );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_info( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh, const int *node_global2local,
                 const int *elem_global2local, const int *elem_local2global )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( elem_global2local );
    HECMW_assert( elem_local2global );

    rtc = const_n_elem_type( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_type( global_mesh, local_mesh, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_type_index( global_mesh, local_mesh, elem_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_type_item( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_node_index( global_mesh, local_mesh, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_node_item( global_mesh, local_mesh, node_global2local, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_id( global_mesh, local_mesh, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_global_elem_id( global_mesh, local_mesh, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_section_id( global_mesh, local_mesh, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_mat_id_index( global_mesh, local_mesh, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_n_elem_mat_id( local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_mat_id_item( global_mesh, local_mesh, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_hecmw_comm( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->HECMW_COMM = global_mesh->HECMW_COMM;

    return RTC_NORMAL;
}


static int
const_zero( struct hecmwST_local_mesh *local_mesh, int current_domain )
{
    local_mesh->zero = ( current_domain == 0 ) ? 1 : 0;

    return RTC_NORMAL;
}


static int
const_petot( const struct hecmwST_local_mesh *global_mesh,
             struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->PETOT = global_mesh->n_subdomain;

    return RTC_NORMAL;
}


static int
const_pesmptot( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->PEsmpTOT = global_mesh->PEsmpTOT;

    return RTC_NORMAL;
}


static int
const_my_rank( struct hecmwST_local_mesh *local_mesh, int current_domain )
{
    local_mesh->my_rank = current_domain;

    return RTC_NORMAL;
}


static int
const_errnof( const struct hecmwST_local_mesh *global_mesh,
              struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->errnof = global_mesh->errnof;

    return RTC_NORMAL;
}


static int
const_n_subdomain( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->n_subdomain = global_mesh->n_subdomain;

    return RTC_NORMAL;
}


static int
const_import_item( struct hecmwST_local_mesh *local_mesh, const int *global2local )
{
    int new_id;
    int i;

    if( local_mesh->n_neighbor_pe == 0 ) {
        local_mesh->import_item = NULL;
        return RTC_NORMAL;
    }

    HECMW_assert( local_mesh->n_neighbor_pe > 0 );
    HECMW_assert( local_mesh->import_index );
    HECMW_assert( local_mesh->import_index[local_mesh->n_neighbor_pe] > 0 );
    HECMW_assert( local_mesh->import_item );

    for( i=0; i<local_mesh->import_index[local_mesh->n_neighbor_pe]; i++ ) {
        new_id = global2local[local_mesh->import_item[i]-1];
        local_mesh->import_item[i] = new_id;
    }

    return RTC_NORMAL;
}


static int
const_export_item( struct hecmwST_local_mesh *local_mesh, const int *global2local )
{
    int new_id;
    int i;

    if( local_mesh->n_neighbor_pe == 0 ) {
        local_mesh->export_item = NULL;
        return RTC_NORMAL;
    }

    HECMW_assert( local_mesh->n_neighbor_pe > 0 );
    HECMW_assert( local_mesh->export_index );
    HECMW_assert( local_mesh->export_index[local_mesh->n_neighbor_pe] > 0 );
    HECMW_assert( local_mesh->export_item );

    for( i=0; i<local_mesh->export_index[local_mesh->n_neighbor_pe]; i++ ) {
        new_id = global2local[local_mesh->export_item[i]-1];
        local_mesh->export_item[i] = new_id;
    }

    return RTC_NORMAL;
}


static int
const_shared_item( struct hecmwST_local_mesh *local_mesh, const int *global2local )
{
    int new_id;
    int i;

    if( local_mesh->n_neighbor_pe == 0 ) {
        local_mesh->shared_item = NULL;
        return RTC_NORMAL;
    }

    HECMW_assert( local_mesh->n_neighbor_pe > 0 );
    HECMW_assert( local_mesh->shared_index );
    HECMW_assert( local_mesh->shared_index[local_mesh->n_neighbor_pe] > 0 );
    HECMW_assert( local_mesh->shared_item );

    for( i=0; i<local_mesh->shared_index[local_mesh->n_neighbor_pe]; i++ ) {
        new_id = global2local[local_mesh->shared_item[i]-1];
        local_mesh->shared_item[i] = new_id;
    }

    return RTC_NORMAL;
}


static int
const_comm_info( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh,
                 const int *node_global2local, const int *elem_global2local, int current_domain )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( node_global2local );
    HECMW_assert( elem_global2local );

    rtc = const_hecmw_comm( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_zero( local_mesh, current_domain );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_petot( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_pesmptot( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_my_rank( local_mesh, current_domain );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_errnof( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_n_subdomain( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    switch( global_mesh->hecmw_flag_parttype ) {

    case HECMW_FLAG_PARTTYPE_NODEBASED:
        rtc = const_import_item( local_mesh, node_global2local );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = const_export_item( local_mesh, node_global2local );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = const_shared_item( local_mesh, elem_global2local );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:
        rtc = const_import_item( local_mesh, elem_global2local );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = const_export_item( local_mesh, elem_global2local );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = const_shared_item( local_mesh, node_global2local );
        if( rtc != RTC_NORMAL )  goto error;

        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "%d", global_mesh->hecmw_flag_parttype );
        goto error;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_n_adapt( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->n_adapt = global_mesh->n_adapt;

    return RTC_NORMAL;
}


static int
const_coarse_grid_level( const struct hecmwST_local_mesh *global_mesh,
                         struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->coarse_grid_level = global_mesh->coarse_grid_level;

    return RTC_NORMAL;
}


static int
const_when_i_was_refined_node( const struct hecmwST_local_mesh *global_mesh,
                               struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->when_i_was_refined_node = global_mesh->when_i_was_refined_node;

    return RTC_NORMAL;
}


static int
const_when_i_was_refined_elem( const struct hecmwST_local_mesh *global_mesh,
                               struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->when_i_was_refined_elem = global_mesh->when_i_was_refined_elem;

    return RTC_NORMAL;
}


static int
const_adapt_parent_type( const struct hecmwST_local_mesh *global_mesh,
                         struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->adapt_parent_type = global_mesh->adapt_parent_type;

    return RTC_NORMAL;
}


static int
const_adapt_type( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->adapt_type = global_mesh->adapt_type;

    return RTC_NORMAL;
}


static int
const_adapt_level( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->adapt_level = global_mesh->adapt_level;

    return RTC_NORMAL;
}


static int
const_adapt_parent( const struct hecmwST_local_mesh *global_mesh,
                    struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->adapt_parent = global_mesh->adapt_parent;

    return RTC_NORMAL;
}


static int
const_adapt_children_index( const struct hecmwST_local_mesh *global_mesh,
                            struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->adapt_children_index = global_mesh->adapt_children_index;

    return RTC_NORMAL;
}


static int
const_adapt_children_item( const struct hecmwST_local_mesh *global_mesh,
                           struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->adapt_children_item = global_mesh->adapt_children_item;

    return RTC_NORMAL;
}


static int
const_adapt_info( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );

    rtc = const_n_adapt( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_coarse_grid_level( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_when_i_was_refined_node( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_when_i_was_refined_elem( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_adapt_parent_type( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_adapt_type( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_adapt_level( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_adapt_parent( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_adapt_children_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_adapt_children_item( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_n_sect( const struct hecmwST_local_mesh *global_mesh,
              struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->n_sect = global_mesh->section->n_sect;

    return RTC_NORMAL;
}


static int
const_sect_type( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_type = global_mesh->section->sect_type;

    return RTC_NORMAL;
}


static int
const_sect_opt( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_opt = global_mesh->section->sect_opt;

    return RTC_NORMAL;
}


static int
const_sect_mat_id_index( const struct hecmwST_local_mesh *global_mesh,
                         struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_mat_ID_index = global_mesh->section->sect_mat_ID_index;

    return RTC_NORMAL;
}


static int
const_sect_mat_id_item( const struct hecmwST_local_mesh *global_mesh,
                        struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_mat_ID_item = global_mesh->section->sect_mat_ID_item;

    return RTC_NORMAL;
}


static int
const_sect_i_index( const struct hecmwST_local_mesh *global_mesh,
                    struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_I_index = global_mesh->section->sect_I_index;

    return RTC_NORMAL;
}


static int
const_sect_i_item( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_I_item = global_mesh->section->sect_I_item;

    return RTC_NORMAL;
}


static int
const_sect_r_index( const struct hecmwST_local_mesh *global_mesh,
                    struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_R_index = global_mesh->section->sect_R_index;

    return RTC_NORMAL;
}


static int
const_sect_r_item( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->section->sect_R_item = global_mesh->section->sect_R_item;

    return RTC_NORMAL;
}


static int
const_sect_info( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( local_mesh );
    HECMW_assert( global_mesh->section );
    HECMW_assert( local_mesh->section );

    rtc = const_n_sect( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_type( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_opt( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_mat_id_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_mat_id_item( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_i_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_i_item( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_r_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_r_item( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_n_mat( const struct hecmwST_local_mesh *global_mesh,
             struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->n_mat = global_mesh->material->n_mat;

    return RTC_NORMAL;
}


static int
const_n_mat_item( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->n_mat_item = global_mesh->material->n_mat_item;

    return RTC_NORMAL;
}


static int
const_n_mat_subitem( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->n_mat_subitem = global_mesh->material->n_mat_subitem;

    return RTC_NORMAL;
}


static int
const_n_mat_table( const struct hecmwST_local_mesh *global_mesh,
                   struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->n_mat_table = global_mesh->material->n_mat_table;

    return RTC_NORMAL;
}


static int
const_mat_name( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->mat_name = global_mesh->material->mat_name;

    return RTC_NORMAL;
}


static int
const_mat_item_index( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->mat_item_index = global_mesh->material->mat_item_index;

    return RTC_NORMAL;
}


static int
const_mat_subitem_index( const struct hecmwST_local_mesh *global_mesh,
                         struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->mat_subitem_index = global_mesh->material->mat_subitem_index;

    return RTC_NORMAL;
}


static int
const_mat_table_index( const struct hecmwST_local_mesh *global_mesh,
                       struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->mat_table_index = global_mesh->material->mat_table_index;

    return RTC_NORMAL;
}


static int
const_mat_val( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->mat_val = global_mesh->material->mat_val;

    return RTC_NORMAL;
}


static int
const_mat_temp( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->material->mat_temp = global_mesh->material->mat_temp;

    return RTC_NORMAL;
}


static int
const_mat_info( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->material );
    HECMW_assert( local_mesh );
    HECMW_assert( local_mesh->material );

    rtc = const_n_mat( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_n_mat_item( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_n_mat_subitem( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_n_mat_table( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mat_name( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mat_item_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mat_subitem_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mat_table_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mat_val( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mat_temp( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_n_mpc( const struct hecmwST_local_mesh *global_mesh,
             struct hecmwST_local_mesh *local_mesh,
             const int *node_global2local, char *mpc_flag )
{
    struct hecmwST_mpc *mpc_global = global_mesh->mpc;
    struct hecmwST_mpc *mpc_local = local_mesh->mpc;
    int node, diff, evalsum, counter;
    int i, j;

    for( counter=0, i=0; i<mpc_global->n_mpc; i++ ) {
        diff = mpc_global->mpc_index[i+1]-mpc_global->mpc_index[i];

        for( evalsum=0, j=mpc_global->mpc_index[i]; j<mpc_global->mpc_index[i+1]; j++ ) {
            node = mpc_global->mpc_item[j];
            if( node_global2local[node-1] >  0  &&
                node_global2local[node-1] <= local_mesh->nn_internal )  evalsum++;
        }

        if( evalsum ) {
            /* commented out by K.Goto; begin */
            /* HECMW_assert( diff == evalsum ); */
            /* commented out by K.Goto; end */
            MASK_BIT( mpc_flag[i], MASK );
            counter++;
        }
    }
    mpc_local->n_mpc = counter;

    return RTC_NORMAL;

/* error:                2007/12/27 S.Ito */
/*     return RTC_ERROR; 2007/12/27 S.Ito */
}


static int
const_mpc_index( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh, const char *mpc_flag )
{
    struct hecmwST_mpc *mpc_global = global_mesh->mpc;
    struct hecmwST_mpc *mpc_local = local_mesh->mpc;
    int counter;
    int i;

    mpc_local->mpc_index = (int *)HECMW_calloc( mpc_local->n_mpc+1, sizeof(int) );
    if( local_mesh->mpc->mpc_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<mpc_global->n_mpc; i++ ) {
        if( EVAL_BIT( mpc_flag[i], MASK ) ) {
            mpc_local->mpc_index[counter+1] = mpc_local->mpc_index[counter] +
                                              mpc_global->mpc_index[i+1] - mpc_global->mpc_index[i];
            counter++;
        }
    }
    HECMW_assert( counter == mpc_local->n_mpc );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_mpc_item( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh,
                const int *node_global2local, const char *mpc_flag )
{
    struct hecmwST_mpc *mpc_global = global_mesh->mpc;
    struct hecmwST_mpc *mpc_local = local_mesh->mpc;
    int mcounter, icounter;
    int i, j;

    mpc_local->mpc_item = (int *)HECMW_malloc( sizeof(int)*mpc_local->mpc_index[mpc_local->n_mpc] );
    if( mpc_local->mpc_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( mcounter=0, icounter=0, i=0; i<mpc_global->n_mpc; i++ ) {
        if( EVAL_BIT( mpc_flag[i], MASK ) ) {
            for( j=mpc_global->mpc_index[i]; j<mpc_global->mpc_index[i+1]; j++ ) {
                mpc_local->mpc_item[mcounter++] = node_global2local[mpc_global->mpc_item[j]-1];
            }
            HECMW_assert( mcounter == mpc_local->mpc_index[++icounter] );
        }
    }
    HECMW_assert( icounter == mpc_local->n_mpc );
    HECMW_assert( mcounter == mpc_local->mpc_index[mpc_local->n_mpc] );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_mpc_dof( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh, const char *mpc_flag )
{
    struct hecmwST_mpc *mpc_global = global_mesh->mpc;
    struct hecmwST_mpc *mpc_local = local_mesh->mpc;
    int mcounter, icounter;
    int i, j;

    mpc_local->mpc_dof = (int *)HECMW_malloc( sizeof(int)*mpc_local->mpc_index[mpc_local->n_mpc] );
    if( local_mesh->mpc->mpc_dof == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( mcounter=0, icounter=0, i=0; i<mpc_global->n_mpc; i++ ) {
        if( EVAL_BIT( mpc_flag[i], MASK ) ) {
            for( j=mpc_global->mpc_index[i]; j<mpc_global->mpc_index[i+1]; j++ ) {
                mpc_local->mpc_dof[mcounter++] = mpc_global->mpc_dof[j];
            }
            HECMW_assert( mcounter == mpc_local->mpc_index[++icounter] );
        }
    }
    HECMW_assert( icounter == mpc_local->n_mpc );
    HECMW_assert( mcounter == mpc_local->mpc_index[mpc_local->n_mpc] );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_mpc_val( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh, const char *mpc_flag )
{
    struct hecmwST_mpc *mpc_global = global_mesh->mpc;
    struct hecmwST_mpc *mpc_local = local_mesh->mpc;
    int size;
    int mcounter, icounter;
    int i, j;

    size = sizeof(double) * mpc_local->mpc_index[mpc_local->n_mpc];
    mpc_local->mpc_val = (double *)HECMW_malloc( size );
    if( local_mesh->mpc->mpc_val == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( mcounter=0, icounter=0, i=0; i<mpc_global->n_mpc; i++ ) {
        if( EVAL_BIT( mpc_flag[i], MASK ) ) {
            for( j=mpc_global->mpc_index[i]; j<mpc_global->mpc_index[i+1]; j++ ) {
                mpc_local->mpc_val[mcounter++] = mpc_global->mpc_val[j];
            }
            HECMW_assert( mcounter == mpc_local->mpc_index[++icounter] );
        }
    }
    HECMW_assert( icounter == local_mesh->mpc->n_mpc );
    HECMW_assert( mcounter == local_mesh->mpc->mpc_index[local_mesh->mpc->n_mpc] );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_mpc_const( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh, const char *mpc_flag )
{
    struct hecmwST_mpc *mpc_global = global_mesh->mpc;
    struct hecmwST_mpc *mpc_local = local_mesh->mpc;
    int size;
    int icounter;
    int i;

    size = sizeof(double) * mpc_local->n_mpc;
    mpc_local->mpc_const = (double *)HECMW_malloc( size );
    if( local_mesh->mpc->mpc_const == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( icounter=0, i=0; i<mpc_global->n_mpc; i++ ) {
        if( EVAL_BIT( mpc_flag[i], MASK ) ) {
            mpc_local->mpc_const[icounter] = mpc_global->mpc_const[i];
            icounter++;
        }
    }
    HECMW_assert( icounter == local_mesh->mpc->n_mpc );

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_mpc_info( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh, const int *node_global2local )
{
    char *mpc_flag = NULL;
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->mpc );
    HECMW_assert( local_mesh );
    HECMW_assert( local_mesh->mpc );
    HECMW_assert( node_global2local );

    if( global_mesh->mpc->n_mpc == 0 ) {
        init_struct_mpc( local_mesh );
        return RTC_NORMAL;
    }

    mpc_flag = (char *)HECMW_calloc( global_mesh->mpc->n_mpc, sizeof(char) );
    if( mpc_flag == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = const_n_mpc( global_mesh, local_mesh, node_global2local, mpc_flag );
    if( rtc != RTC_NORMAL )  goto error;

    if( local_mesh->mpc->n_mpc == 0 ) {
        init_struct_mpc( local_mesh );
        HECMW_free( mpc_flag );
        return RTC_NORMAL;
    }

    rtc = const_mpc_index( global_mesh, local_mesh, mpc_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mpc_item( global_mesh, local_mesh, node_global2local, mpc_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mpc_dof( global_mesh, local_mesh, mpc_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mpc_val( global_mesh, local_mesh, mpc_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mpc_const( global_mesh, local_mesh, mpc_flag );
    if( rtc != RTC_NORMAL )  goto error;

    HECMW_free( mpc_flag );

    return RTC_NORMAL;

error:
    HECMW_free( mpc_flag );

    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_n_amp( const struct hecmwST_local_mesh *global_mesh,
             struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->n_amp = global_mesh->amp->n_amp;

    return RTC_NORMAL;
}


static int
const_amp_name( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->amp_name = global_mesh->amp->amp_name;

    return RTC_NORMAL;
}


static int
const_amp_type_definition( const struct hecmwST_local_mesh *global_mesh,
                           struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->amp_type_definition = global_mesh->amp->amp_type_definition;

    return RTC_NORMAL;
}


static int
const_amp_type_time( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->amp_type_time = global_mesh->amp->amp_type_time;

    return RTC_NORMAL;
}


static int
const_amp_type_value( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->amp_type_value = global_mesh->amp->amp_type_value;

    return RTC_NORMAL;
}


static int
const_amp_index( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->amp_index = global_mesh->amp->amp_index;

    return RTC_NORMAL;
}


static int
const_amp_val( const struct hecmwST_local_mesh *global_mesh,
               struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->amp_val = global_mesh->amp->amp_val;

    return RTC_NORMAL;
}


static int
const_amp_table( const struct hecmwST_local_mesh *global_mesh,
                 struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->amp->amp_table = global_mesh->amp->amp_table;

    return RTC_NORMAL;
}


static int
const_amp_info( const struct hecmwST_local_mesh *global_mesh,
                struct hecmwST_local_mesh *local_mesh )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->amp );
    HECMW_assert( local_mesh );
    HECMW_assert( local_mesh->amp );

    if( global_mesh->amp->n_amp == 0 ) {
        init_struct_amp( local_mesh );
        return RTC_NORMAL;
    }

    rtc = const_n_amp( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_name( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_type_definition( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_type_time( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_type_value( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_index( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_val( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_table( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int *
const_node_grp_mask_eqn( const struct hecmwST_local_mesh *global_mesh,
                         struct hecmwST_local_mesh *local_mesh,
                         const int *node_global2local, int eqn_block_idx )
{
    struct hecmwST_node_grp *node_group_global = global_mesh->node_group;
    int *n_eqn_item = NULL;
    int diff, evalsum;
    int i, j, is, ie, js;

    is = node_group_global->grp_index[eqn_block_idx];
    ie = node_group_global->grp_index[eqn_block_idx+1];

    n_eqn_item = (int *)HECMW_malloc( sizeof(int)*(ie-is) );
    if( n_eqn_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( js=0, i=0; i<ie-is; i++ ) {
        diff = node_group_global->grp_item[is+i] - js;
        for( evalsum=0, j=js; j<node_group_global->grp_item[is+i]; j++ ) {
            if( node_global2local[j] >  0 &&
                node_global2local[j] <= local_mesh->nn_internal )  evalsum++;
        }

        if( evalsum ) {
            HECMW_assert( evalsum == diff );
            n_eqn_item[i] = diff;
        } else {
            n_eqn_item[i] = 0;
        }

        js = node_group_global->grp_item[is+i];
    }

    return n_eqn_item;

error:
    return NULL;
}


static int
const_node_n_grp( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->node_group->n_grp = global_mesh->node_group->n_grp;

    return RTC_NORMAL;
}


static int
const_node_grp_name( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->node_group->grp_name = global_mesh->node_group->grp_name;

    return RTC_NORMAL;
}


static int
const_node_grp_index( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh,
                      const int *node_global2local, const int *n_eqn_item, int eqn_block_idx )
{
    struct hecmwST_node_grp *node_group_global = global_mesh->node_group;
    struct hecmwST_node_grp *node_group_local = local_mesh->node_group;
    int node;
    int counter, diff;
    int i, j;

    node_group_local->grp_index = (int *)HECMW_calloc( node_group_local->n_grp+1, sizeof(int) );
    if( node_group_local->grp_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<node_group_global->n_grp; i++ ) {


        if( i != eqn_block_idx ) {
            for( j=node_group_global->grp_index[i]; j<node_group_global->grp_index[i+1]; j++ ) {
                node = node_group_global->grp_item[j];
                if( node_global2local[node-1] )  counter++;
            }


        } else {
            diff = node_group_global->grp_index[i+1] - node_group_global->grp_index[i];
            for( j=0; j<diff; j++ ) {
                if( n_eqn_item[j] > 0 )  counter++;
            }
        }

        node_group_local->grp_index[i+1] = counter;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_node_grp_item( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh,
                     const int *node_global2local, const int *n_eqn_item, int eqn_block_idx )
{
    struct hecmwST_node_grp *node_group_global = global_mesh->node_group;
    struct hecmwST_node_grp *node_group_local  = local_mesh->node_group;
    int node;
    int size;
    int counter;
    int i, j, k, js, je, ks, ls;

    size = sizeof(int) * node_group_local->grp_index[node_group_local->n_grp];
    node_group_local->grp_item = (int *)HECMW_malloc( size );
    if( node_group_local->grp_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<node_group_global->n_grp; i++ ) {


        if( i != eqn_block_idx ) {
            for( j=node_group_global->grp_index[i]; j<node_group_global->grp_index[i+1]; j++ ) {
                node = node_group_global->grp_item[j];
                if( node_global2local[node-1] ) {
                    node_group_local->grp_item[counter++] = node_global2local[node-1];
                }
            }


        } else {
            js = node_group_global->grp_index[i];
            je = node_group_global->grp_index[i+1];
            for( ks=0, ls=0, j=js; j<je; j++ ) {
                if( n_eqn_item[j-js] ) {
                    HECMW_assert( n_eqn_item[j-js] == node_group_global->grp_item[j] - ks );
                    node_group_local->grp_item[counter] = ls + n_eqn_item[j-js];

                    for( k=ks; k<node_group_global->grp_item[j]; k++ ) {
                        HECMW_assert( ls < node_global2local[k] &&
                                      node_global2local[k] <= node_group_local->grp_item[counter] );
                    }
                    ls = node_group_local->grp_item[counter];
                    counter++;
                }
                ks = node_group_global->grp_item[j];
            }
        }
        HECMW_assert( counter == node_group_local->grp_index[i+1] );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_node_grp_info( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh, const int *node_global2local )
{
    int *n_eqn_item = NULL;
    int eqn_block_idx;
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->node_group );
    HECMW_assert( local_mesh );
    HECMW_assert( local_mesh->node_group );
    HECMW_assert( node_global2local );

    if( global_mesh->node_group->n_grp == 0 ) {
        init_struct_node_grp( local_mesh );
        return RTC_NORMAL;
    }

    eqn_block_idx = search_eqn_block_idx( global_mesh );

    if( eqn_block_idx >= 0 ) {
        n_eqn_item = const_node_grp_mask_eqn( global_mesh, local_mesh,
                                              node_global2local, eqn_block_idx );
        if( n_eqn_item == NULL )  goto error;
    }

    rtc = const_node_n_grp( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_grp_name( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_grp_index( global_mesh, local_mesh,
                                node_global2local, n_eqn_item, eqn_block_idx );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_grp_item( global_mesh, local_mesh,
                               node_global2local, n_eqn_item, eqn_block_idx );
    if( rtc != RTC_NORMAL )  goto error;

    HECMW_free( n_eqn_item );

    return RTC_NORMAL;

error:
    HECMW_free( n_eqn_item );

    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_elem_n_grp( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->elem_group->n_grp = global_mesh->elem_group->n_grp;

    return RTC_NORMAL;
}


static int
const_elem_grp_name( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->elem_group->grp_name = global_mesh->elem_group->grp_name;

    return RTC_NORMAL;
}


static int
const_elem_grp_index( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh, const int *elem_global2local )
{
    struct hecmwST_elem_grp *elem_group_global = global_mesh->elem_group;
    struct hecmwST_elem_grp *elem_group_local = local_mesh->elem_group;
    int elem;
    int counter;
    int i, j;

    elem_group_local->grp_index = (int *)HECMW_calloc( elem_group_local->n_grp+1, sizeof(int) );
    if( elem_group_local->grp_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<elem_group_global->n_grp; i++ ) {
        for( j=elem_group_global->grp_index[i]; j<elem_group_global->grp_index[i+1]; j++ ) {
            elem = elem_group_global->grp_item[j];
            if( elem_global2local[elem-1] )  counter++;
        }
        elem_group_local->grp_index[i+1] = counter;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_grp_item( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh, const int *elem_global2local )
{
    struct hecmwST_elem_grp *elem_group_global = global_mesh->elem_group;
    struct hecmwST_elem_grp *elem_group_local = local_mesh->elem_group;
    int elem;
    int size;
    int counter;
    int i, j;

    size = sizeof(int) * elem_group_local->grp_index[elem_group_local->n_grp];
    elem_group_local->grp_item = (int *)HECMW_malloc( size );
    if( local_mesh->elem_group->grp_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<elem_group_global->n_grp; i++ ) {
        for( j=elem_group_global->grp_index[i]; j<elem_group_global->grp_index[i+1]; j++ ) {
            elem = elem_group_global->grp_item[j];
            if( elem_global2local[elem-1] ) {
                elem_group_local->grp_item[counter++] = elem_global2local[elem-1];
            }
        }
        HECMW_assert( counter == elem_group_local->grp_index[i+1] );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_elem_grp_info( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh, const int *elem_global2local )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->elem_group );
    HECMW_assert( local_mesh );
    HECMW_assert( local_mesh->elem_group );
    HECMW_assert( elem_global2local );

    if( global_mesh->elem_group->n_grp == 0 ) {
        init_struct_elem_grp( local_mesh );
        return RTC_NORMAL;
    }

    rtc = const_elem_n_grp( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_grp_name( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_grp_index( global_mesh, local_mesh, elem_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_grp_item( global_mesh, local_mesh, elem_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_surf_n_grp( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->surf_group->n_grp = global_mesh->surf_group->n_grp;

    return RTC_NORMAL;
}


static int
const_surf_grp_name( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->surf_group->grp_name = global_mesh->surf_group->grp_name;

    return RTC_NORMAL;
}


static int
const_surf_grp_index( const struct hecmwST_local_mesh *global_mesh,
                      struct hecmwST_local_mesh *local_mesh, const int *elem_global2local )
{
    struct hecmwST_surf_grp *surf_group_global = global_mesh->surf_group;
    struct hecmwST_surf_grp *surf_group_local = local_mesh->surf_group;
    int elem;
    int counter;
    int i, j;

    surf_group_local->grp_index = (int *)HECMW_calloc( surf_group_local->n_grp+1, sizeof(int) );
    if( surf_group_local->grp_index == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<surf_group_global->n_grp; i++ ) {
        for( j=surf_group_global->grp_index[i]; j<surf_group_global->grp_index[i+1]; j++ ) {
            elem = surf_group_global->grp_item[2*j];
            if( elem_global2local[elem-1] )  counter++;
        }
        surf_group_local->grp_index[i+1] = counter;
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_surf_grp_item( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh, const int *elem_global2local )
{
    struct hecmwST_surf_grp *surf_group_global = global_mesh->surf_group;
    struct hecmwST_surf_grp *surf_group_local = local_mesh->surf_group;
    int elem, surf;
    int size;
    int counter;
    int i, j;

    size = sizeof(int) * surf_group_local->grp_index[surf_group_local->n_grp] * 2;
    surf_group_local->grp_item = (int *)HECMW_malloc( size );
    if( surf_group_local->grp_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( counter=0, i=0; i<surf_group_global->n_grp; i++ ) {
        for( j=surf_group_global->grp_index[i]; j<surf_group_global->grp_index[i+1]; j++ ) {
            elem = surf_group_global->grp_item[2*j];
            surf = surf_group_global->grp_item[2*j+1];
            if( elem_global2local[elem-1] ) {
                surf_group_local->grp_item[2*counter]   = elem_global2local[elem-1];
                surf_group_local->grp_item[2*counter+1] = surf;
                counter++;
            }
        }
        HECMW_assert( counter == surf_group_local->grp_index[i+1] );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_surf_grp_info( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh, const int *elem_global2local )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->surf_group );
    HECMW_assert( local_mesh );
    HECMW_assert( local_mesh->surf_group );
    HECMW_assert( elem_global2local );

    if( global_mesh->surf_group->n_grp == 0 ) {
        init_struct_surf_grp( local_mesh );
        return RTC_NORMAL;
    }

    rtc = const_surf_n_grp( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_surf_grp_name( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_surf_grp_index( global_mesh, local_mesh, elem_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_surf_grp_item( global_mesh, local_mesh, elem_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_contact_pair_n_pair( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->contact_pair->n_pair = global_mesh->contact_pair->n_pair;

    return RTC_NORMAL;
}


static int
const_contact_pair_name( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    local_mesh->contact_pair->name = global_mesh->contact_pair->name;

    return RTC_NORMAL;
}


static int
const_contact_pair_type( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    struct hecmwST_contact_pair *cpair_global = global_mesh->contact_pair;
    struct hecmwST_contact_pair *cpair_local = local_mesh->contact_pair;
    int i;

    cpair_local->type = (int *)HECMW_calloc( cpair_local->n_pair, sizeof(int) );
    if( cpair_local->type == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<cpair_global->n_pair; i++ ) {
        cpair_local->type[i] = cpair_global->type[i];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_contact_pair_slave_grp_id( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    struct hecmwST_contact_pair *cpair_global = global_mesh->contact_pair;
    struct hecmwST_contact_pair *cpair_local = local_mesh->contact_pair;
    int i;

    cpair_local->slave_grp_id = (int *)HECMW_calloc( cpair_local->n_pair, sizeof(int) );
    if( cpair_local->slave_grp_id == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<cpair_global->n_pair; i++ ) {
        cpair_local->slave_grp_id[i] = cpair_global->slave_grp_id[i];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_contact_pair_master_grp_id( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh )
{
    struct hecmwST_contact_pair *cpair_global = global_mesh->contact_pair;
    struct hecmwST_contact_pair *cpair_local = local_mesh->contact_pair;
    int i;

    cpair_local->master_grp_id = (int *)HECMW_calloc( cpair_local->n_pair, sizeof(int) );
    if( cpair_local->master_grp_id == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    for( i=0; i<cpair_global->n_pair; i++ ) {
        cpair_local->master_grp_id[i] = cpair_global->master_grp_id[i];
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}


static int
const_contact_pair_info( const struct hecmwST_local_mesh *global_mesh,
                     struct hecmwST_local_mesh *local_mesh )
{
    int rtc;

    HECMW_assert( global_mesh );
    HECMW_assert( global_mesh->contact_pair );
    HECMW_assert( local_mesh );
    HECMW_assert( local_mesh->contact_pair );

    if( global_mesh->contact_pair->n_pair == 0 ) {
        init_struct_contact_pair( local_mesh );
        return RTC_NORMAL;
    }

    rtc = const_contact_pair_n_pair( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_contact_pair_name( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_contact_pair_type( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_contact_pair_slave_grp_id( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_contact_pair_master_grp_id( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    return RTC_NORMAL;

error:
    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
const_local_data( const struct hecmwST_local_mesh *global_mesh,
                  struct hecmwST_local_mesh *local_mesh,
                  const struct hecmw_part_cont_data *cont_data,
                  const char *node_flag, const char *elem_flag, int current_domain )
{
    int *node_global2local = NULL;
    int *elem_global2local = NULL;
    int *node_local2global = NULL;
    int *elem_local2global = NULL;
    int rtc;


    node_global2local = (int *)HECMW_calloc( global_mesh->n_node, sizeof(int) );
    if( node_global2local == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = set_node_global2local( global_mesh, local_mesh, node_global2local, node_flag );
    if( rtc != RTC_NORMAL )  goto error;


    node_local2global = (int *)HECMW_calloc( local_mesh->n_node, sizeof(int) );
    if( node_local2global == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = set_node_local2global( global_mesh, local_mesh, node_global2local, node_local2global );
    if( rtc != RTC_NORMAL )  goto error;


    elem_global2local = (int *)HECMW_calloc( global_mesh->n_elem, sizeof(int) );
    if( elem_global2local == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = set_elem_global2local( global_mesh, local_mesh, elem_global2local, elem_flag );
    if( rtc != RTC_NORMAL )  goto error;


    elem_local2global = (int *)HECMW_calloc( local_mesh->n_elem, sizeof(int) );
    if( elem_local2global == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    rtc = set_elem_local2global( global_mesh, local_mesh, elem_global2local, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;


    rtc = const_global_info( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_info( global_mesh, local_mesh, node_local2global, node_flag );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_info( global_mesh, local_mesh,
                           node_global2local, elem_global2local, elem_local2global );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_comm_info( global_mesh, local_mesh,
                           node_global2local, elem_global2local, current_domain );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_adapt_info( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_sect_info( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mat_info( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_mpc_info( global_mesh, local_mesh, node_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_amp_info( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_node_grp_info( global_mesh, local_mesh, node_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_elem_grp_info( global_mesh, local_mesh, elem_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_surf_grp_info( global_mesh, local_mesh, elem_global2local );
    if( rtc != RTC_NORMAL )  goto error;

    rtc = const_contact_pair_info( global_mesh, local_mesh );
    if( rtc != RTC_NORMAL )  goto error;


    HECMW_free( node_global2local );
    HECMW_free( elem_global2local );
    HECMW_free( node_local2global );
    HECMW_free( elem_local2global );

    return RTC_NORMAL;

error:
    HECMW_free( node_global2local );
    HECMW_free( elem_global2local );
    HECMW_free( node_local2global );
    HECMW_free( elem_local2global );
    clean_struct_local_mesh( local_mesh );

    return RTC_ERROR;
}

/*==================================================================================================

  print UCD format data

==================================================================================================*/

static int
print_ucd_entire_set_node_data( struct hecmwST_local_mesh *global_mesh,
                                struct hecmwST_result_data *result_data )
{
    int nn_item;
    int i;

    result_data->nn_component = 0;

    result_data->nn_dof = NULL;

    result_data->node_label = NULL;

    for( nn_item=0, i=0; i<result_data->nn_component; i++ ) {
        nn_item += result_data->nn_dof[i];
    }

    result_data->node_val_item = NULL;

    return RTC_NORMAL;

/* error:      2007/12/27 S.Ito                */
/*     free_struct_result_data( result_data ); */

    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
print_ucd_entire_set_elem_data( struct hecmwST_local_mesh *global_mesh,
                                struct hecmwST_result_data *result_data, char *elem_flag )
{
    int size;
    int ne_item;
    int i;

    result_data->ne_component = 1;

    result_data->ne_dof = (int *)HECMW_malloc( sizeof(int)*result_data->ne_component );
    if( result_data->ne_dof == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    result_data->ne_dof[0] = 1;

    result_data->elem_label = (char **)HECMW_malloc( sizeof(char *)*result_data->ne_component );
    if( result_data->elem_label == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        for( i=0; i<result_data->ne_component; i++ ) {
            result_data->elem_label[i] = NULL;
        }
    }
    for( i=0; i<result_data->ne_component; i++ ) {
        result_data->elem_label[i] = (char *)HECMW_malloc( sizeof(char)*(HECMW_NAME_LEN+1) );
        if( result_data->elem_label[i] == NULL ) {
            HECMW_set_error( errno, "" );
            goto error;
        }
    }
    strcpy( result_data->elem_label[0], "partitioning_image" );

    for( ne_item=0, i=0; i<result_data->ne_component; i++ ) {
        ne_item += result_data->ne_dof[i];
    }

    size = sizeof(double) * ne_item * global_mesh->n_elem;
    result_data->elem_val_item = (double *)HECMW_malloc( size );
    if( result_data->elem_val_item == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }

    switch( global_mesh->hecmw_flag_parttype ) {
    case HECMW_FLAG_PARTTYPE_NODEBASED:
        for( i=0; i<global_mesh->n_elem; i++ ) {
            if( EVAL_BIT( elem_flag[i], OVERLAP ) ) {
                result_data->elem_val_item[i] = (double)global_mesh->n_subdomain + 2.0;
            } else {
                result_data->elem_val_item[i] = (double)global_mesh->elem_ID[2*i+1];
            }
        }
        break;

    case HECMW_FLAG_PARTTYPE_ELEMBASED:
        for( i=0; i<global_mesh->n_elem; i++ ) {
            result_data->elem_val_item[i] = (double)global_mesh->elem_ID[2*i+1];
        }
        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "%d", global_mesh->hecmw_flag_parttype );
        goto error;
    }

    return RTC_NORMAL;

error:
    free_struct_result_data( result_data );

    return RTC_ERROR;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
print_ucd_entire( struct hecmwST_local_mesh *global_mesh, char *elem_flag, char *ofname )
{
    struct hecmwST_result_data *result_data;

    result_data = (struct hecmwST_result_data *)HECMW_malloc( sizeof(struct hecmwST_result_data) );
    if( result_data == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    } else {
        init_struct_result_data( result_data );
    }


    if( print_ucd_entire_set_node_data( global_mesh, result_data ) ) {
        goto error;
    }


    if( print_ucd_entire_set_elem_data( global_mesh, result_data, elem_flag ) ) {
        goto error;
    }


    if( HECMW_ucd_print( global_mesh, result_data, ofname ) ) {
        goto error;
    }

    free_struct_result_data( result_data );

    return RTC_NORMAL;

error:
    free_struct_result_data( result_data );

    return RTC_ERROR;
}




static int
init_partition( struct hecmwST_local_mesh *global_mesh,
                struct hecmw_part_cont_data *cont_data )
{
    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Starting initialization for partitioner..." );
    }

    /* global_mesh->n_subdomain */
    global_mesh->n_subdomain = cont_data->n_domain;

    /* global_mesh->hecmw_flag_parttype */
    switch( cont_data->type ) {
    case HECMW_PART_TYPE_NODE_BASED:     /* for node-based partitioning */
        global_mesh->hecmw_flag_parttype = HECMW_FLAG_PARTTYPE_NODEBASED;
        break;

    case HECMW_PART_TYPE_ELEMENT_BASED:  /* for element-based partitioning */
        global_mesh->hecmw_flag_parttype = HECMW_FLAG_PARTTYPE_ELEMBASED;
        break;

    default:
        HECMW_set_error( HECMW_PART_E_INVALID_PTYPE, "%d", cont_data->type );
        goto error;
    }

    /* global_mesh->hecmw_flag_partdepth */
    global_mesh->hecmw_flag_partdepth = cont_data->depth;

    if( HECMW_PART_VERBOSE_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Initialization for partitioner done" );
    }

    return RTC_NORMAL;

error:
    return RTC_ERROR;;
}

/*==================================================================================================

  main function

==================================================================================================*/

extern struct hecmwST_local_mesh *
HECMW_partition_inner( struct hecmwST_local_mesh *global_mesh,
                       struct hecmw_part_cont_data *cont_data )
{
    struct hecmwST_local_mesh *local_mesh = NULL;
    struct hecmw_ctrl_meshfiles *ofheader = NULL;
    char *node_flag = NULL;
    char *elem_flag = NULL;
    char ofname[HECMW_FILENAME_LEN+1];
    int current_domain;
    int rtc;
    int i;

    if( global_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'global_mesh\' is NULL" );
        goto error;
    }
    if( cont_data == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'cont_data\' is NULL" );
        goto error;
    }


    rtc = init_partition( global_mesh, cont_data );
    if( rtc != RTC_NORMAL )  goto error;


    if( global_mesh->my_rank == 0 ) {
        rtc = HECMW_part_init_log( global_mesh->n_subdomain );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = HECMW_part_set_log_part_type( cont_data->type );
        if( rtc != RTC_NORMAL )  goto error;
        rtc = HECMW_part_set_log_part_method( cont_data->method );
        if( rtc != RTC_NORMAL )  goto error;
        rtc = HECMW_part_set_log_part_depth( cont_data->depth );
        if( rtc != RTC_NORMAL )  goto error;

        rtc = HECMW_part_set_log_n_node_g( global_mesh->n_node );
        if( rtc != RTC_NORMAL )  goto error;
        rtc = HECMW_part_set_log_n_elem_g( global_mesh->n_elem );
        if( rtc != RTC_NORMAL )  goto error;
    }


    rtc = wnumbering( global_mesh, cont_data );
    if( rtc != RTC_NORMAL )  goto error;

    node_flag = (char *)HECMW_calloc( global_mesh->n_node, sizeof(char) );
    if( node_flag == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }
    elem_flag = (char *)HECMW_calloc( global_mesh->n_elem, sizeof(char) );
    if( elem_flag == NULL ) {
        HECMW_set_error( errno, "" );
        goto error;
    }


    local_mesh = HECMW_dist_alloc( );
    if( local_mesh == NULL )  goto error;

#if 0
    for( i=0; i<cont_data->n_my_domain; i++ ) {
#endif
    for( i=0; i<global_mesh->n_subdomain; i++ ) {
#if 0
        current_domain = cont_data->my_domain[i]; */
#endif
        current_domain = i;

        if( !HECMW_PART_SILENT_MODE ) {
            HECMW_log( HECMW_LOG_INFO, "Creating local mesh for domain #%d ...", current_domain );
        }


        rtc = create_neighbor_info( global_mesh, local_mesh, node_flag, elem_flag, current_domain );
        if( rtc != RTC_NORMAL )  goto error;


        if( global_mesh->n_subdomain > 1 ) {
            rtc = create_comm_info( global_mesh, local_mesh, node_flag, elem_flag, current_domain );
            if( rtc != RTC_NORMAL )  goto error;
        }


        rtc = const_local_data( global_mesh, local_mesh, cont_data,
                                node_flag, elem_flag, current_domain );
        if( rtc != RTC_NORMAL )  goto error;

        /* set value to profile */
        rtc = HECMW_part_set_log_n_node( current_domain, local_mesh->n_node );
        if( rtc != 0 )  goto error;
        rtc = HECMW_part_set_log_nn_internal( current_domain, local_mesh->nn_internal );
        if( rtc != 0 )  goto error;
        rtc = HECMW_part_set_log_n_elem( current_domain, local_mesh->n_elem );
        if( rtc != 0 )  goto error;
        rtc = HECMW_part_set_log_ne_internal( current_domain, local_mesh->ne_internal );
        if( rtc != 0 )  goto error;


        ofheader = HECMW_ctrl_get_meshfiles_header( "part_out" );
        if( ofheader == NULL ) {
            HECMW_log( HECMW_LOG_ERROR, "not set output file header" );
            goto error;
        }
        if( ofheader->n_mesh == 0 ) {
            HECMW_log( HECMW_LOG_ERROR, "output file name is not set" );
            goto error;
        }

        get_dist_file_name( ofheader->meshfiles[0].filename, current_domain, ofname );
        HECMW_assert( ofname != NULL );

        HECMW_put_dist_mesh( local_mesh, ofname );


        clean_struct_local_mesh( local_mesh );

        HECMW_ctrl_free_meshfiles( ofheader );
        ofheader = NULL;
    }


    if( cont_data->is_print_ucd == 1 ) {
        if( global_mesh->my_rank == 0 ) {
            print_ucd_entire( global_mesh, elem_flag, cont_data->ucd_file_name );
        }
    }

    if( global_mesh->my_rank == 0 ) {
        rtc = HECMW_part_print_log( );
        if( rtc )  goto error;
    }
    HECMW_part_finalize_log( );


    HECMW_dist_free( local_mesh );

    HECMW_free( node_flag );
    HECMW_free( elem_flag );

    return global_mesh;

error:
    HECMW_free( node_flag );
    HECMW_free( elem_flag );
    HECMW_dist_free( local_mesh );
    if( ofheader ) {
        HECMW_ctrl_free_meshfiles( ofheader );
    }
    HECMW_part_finalize_log( );

    return NULL;
}


extern struct hecmwST_local_mesh *
HECMW_partition( struct hecmwST_local_mesh *global_mesh )
{
    struct hecmwST_local_mesh *local_mesh;
    struct hecmw_part_cont_data *cont_data;

    if( !HECMW_PART_SILENT_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Starting domain decomposition...\n" );
    }

    if( global_mesh == NULL ) {
        HECMW_set_error( HECMW_PART_E_INV_ARG, "\'global_mesh\' is NULL" );
        goto error;
    }

    cont_data = HECMW_part_get_control( global_mesh );
    if( cont_data == NULL )  goto error;

    local_mesh = HECMW_partition_inner( global_mesh, cont_data );
    if( local_mesh == NULL )  goto error;

    HECMW_part_free_control( cont_data );

    if( !HECMW_PART_SILENT_MODE ) {
        HECMW_log( HECMW_LOG_INFO, "Domain decomposition done\n" );
    }

    return local_mesh;

error:
    return NULL;
}