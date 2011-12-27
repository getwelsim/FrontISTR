/*=====================================================================*
 *                                                                     *
 *   Software Name : HEC-MW Library for PC-cluster                     *
 *         Version : 2.3                                               *
 *                                                                     *
 *     Last Update : 2007/12/03                                        *
 *        Category : I/O and Utility                                   *
 *                                                                     *
 *            Written by Satoshi Ito (Univ. of Tokyo)                  *
 *                       Noboru Imai (Univ. of Tokyo)                  *
 *                       Kazuaki Sakane (RIST)                         *
 *                                                                     *
 *     Contact address :  IIS,The University of Tokyo RSS21 project    *
 *                                                                     *
 *     "Structural Analysis System for General-purpose Coupling        *
 *      Simulations Using High End Computing Middleware (HEC-MW)"      *
 *                                                                     *
 *=====================================================================*/



#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <ctype.h>
#include "hecmw_util.h"
#include "hecmw_config.h"
#include "hecmw_result.h"
#include "hecmw_bin_io.h"


#define COL_INT 10
#define COL_DOUBLE 5 
#define LINEBUF_SIZE 1023

struct result_list {
	char *label;
	double *ptr;
	int n_dof;
	struct result_list *next;
};

static int step;
static int nnode;
static int nelem;
static char head[HECMW_HEADER_LEN+1];
static char line_buf[LINEBUF_SIZE+1];

static struct result_list *node_list; 
static struct result_list *elem_list; 

static int *node_global_ID;
static int *elem_global_ID;

struct fortran_remainder {
	double *ptr;
	struct fortran_remainder *next;
};

static struct fortran_remainder *remainder;	/* for Fortran */


static int
is_valid_label(char *label)
{
#define ALLOW_CHAR_FIRST "_"	/* and alphabet */
#define ALLOW_CHAR "_-"			/* and alphabet, digit */
	int c;
	char *p,*q;

	if(label == NULL) return 0;

	c = label[0];

	/* check first character */
	if(!isalpha(c)) {
		q = ALLOW_CHAR_FIRST;
		while(*q) {
			if(*q == c) break;
			q++;
		}
		if(!*q) return 0;
	}

	/* check 2nd character or later */
	p = &label[1];
	while(*p) {
		if(!isalnum(*p)) {
			q = ALLOW_CHAR;
			while(*q) {
				if(*q == *p) break;
				q++;
			}
			if(!*q) return 0;
		}
		p++;
	}
	return 1;
}


static void
clear() 
{
	struct result_list *p,*q;

	for(p=node_list; p; p=q) {
		q = p->next;
		HECMW_free(p->label);
		HECMW_free(p);
	}
	node_list = NULL;

	for(p=elem_list; p; p=q) {
		q = p->next;
		HECMW_free(p->label);
		HECMW_free(p);
	}
	elem_list = NULL;

	nnode = nelem = 0;
	strcpy(head, "");
}



void
HECMW_result_free(struct hecmwST_result_data *result)
{
	int i;

	if(result == NULL) return;

	if(result->nn_component > 0) {
		HECMW_free(result->nn_dof);
		HECMW_free(result->node_val_item);
		for(i=0; i < result->nn_component; i++) {
			HECMW_free(result->node_label[i]);
		}
		HECMW_free(result->node_label);
	}

	if(result->ne_component > 0) {
		HECMW_free(result->ne_dof);
		HECMW_free(result->elem_val_item);
		for(i=0; i < result->ne_component; i++) {
			HECMW_free(result->elem_label[i]);
		}
		HECMW_free(result->elem_label);
	}

	HECMW_free(result);
}



int
HECMW_result_init(struct hecmwST_local_mesh *hecMESH, int tstep, char *header)
{
        return HECMW_result_init_body(hecMESH->n_node, hecMESH->n_elem,
				      hecMESH->global_node_ID, hecMESH->global_elem_ID, tstep, header);
}


