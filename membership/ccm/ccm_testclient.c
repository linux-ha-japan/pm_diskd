/* 
 * ccm.c: A consensus cluster membership sample client
 *
 * Copyright (c) International Business Machines  Corp., 2000
 * Author: Ram Pai (linuxram@us.ibm.com)
 * 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <oc_event.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


oc_ev_t *ev_token;


static void 
my_ms_events(oc_ed_t event, void *cookie, 
		size_t size, const void *data)
{
	const oc_ev_membership_t *oc = (const oc_ev_membership_t *)data;
	int i;
	int i_am_in;

 	fprintf(stderr,"\nevent=%s\n", 
			event==OC_EV_MS_NEW_MEMBERSHIP?"NEW MEMBERSHIP":
		        event==OC_EV_MS_NOT_PRIMARY?"NOT PRIMARY":
			event==OC_EV_MS_PRIMARY_RESTORED?"PRIMARY RESTORED":
			      "EVICTED");

	if(OC_EV_MS_EVICTED == event) {
		oc_ev_callback_done(cookie);
		return;
	}

	fprintf(stderr,"trans=%d, nodes=%d, new=%d, lost=%d n_idx=%d, "
				"new_idx=%d, old_idx=%d\n",
			oc->m_instance,
			oc->m_n_member,
			oc->m_n_in,
			oc->m_n_out,
			oc->m_memb_idx,
			oc->m_in_idx,
			oc->m_out_idx);

	i_am_in=0;
	fprintf(stderr, "\nNODES IN THE PRIMARY MEMBERSHIP\n");
	for(i=0; i<oc->m_n_member; i++) {
		fprintf(stderr,"\tnodeid=%d, born=%d\n",
			oc->m_array[oc->m_memb_idx+i].node_id,
			oc->m_array[oc->m_memb_idx+i].node_born_on);
		if(oc_ev_is_my_nodeid(ev_token, &(oc->m_array[i]))){
			i_am_in=1;
		}
	}
	if(i_am_in) {
		fprintf(stderr,"\nMY NODE IS A MEMBER OF THE MEMBERSHIP LIST\n\n");
	}

	fprintf(stderr, "NEW MEMBERS\n");
	if(oc->m_n_in==0) 
		fprintf(stderr, "\tNONE\n");
	for(i=0; i<oc->m_n_in; i++) {
		fprintf(stderr,"\tnodeid=%d, born=%d\n",
			oc->m_array[oc->m_in_idx+i].node_id,
			oc->m_array[oc->m_in_idx+i].node_born_on);
	}
	fprintf(stderr, "\nMEMBERS LOST\n");
	if(oc->m_n_out==0) 
		fprintf(stderr, "\tNONE\n");
	for(i=0; i<oc->m_n_out; i++) {
		fprintf(stderr,"\tnodeid=%d, born=%d\n",
			oc->m_array[oc->m_out_idx+i].node_id,
			oc->m_array[oc->m_out_idx+i].node_born_on);
	}
	fprintf(stderr, "-----------------------\n");
	oc_ev_callback_done(cookie);
}



int
main(int argc, char *argv[])
{
	int ret;
	fd_set rset;
	int	my_ev_fd;

	oc_ev_register(&ev_token);

	oc_ev_set_callback(ev_token, OC_EV_MEMB_CLASS, my_ms_events, NULL);

	ret = oc_ev_activate(ev_token, &my_ev_fd);
	if(ret){
		oc_ev_unregister(ev_token);
		return(1);
	}

	for (;;) {

		FD_ZERO(&rset);
		FD_SET(my_ev_fd,&rset);

		if(select(my_ev_fd + 1, &rset, NULL,NULL,NULL) == -1){
			perror("select");
			return(1);
		}
		oc_ev_handle_event(ev_token);
	}
	return 0;
}
