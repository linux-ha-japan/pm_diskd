/* 
 * ccmlib_memapi.c: Consensus Cluster Membership API
 *
 * Copyright (c) International Business Machines  Corp., 2002
 * Author: Ram Pai (linuxram@us.ibm.com)
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <ccmlib.h>

/* structure to track the membership delivered to client */
typedef struct mbr_track_s {
	int			m_count;
	int			m_size;
	oc_ev_membership_t 	m_mem;
} mbr_track_t;


typedef struct mbr_private_s {
	int			magiccookie;
	oc_ev_callback_t 	*callback; /* the callback function registered
					      	by the client */
	struct IPC_CHANNEL      *channel; /* the channel to talk to ccm */
	ccm_llm_t 		*llm;	 /* list of all nodes */
	GHashTable  		*bornon; /* list of born time 
					    for all nodes */
	void 			*cookie; /* the last known 
					    membership event cookie */
} mbr_private_t;


#define OC_EV_SET_INSTANCE(m,trans)  m->m_mem.m_instance=trans
#define OC_EV_SET_N_MEMBER(m,n)  m->m_mem.m_n_member=n
#define OC_EV_SET_MEMB_IDX(m,idx)  m->m_mem.m_memb_idx=idx
#define OC_EV_SET_N_OUT(m,n)  m->m_mem.m_n_out=n
#define OC_EV_SET_OUT_IDX(m,idx)  m->m_mem.m_out_idx=idx
#define OC_EV_SET_N_IN(m,n)  m->m_mem.m_n_in=n
#define OC_EV_SET_IN_IDX(m,idx)  m->m_mem.m_in_idx=idx
#define OC_EV_SET_NODEID(m,idx,nodeid)  m->m_mem.m_array[idx].node_id=nodeid
#define OC_EV_SET_BORN(m,idx,born)  m->m_mem.m_array[idx].node_born_on=born

#define OC_EV_INC_N_MEMBER(m)  m->m_mem.m_n_member++
#define OC_EV_INC_N_IN(m)  m->m_mem.m_n_in++
#define OC_EV_INC_N_OUT(m)  m->m_mem.m_n_out++

#define OC_EV_SET_COUNT(m,count)  m->m_count=count
#define OC_EV_INC_COUNT(m)  ++m->m_count
#define OC_EV_DEC_COUNT(m)  --m->m_count

#define OC_EV_SET_SIZE(m,size)  m->m_size=size
#define OC_EV_SET_DONEFUNC(m,f)  m->m_func=f

#define OC_EV_GET_INSTANCE(m)  m->m_mem.m_instance
#define OC_EV_GET_N_MEMBER(m)  m->m_mem.m_n_member
#define OC_EV_GET_MEMB_IDX(m)  m->m_mem.m_memb_idx
#define OC_EV_GET_N_OUT(m)  m->m_mem.m_n_out
#define OC_EV_GET_OUT_IDX(m)  m->m_mem.m_out_idx
#define OC_EV_GET_N_IN(m)  m->m_mem.m_n_in
#define OC_EV_GET_IN(m)  m->m_mem.m_in_idx
#define OC_EV_GET_NODEARRY(m)  m->m_mem.m_array
#define OC_EV_GET_NODE(m,idx)  m->m_mem.m_array[idx]
#define OC_EV_GET_NODEID(m,idx)  m->m_mem.m_array[idx].node_id
#define OC_EV_GET_BORN(m,idx)  m->m_mem.m_array[idx].node_born_on

#define OC_EV_COPY_NODE(m1,idx1,m2,idx2)  \
		m1->m_mem.m_array[idx1]=m2->m_mem.m_array[idx2]

#define OC_EV_GET_SIZE(m)  m->m_size


/* prototypes of external functions used in this file 
 * Should be made part of some header file 
 */
void * cookie_construct(void (*f)(void *), void *);
void * cookie_get_data(void *);
void * cookie_get_func(void *);
void   cookie_destruct(void *);



static void
init_llm(mbr_private_t *mem, struct IPC_MESSAGE *msg)
{
	unsigned long len = msg->msg_len;
	int	numnodes;

	mem->llm = (ccm_llm_t *)g_malloc(len);
	memcpy(mem->llm, msg->msg_body, len);

	mem->bornon = g_hash_table_new(g_direct_hash, 
				g_direct_equal);

	numnodes = CLLM_GET_NODECOUNT(mem->llm);
	mem->cookie = NULL;
	return;
}