int
HECMW_result_init_body(int n_node, int n_elem, int *nodeID, int *elemID, int tstep, char *header)
{
	int len;
	char *p,*q;

	nnode = n_node;
	nelem = n_elem;
	step = tstep;

/*********************** 2007/9/6/ ***********************/
	node_global_ID = nodeID;
	elem_global_ID = elemID;

/* 	fprintf(stderr,"(init_body) N_ID check\n"); */
/* 	fprintf(stderr,"(init_body) N_ID = %d\n", nodeID[1]); */
	
	if(header == NULL) {
		head[0] = '\0';
		return 0;
	}

	len = 0;
	p = header;
	q = head;
	while(len < sizeof(head)-1 && *p && *p != '\n') {
		*q++ = *p++;
		len++;
	}
	*++q = '\0';

	return 0;
}



int
HECMW_result_finalize(void)
{
	clear();
	return 0;
}


static int
add_to_node_list(struct result_list *result)
{
	struct result_list *p,*q;

	q = NULL;	
	for(p=node_list; p; p=(q=p)->next);

	if(q == NULL) {
		node_list = result;
	} else {
		q->next = result;
	}
	return 0;
}


static int
add_to_elem_list(struct result_list *result)
{
	struct result_list *p,*q;

	q = NULL;	
	for(p=elem_list; p; p=(q=p)->next);

	if(q == NULL) {
		elem_list = result;
	} else {
		q->next = result;
	}
	return 0;
}


static struct result_list *
make_result_list(int n_dof, char *label, double *ptr)
{
	struct result_list *result = NULL;
	char *new_label = NULL;

	result = HECMW_malloc(sizeof(*result));
	if(result == NULL) {
		HECMW_set_error(errno, "");
		goto error;
	}
	
	new_label = HECMW_strdup(label);
	if(new_label == NULL) {
		HECMW_set_error(errno, "");
		goto error;
	}

	result->label = new_label;
	result->ptr = ptr; 
	result->n_dof = n_dof;
	result->next = NULL;

	return result;
error:
	HECMW_free(result);
	HECMW_free(new_label);
	return NULL;
}



int
HECMW_result_add(int node_or_elem, int n_dof, char *label, double *ptr)
{
	struct result_list *result = NULL;

	if(node_or_elem != 1 && node_or_elem != 2) {
		HECMW_set_error(HECMW_UTIL_E0206, "");
		goto error;
	}

	if(!is_valid_label(label)) {
		HECMW_set_error(HECMW_UTIL_E0207, "");
		goto error;
	}

	result = make_result_list(n_dof, label, ptr);
	if(result == NULL) {
		goto error;
	}

	if(node_or_elem == 1) {
		/* node */
		if(add_to_node_list(result)) goto error;
	} else {
		/* elem */
		if(add_to_elem_list(result)) goto error;
	}

	return 0;
error:
	HECMW_free(result);
	return -1;
}


static int
count_nn_comp(void)
{
	int nn_comp = 0;
	struct result_list *p;

	for(p=node_list; p; p=p->next) {
		nn_comp++;
	}
	return nn_comp;
}


static int
count_ne_comp(void)
{
	int ne_comp = 0;
	struct result_list *p;

	for(p=elem_list; p; p=p->next) {
		ne_comp++;
	}
	return ne_comp;
}


/*---------------------------------------------------------------------------*/
/* TEXT MODE I/O (STATIC)                                                    */
/*---------------------------------------------------------------------------*/

#include "res_txt_io.inc"

/*---------------------------------------------------------------------------*/
/* BINARY MODE I/O (STATIC)                                                  */
/*---------------------------------------------------------------------------*/

#include "res_bin_io.inc"

/*---------------------------------------------------------------------------*/
/* TEXT MODE WRITE                                                           */
/*---------------------------------------------------------------------------*/

int
HECMW_result_write_txt_by_fname(char *filename)
{
	FILE *fp = NULL;

	if((fp = fopen(filename, "w")) == NULL) {
		HECMW_set_error(HECMW_UTIL_E0201, "File: %s, %s", filename, HECMW_strmsg(errno));
		goto error;
	}

	if(output_result_data(fp)) {
		goto error;
	}

	if(fclose(fp)) {
		HECMW_set_error(HECMW_UTIL_E0202, HECMW_strmsg(errno));
		goto error;
	}
	fp = NULL;

	return 0;
error:
	if(fp) fclose(fp);
	return -1;
}



/*---------------------------------------------------------------------------*/