static void
init_bornon(mbr_private_t *private, 
		struct IPC_MESSAGE *msg)
{
	ccm_born_t *born;
	int numnodes, i, n;
	struct born_s *bornon;

	numnodes = CLLM_GET_NODECOUNT(private->llm);

	born = (ccm_born_t *)msg->msg_body;

	n = born->n;
	//fprintf(stderr,"n=%d, msg->msg_len=%ld\n",n,msg->msg_len);
	assert(msg->msg_len == sizeof(ccm_born_t)
			+n*sizeof(struct born_s));
	bornon = born->born;
	for (i = 0 ; i < n; i++) {
		assert(bornon[i].index <= numnodes);
		g_hash_table_insert(private->bornon, 
			GINT_TO_POINTER(CLLM_GET_UUID(private->llm, 
					bornon[i].index)),
			GINT_TO_POINTER(bornon[i].bornon+1));
	}
	return;
}



static int
init_llmborn(mbr_private_t *private)
{
	fd_set rset;
	struct IPC_CHANNEL *ch;
	int 	sockfd, i=0, ret;
	struct IPC_MESSAGE *msg;
	struct timeval tv;

	if(private->llm) return 0;

	ch 	   = private->channel;
	sockfd = ch->ops->get_recv_select_fd(ch);

	/* receive the initiale low level membership 
	*  information in the first iteration, and 
	*  recieve the bornon information in the 
	*  second iteration 
	*/
	while( i < 2) {

		FD_ZERO(&rset);
		FD_SET(sockfd,&rset);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		if(!ch->ops->is_message_pending(ch) && (select(sockfd + 1, 
				&rset, NULL,NULL,&tv)) == -1){
			perror("select");
			ch->ops->destroy(ch);
			return -1;
		}
		ret = ch->ops->recv(ch,&msg);
		if(ret == IPC_BROKEN) {
			printf("connection denied\n");
			return -1;
		}
	 	if(ret == IPC_FAIL){
			fprintf(stderr,".");
			sleep(1);
			continue;
		}

		switch(i) {
		case 0: init_llm(private, msg);
			break;
		case 1: init_bornon(private, msg);
			break;
		}
		i++;
		msg->msg_done(msg);
	}
	return 0;
}


static gboolean
class_valid(class_t *class)
{
	mbr_private_t 	*private;

	if(class->type != OC_EV_MEMB_CLASS) {
		return FALSE;
	}

	private = (mbr_private_t *)class->private;

	if(!private || 
		private->magiccookie != 0xabcdef){
		return FALSE;
	}
	return TRUE;
}



static gboolean
already_present(oc_node_t *arr, uint size, oc_node_t node)
{
	int i;
	for ( i = 0 ; i < size ; i ++ ) {
		if(arr[i].node_id == node.node_id) {
			return TRUE;
		}
	}
	return FALSE;
}

static int
compare(const void *value1, const void *value2)
{
	const oc_node_t *t1 = (const oc_node_t *)value1;
	const oc_node_t *t2 = (const oc_node_t *)value2;

	if (t1->node_born_on < t2->node_born_on) return -1;
	if (t1->node_born_on > t2->node_born_on) return 1;
	if (t1->node_id < t2->node_id) return -1;
	if (t1->node_id > t2->node_id) return 1;
	return 0;
}