int
HECMW_result_write_txt_ST_by_fname(char *filename, struct hecmwST_result_data *result,
		int n_node, int n_elem, char *header)
{
	FILE *fp = NULL;

	if((fp = fopen(filename, "w")) == NULL) {
		HECMW_set_error(HECMW_UTIL_E0201, "File: %s, %s", filename, HECMW_strmsg(errno));
		goto error;
	}

	if(output_result_data_ST(result, n_node, n_elem, header, fp)) {
		goto error;
	}

	if(fclose(fp)) {
		HECMW_set_error(HECMW_UTIL_E0202, HECMW_strmsg(errno));
		goto error;
	}
	fp = NULL;

	return 0;
error:
	if(fp) fclose(fp);
	return -1;
}



/*---------------------------------------------------------------------------*/
/* BINARY MODE WRITE                                                         */
/*---------------------------------------------------------------------------*/



int
HECMW_result_write_bin_by_fname(char *filename)
{
	FILE *fp = NULL;

	if((fp = fopen(filename, "wb")) == NULL) {
		HECMW_set_error(HECMW_UTIL_E0201, "File: %s, %s", filename, HECMW_strmsg(errno));
		goto error;
	}

	hecmw_set_endian_info();
	if(write_bin_header(fp)) goto error;
	if(bin_output_result_data(fp)) goto error;

	if(fclose(fp)) {
		HECMW_set_error(HECMW_UTIL_E0202, HECMW_strmsg(errno));
		goto error;
	}
	fp = NULL;

	return 0;
error:
	if(fp) fclose(fp);
	return -1;
}



int
HECMW_result_write_bin_ST_by_fname(char *filename, struct hecmwST_result_data *result,
		int n_node, int n_elem, char *header)
{
	FILE *fp = NULL;

	if((fp = fopen(filename, "wb")) == NULL) {
		HECMW_set_error(HECMW_UTIL_E0201, "File: %s, %s", filename, HECMW_strmsg(errno));
		goto error;
	}

	hecmw_set_endian_info();
	if(write_bin_header(fp)) goto error;
	if(bin_output_result_data_ST(result, n_node, n_elem, header, fp)) goto error;

	if(fclose(fp)) {
		HECMW_set_error(HECMW_UTIL_E0202, HECMW_strmsg(errno));
		goto error;
	}
	fp = NULL;

	return 0;
error:
	if(fp) fclose(fp);
	return -1;
}



/*---------------------------------------------------------------------------*/
/* UNIVERSAL I/O                                                             */
/*---------------------------------------------------------------------------*/


int
HECMW_result_write(void)
{
	return HECMW_result_write_by_name(NULL);
}


int
HECMW_result_write_by_name(char *name_ID)
{
	char filename[HECMW_FILENAME_LEN+1];
	int fg_text = 1;

	if(name_ID) {
		if(HECMW_ctrl_get_result_file(name_ID, filename, sizeof(filename),&fg_text) == NULL) {
			return -1;
		}
	} else {
		if(HECMW_ctrl_get_result_file_type_by_io(HECMW_CTRL_FILE_IO_OUT, filename, sizeof(filename), &fg_text) == NULL) {
			return -1;
		}
	}

	/* add step */
	sprintf(filename+strlen(filename), ".%d", step);

	if( fg_text ){
		if(HECMW_result_write_txt_by_fname(filename)) return -1;
	} else {
		if(HECMW_result_write_bin_by_fname(filename)) return -1;
	}
	return 0;
}


int HECMW_result_write_by_fname(char *filename)
{
	return HECMW_result_write_txt_by_fname(filename);
}


/*---------------------------------------------------------------------------*/

int
HECMW_result_write_ST_by_fname(char *filename, struct hecmwST_result_data *result,
		int n_node, int n_elem, char *header)
{
	return HECMW_result_write_txt_ST_by_fname( filename, result, n_node, n_elem, header);
}

/*---------------------------------------------------------------------------*/


int
HECMW_result_write_ST(struct hecmwST_result_data *result, int n_node, int n_elem, int tstep, char *header)
{
	return HECMW_result_write_ST_by_name(NULL, result, n_node, n_elem, step, header);
}