static int
get_new_membership(mbr_private_t *private,
		ccm_meminfo_t *mbrinfo,
		int		len,
		mbr_track_t **mbr)
{
	mbr_track_t *newmbr, *oldmbr;
	int trans, i, j, in_index, out_index, born;
	int n_members,uuid;
	


	int n_nodes = CLLM_GET_NODECOUNT(private->llm);

	int size    = sizeof(mbr_track_t) + 
			2*n_nodes*sizeof(oc_node_t);
 	newmbr = *mbr = (mbr_track_t *) g_malloc(size);



	trans = OC_EV_SET_INSTANCE(newmbr,mbrinfo->trans);
	n_members = OC_EV_SET_N_MEMBER(newmbr,mbrinfo->n);

	j = OC_EV_SET_MEMB_IDX(newmbr,0);

	for ( i = 0 ; i < n_members; i++ ) {
		uuid =  CLLM_GET_UUID(private->llm, mbrinfo->member[i]);

		OC_EV_SET_NODEID(newmbr,j,uuid);

		born = GPOINTER_TO_INT(g_hash_table_lookup(private->bornon, 
				GINT_TO_POINTER(mbrinfo->member[i])));

		/* if there is already a born entry for the
		 * node, use it. Otherwise create a born entry
		 * for the node 
		 */
		if(born!=0) {
			OC_EV_SET_BORN(newmbr,j,(born-1));
		} else {
			g_hash_table_insert(private->bornon, 
					GINT_TO_POINTER(uuid),
					GINT_TO_POINTER(trans));
			OC_EV_SET_BORN(newmbr,j,trans);
		}
		j++;
	}
	/* sort the m_arry */
	qsort(OC_EV_GET_NODEARRY(newmbr), n_members, sizeof(oc_node_t), compare);

	in_index = OC_EV_SET_IN_IDX(newmbr,j);
	out_index = OC_EV_SET_OUT_IDX(newmbr,(j+n_nodes));

	OC_EV_SET_N_IN(newmbr,0);
	OC_EV_SET_N_OUT(newmbr,0);

	oldmbr = (mbr_track_t *) cookie_get_data(private->cookie);

	if(oldmbr) {
		for ( i = 0 ; i < n_members; i++ ) {
			if(!already_present(OC_EV_GET_NODEARRY(oldmbr),
						OC_EV_GET_N_MEMBER(oldmbr),
						OC_EV_GET_NODE(newmbr,i))){
				OC_EV_COPY_NODE(newmbr, in_index, newmbr, i);
				in_index++;
				OC_EV_INC_N_IN(newmbr);
				g_hash_table_insert(private->bornon, 
					GINT_TO_POINTER(OC_EV_GET_NODEID(newmbr,i)), 
					GINT_TO_POINTER(trans+1));
			}
		}

		for ( i = 0 ; i < OC_EV_GET_N_MEMBER(oldmbr) ; i++ ) {
			if(!already_present(OC_EV_GET_NODEARRY(newmbr), 
						OC_EV_GET_N_MEMBER(newmbr), 
						OC_EV_GET_NODE(oldmbr,i))){
				OC_EV_COPY_NODE(newmbr, out_index, oldmbr, i);
				out_index++;
				OC_EV_INC_N_OUT(newmbr);
				g_hash_table_remove(private->bornon, 
					GINT_TO_POINTER(OC_EV_GET_NODEID(oldmbr, i)));
				/* remove the born entry for this node */
			}
		}
	} else {
		OC_EV_SET_IN_IDX(newmbr,0);
		OC_EV_SET_N_IN(newmbr,OC_EV_GET_N_MEMBER(newmbr));
	}

	return size;
}

static void
mem_callback_done(void *cookie)
{
	mbr_track_t  *mbr_track =  
		(mbr_track_t *)cookie_get_data(cookie);
	if(mbr_track) {
		if(OC_EV_DEC_COUNT(mbr_track) == 0){
			cookie_destruct(cookie);
			g_free(mbr_track);
		}
	}
	return;
}