int
HECMW_result_write_ST_by_name(char *name_ID, struct hecmwST_result_data *result, 
			int n_node, int n_elem, int tstep, char *header)
{
	char filename[HECMW_FILENAME_LEN+1];
	int fg_text = 1;

	if(name_ID) {
		if(HECMW_ctrl_get_result_file(name_ID, filename, sizeof(filename), &fg_text) == NULL) {
			return -1;
		}
	} else {
		if(HECMW_ctrl_get_result_file_by_io(HECMW_CTRL_FILE_IO_OUT, filename, sizeof(filename), &fg_text) == NULL) {
			return -1;
		}
	}

	/* add step */
	sprintf(filename+strlen(filename), ".%d", step);

	if(fg_text){
		if(HECMW_result_write_txt_ST_by_fname(filename, result, n_node, n_elem, header)) return -1;
	} else {
		if(HECMW_result_write_bin_ST_by_fname(filename, result, n_node, n_elem, header)) return -1;
	}
	return 0;
}



/*---------------------------------------------------------------------------*/


struct hecmwST_result_data *
HECMW_result_read_by_fname(char *filename)
{
	struct hecmwST_result_data *result;

	if( judge_result_bin_file( filename )) {
		result = result_read_bin_by_fname(filename);
	} else {
		result = result_read_txt_by_fname(filename);
	}

	return result;
}



struct hecmwST_result_data *
HECMW_result_read_by_name(char *name_ID, int tstep)
{
	char filename[HECMW_FILENAME_LEN+1];
	struct hecmwST_result_data *result;
	int fg_text;

	if(name_ID) {
		if(HECMW_ctrl_get_result_file(name_ID, filename, sizeof(filename), &fg_text) == NULL) {
			return NULL;
		}
	} else {
		if(HECMW_ctrl_get_result_file_by_io(HECMW_CTRL_FILE_IO_IN, filename, sizeof(filename), &fg_text ) == NULL) {
			return NULL;
		}
	}

	/* IGNORE fg_text */

	sprintf(filename+strlen(filename), ".%d", tstep);

	result = HECMW_result_read_by_fname(filename);
	if(result == NULL) {
		return NULL;
	}

	return result;
}


struct hecmwST_result_data *
HECMW_result_read(int tstep)
{
	return HECMW_result_read_by_name(NULL, tstep);
}


/*---------------------------------------------------------------------------*/
/* etc.                                                                      */
/*---------------------------------------------------------------------------*/


int
HECMW_result_get_nnode(void)
{
	return nnode;
}


int
HECMW_result_get_nelem(void)
{
	return nelem;
}

char* 
HECMW_result_get_header(char* buff)
{
	strcpy( buff, head );
	return buff;
}

int* 
HECMW_result_get_nodeID(int* buff)
{
	int i;
	for(i=0; i<nnode; i++) {
		buff[i] = node_global_ID[i];
	}
	return buff;
}

int* 
HECMW_result_get_elemID(int* buff)
{
	int i;
	for(i=0; i<nelem; i++) {
		buff[i] = elem_global_ID[i];
	}
	return buff;
}


/*---------------------------------------------------------------------------*/
/* FORTRAN INTERFACE                                                         */
/*---------------------------------------------------------------------------*/


void
hecmw_result_init_if(int *n_node, int *n_elem, int *nodeID, int *elemID,
			int *tstep, char *header, int *err, int len)
{
	char header_str[HECMW_HEADER_LEN+1];

	*err = 1;

	if(HECMW_strcpy_f2c_r(header,len,header_str,sizeof(header_str)) == NULL) {
		return;
	}
/* 	fprintf(stderr, "(init_if) nodeID check start!\n"); */
/* 	fprintf(stderr,"(init_if) nodeID[1] = %d\n",nodeID[1]);	 */
	if(HECMW_result_init_body(*n_node, *n_elem, nodeID, elemID, *tstep, header_str)) {
		return;
	}

	*err = 0;
}



void
hecmw_result_init_if_(int *n_node, int *n_elem, int *nodeID, int *elemID,
		int *tstep, char *header, int *err, int len)
{
	hecmw_result_init_if(n_node, n_elem, nodeID, elemID, tstep, header, err, len);
}



void
hecmw_result_init_if__(int *n_node, int *n_elem, int *nodeID, int *elemID,
		int *tstep, char *header, int *err, int len)
{
	hecmw_result_init_if(n_node, n_elem, nodeID, elemID, tstep, header, err, len);
}