static gboolean	 
mem_handle_event(class_t *class)
{
	struct IPC_MESSAGE *msg;
	mbr_private_t *private;
	struct IPC_CHANNEL *ch;
	mbr_track_t *mbr_track, *old_oc_mem;
	int	size;
	oc_memb_event_t type;
	void   *cookie;
	int ret;

	if(!class_valid(class)) return FALSE;

	private = (mbr_private_t *)class->private;
	ch 	   = private->channel;

	if(init_llmborn(private)){
		return FALSE;
	}

	while(ch->ops->is_message_pending(ch)){
		/* receive the message and call the callback*/
		ret=ch->ops->recv(ch,&msg);

		if(ret == IPC_FAIL) {
			return TRUE;
		}

		if(ret!=IPC_OK){
			type = OC_EV_MS_EVICTED;
		} else {
			type = ((ccm_meminfo_t *)msg->msg_body)->ev;
		}
		if(type==OC_EV_MS_NEW_MEMBERSHIP){
			size = get_new_membership(private, 
				(ccm_meminfo_t *)msg->msg_body, 
				msg->msg_len, 
				&mbr_track);

			cookie = cookie_construct(mem_callback_done, 
					mbr_track);

			old_oc_mem = (mbr_track_t *)
				cookie_get_data(private->cookie);
			if(old_oc_mem &&
				(OC_EV_DEC_COUNT(old_oc_mem) == 0)){
				cookie_destruct(private->cookie);
				g_free(old_oc_mem);
			}

			private->cookie = cookie;
			OC_EV_SET_COUNT(mbr_track, 1);
			OC_EV_SET_SIZE(mbr_track, size);
		} else {
			cookie = private->cookie;
			mbr_track = (mbr_track_t *)cookie_get_data(cookie);
			size = OC_EV_GET_SIZE(mbr_track);
		}

		/*call the callback*/
		if(private->callback){
			OC_EV_INC_COUNT(mbr_track);
			if(type==OC_EV_MS_EVICTED) {
				private->callback(type,
					(uint *)cookie,
					0, 
					NULL);
			} else {
				private->callback(type,
					(uint *)cookie,
					size, 
					&(mbr_track->m_mem));
			}
		}

		if(ret==IPC_OK) {
			msg->msg_done(msg);
		} else {
			return FALSE;
		}
	}
	return TRUE;
}


static int	 
mem_activate(class_t *class)
{
	mbr_private_t *private;
	struct IPC_CHANNEL *ch;
	int sockfd;

	if(!class_valid(class)) return -1;

	/* if already activated */

	private = (mbr_private_t *)class->private;
	if(private->llm)return -1;

	ch 	   = private->channel;

	if(!ch || ch->ops->initiate_connection(ch) != IPC_OK) {
		return -1;
	}

	sockfd = ch->ops->get_recv_select_fd(ch);

	return sockfd;
}


static void
mem_unregister(class_t *class)
{
	mbr_private_t  *private;
	struct IPC_CHANNEL *ch;

	private = (mbr_private_t *)class->private;
	ch 	   = private->channel;

	/* TOBEDONE
	 * call all instances, of message done
	 * on channel ch.
	 */

	ch->ops->destroy(ch);

	g_free(private->llm);
	g_free(private);
	g_free(class);
}


static oc_ev_callback_t *
mem_set_callback(class_t *class, oc_ev_callback_t f)
{
	mbr_private_t 	*private;
	oc_ev_callback_t *ret_f;

	if(!class_valid(class)) return NULL;

	private = (mbr_private_t *)class->private;
	
	ret_f = private->callback;
	private->callback = f;

	return ret_f;
}

static gboolean
mem_is_my_nodeid(class_t *class, const oc_node_t *node)
{
	mbr_private_t 	*private;

	if(!class_valid(class)) return FALSE;

	private = (mbr_private_t *)class->private;

	if (node->node_id == CLLM_GET_MYUUID(private->llm))
		return TRUE;

	return FALSE;
}


class_t *
oc_ev_memb_class(oc_ev_callback_t  *fn)
{
	mbr_private_t 	*private;
	class_t *memclass;
	struct IPC_CHANNEL *ch;
	GHashTable * attrs;
	static char 	path[] = PATH_ATTR;
	static char 	ccmfifo[] = CCMFIFO;

	memclass = g_malloc(sizeof(class_t));

	if (!memclass) return NULL;

	private = (mbr_private_t *)g_malloc(sizeof(mbr_private_t));
	if (!private) {
		g_free(memclass);
		return NULL;
	}

	memclass->type = OC_EV_MEMB_CLASS;
	memclass->set_callback  =  mem_set_callback;
	memclass->handle_event  =  mem_handle_event;
	memclass->activate = mem_activate;
	memclass->unregister = mem_unregister;
	memclass->is_my_nodeid = mem_is_my_nodeid;

	memclass->private = (void *)private;
	private->callback = fn;
	private->magiccookie = 0xabcdef;

	attrs = g_hash_table_new(g_str_hash,g_str_equal);
	g_hash_table_insert(attrs, path, ccmfifo);
	ch = ipc_channel_constructor(IPC_DOMAIN_SOCKET, attrs);
	g_hash_table_destroy(attrs);

	if(!ch) {
		g_free(memclass);
		g_free(private);
		return NULL;
	}

	private->channel = ch;

	return memclass;
}