void
HECMW_RESULT_INIT_IF(int *n_node, int *n_elem, int *nodeID, int *elemID,
		int *tstep, char *header, int *err, int len)
{
	hecmw_result_init_if(n_node, n_elem, nodeID, elemID, tstep, header, err, len);
}


/*---------------------------------------------------------------------------*/


void hecmw_result_finalize_if(int *err)
{
	*err = 1;

	if(HECMW_result_finalize()) {
		return;
	}

	*err = 0;
}



void hecmw_result_finalize_if_(int *err)
{
	hecmw_result_finalize_if(err);
}



void hecmw_result_finalize_if__(int *err)
{
	hecmw_result_finalize_if(err);
}



void HECMW_RESULT_FINALIZE_IF(int *err)
{
	hecmw_result_finalize_if(err);
}


/*---------------------------------------------------------------------------*/


void
hecmw_result_add_if(int *node_or_elem, int *n_dof, char *label,
			double *ptr, int *err, int len)
{
	char label_str[HECMW_NAME_LEN+1];
	int n,size;
	double *data;
	struct fortran_remainder *remain;

	*err = 1;

	if(HECMW_strcpy_f2c_r(label, len, label_str, sizeof(label_str)) == NULL) {
		return;
	}

	if(*node_or_elem == 1) {
		n = nnode;
	} else {
		n = nelem;
	}
	size = sizeof(double) * n * (*n_dof);
	data = HECMW_malloc(size);
	if(data == NULL) {
		HECMW_set_error(errno, "");
		return;
	}
	memcpy(data, ptr, size);

	remain = HECMW_malloc(sizeof(*remain));
	if(remain == NULL) {
		HECMW_set_error(errno, "");
		return;
	}
	remain->ptr = data;
	remain->next = remainder;
	remainder = remain;

	if(HECMW_result_add(*node_or_elem, *n_dof, label_str, data)) {
		return;
	}

	*err = 0;
}



void
hecmw_result_add_if_(int *node_or_elem, int *n_dof, char *label,
				double *ptr, int *err, int len)
{
	hecmw_result_add_if(node_or_elem, n_dof, label, ptr, err, len);
}



void
hecmw_result_add_if__(int *node_or_elem, int *n_dof, char *label,
				double *ptr, int *err, int len)
{
	hecmw_result_add_if(node_or_elem, n_dof, label, ptr, err, len);
}



void
HECMW_RESULT_ADD_IF(int *node_or_elem, int *n_dof, char *label,
				double *ptr, int *err, int len)
{
	hecmw_result_add_if(node_or_elem, n_dof, label, ptr, err, len);
}


/*----------------------------------------------------------------------------*/


void
hecmw_result_write_by_name_if(char *name_ID, int *err, int len)
{
	char *name = NULL;
	char name_ID_str[HECMW_NAME_LEN+1];
	struct fortran_remainder *p,*q;

	*err = 1;

	if(name_ID) {
		if(HECMW_strcpy_f2c_r(name_ID,len, name_ID_str,sizeof(name_ID_str)) == NULL) {
			return;
		}
		name = name_ID_str;
	}

	if(HECMW_result_write_by_name(name)) {
		return;
	}

	for(p=remainder; p; p=q) {
		q = p->next;
		HECMW_free(p->ptr);
		HECMW_free(p);
	}
	remainder = NULL;

	*err = 0;
}


void
hecmw_result_write_by_name_if_(char *name_ID, int *err, int len)
{
	hecmw_result_write_by_name_if(name_ID, err, len);
}


void
hecmw_result_write_by_name_if__(char *name_ID, int *err, int len)
{
	hecmw_result_write_by_name_if(name_ID, err, len);
}


void
HECMW_RESULT_WRITE_BY_NAME_IF(char *name_ID, int *err, int len)
{
	hecmw_result_write_by_name_if(name_ID, err, len);
}


/*----------------------------------------------------------------------------*/

void
hecmw_result_write_if(int *err)
{
	hecmw_result_write_by_name_if(NULL, err, 0); 
}


void
hecmw_result_write_if_(int *err)
{
	hecmw_result_write_if(err);
}


void
hecmw_result_write_if__(int *err)
{
	hecmw_result_write_if(err);
}


void
HECMW_RESULT_WRITE_IF(int *err)
{
	hecmw_result_write_if(err);
}