/*
 * netstring implementation 
 *
 * Copyright (c) 2003 Guochun Shi <gshi@ncsa.uiuc.edu>
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <heartbeat.h>
#include <ha_msg.h>
#include <hb_proc.h>
#include <unistd.h>
#include <clplumbing/ipc.h>
#include <clplumbing/netstring.h>
#include <clplumbing/base64.h>
#include <portability.h>


extern const char*	FT_strings[];

int 
intlen(int x)
{
	char	buf[MAXLINE];
	
	memset(buf, 0, MAXLINE);
	sprintf(buf, "%d", x);
	
	return(strlen(buf));
	
}

static int 
compose_netstring(char* s, const char* smax, int len, 
		  const char* data, int* comlen)
{
	
	char *	sp = s; 

	if( s + len + 3 > smax){ /*3 == ":" + "," + at least one digit number */
		ha_log(LOG_ERR, "netstring pointer out of boundary(compose_netstring)");
		return(HA_FAIL);
	}
	
	sp += sprintf(sp, "%d:",len );
	
	memcpy(sp, data, len);
	sp += len;
	*sp++ = ',';
	
	*comlen = sp - s;
	
	return(HA_OK);
}



/* Converts a message into a netstriong*/


static int
msg2netstring_buf(const struct ha_msg *m, char *s, 
		  size_t len, size_t * slen){ 

	int	i; 
	char*	sp;
	char*	smax;
	
	char*	datap;
	int	datalen = 0;
	char	authtoken[MAXLINE];
	char	authstring[MAXLINE];
	
	sp = s;
	smax = s + len;
	
	strcpy(sp, MSG_START_NETSTRING);
	
	sp += strlen(MSG_START_NETSTRING);
	
	datap = sp;
	
	for(i=0; i < m->nfields; i++)
		{
			int comlen;
			int len;
			
			if( compose_netstring(sp, smax, m->nlens[i], m->names[i],&comlen) != HA_OK){
				ha_log(LOG_ERR, "compose_netstring fails for name(msg2netstring_buf)");
				return(HA_FAIL);
			}
			
			sp += comlen;
			datalen +=comlen;
			
			
			if( compose_netstring(sp, smax, 1, FT_strings[m->types[i]],&comlen) != HA_OK){
				ha_log(LOG_ERR, "compose_netstring fails for type(msg2netstring_buf)");
				return(HA_FAIL);
			}
			
			sp += comlen;
			datalen +=comlen;
			
			len = m->nlens[i];
			if(m->types[i] == FT_STRUCT){
				int	tmplen;
				char	*sp_save = sp;
				
				len =  get_netstringlen((struct ha_msg *)m->values[i], 0);
				sp += sprintf(sp, "%d:",len);
				
				if (msg2netstring_buf((struct ha_msg * )m->values[i], sp, len, &tmplen) != HA_OK){
					ha_log(LOG_ERR, "msg2netstring_buf(): msg2netstring_buf() failed");
					return(HA_FAIL);
				}
				
				sp +=len;
				
				*sp++ = ',';				
				comlen = sp - sp_save;
				datalen += comlen;
				
			} else if( compose_netstring(sp, smax, m->vlens[i], m->values[i],&comlen) != HA_OK){
				ha_log(LOG_ERR, "compose_netstring fails for value(msg2netstring_buf)");
				return(HA_FAIL);
			} else{			
				sp += comlen;
				datalen +=comlen;
			}
		}
	
	
	/*add authentication*/
	check_auth_change(config);
	if(config){
		if (!config->authmethod->auth->auth(config->authmethod, datap
						    ,   datalen
						    ,   authtoken, DIMOF(authtoken))) {
			ha_log(LOG_ERR 
			       ,        "Cannot compute message authentication [%s/%s/%s]"
			       ,        config->authmethod->authname
			       ,        config->authmethod->key
			       ,        datap);
			return(HA_FAIL);
		}
		
		sprintf(authstring, "%d %s", config->authnum, authtoken);
		sp += sprintf(sp, "%d:%s,",strlen(authstring), authstring);     
	}



	strcpy(sp, MSG_END_NETSTRING);
	sp += sizeof(MSG_END_NETSTRING) -1;
	
	if(sp > smax){
		ha_log(LOG_ERR, "msg2netstring: exceed memory boundary sp =%p smax=%p", sp, smax);
		return(HA_FAIL);
	}
	
	*slen = sp - s + 1;		
	return(HA_OK);
	
}

char* 
msg2netstring(const struct ha_msg *m, size_t * slen)
{
	
	int	len;
	void	*s;
	
	len= get_netstringlen(m, 0) + 1;
	s = ha_calloc(1, len);
	if(!s){
		ha_log(LOG_ERR, "msg2netstring: no memory for netstring");
		return(NULL);
	}	
	
	if(msg2netstring_buf(m, s, len, slen) != HA_OK){
		ha_log(LOG_ERR, "msg2netstring: msg2netstring_buf() failed");
		ha_free(s);
		return(NULL);
	}
	
	return(s);
}


/*
 * peel one string off in a netstring
 *
 */

static int 
peel_netstring(const char* s, const char* smax, int* len,
	       const char** data, int* parselen )
{
	const char*	sp = s;	
	
	if(sp >= smax){
		return(HA_FAIL);
	}
	
	sscanf(sp,"%d", len);

	if(len <= 0){
		return(HA_FAIL);
	}
	
	while(*sp != ':' && sp < smax) {
		sp ++;
	}
	
	if(sp >= smax ){
		return(HA_FAIL);
	}
	
	sp ++;		

	*data = sp;
	
	sp += (*len);
	if( *sp != ','){
		return(HA_FAIL);
	}
	sp++;
	
	*parselen = sp - s;
	
	return(HA_OK);
}

/* Converts a netstring into a message*/
struct ha_msg *
netstring2msg(const char *s, size_t length, int need_auth)
{
	struct ha_msg*	ret = NULL;
	const char*	sp = s;
	const char*	smax = s + length;
	int		startlen;
	int		endlen;
	const char*	datap;
	int		datalen = 0;
	
	if((ret = ha_msg_new(0)) == NULL){
		return(NULL);
	}
	
	startlen = sizeof(MSG_START_NETSTRING)-1;
	if (strncmp(sp, MSG_START_NETSTRING, startlen) != 0) {
		/* This can happen if the sender gets killed */
		/* at just the wrong time... */
		ha_log(LOG_WARNING, "netstring2msg: no MSG_START");
		return(NULL);
	}else{
		sp += startlen;
	}
	
	endlen = sizeof(MSG_END_NETSTRING) - 1;
	
	datap = sp;	
	while( sp < smax && strncmp(sp, MSG_END_NETSTRING, endlen) !=0  ){
		int nlen;
		int vlen;
		const char* name;
		const char* value;
		int parselen;
		int tmp;
		
		int tlen;
		const char* type;
		
		tmp = datalen;
		if(peel_netstring( sp , smax, &nlen, &name,&parselen) != HA_OK){
			ha_log(LOG_ERR, "peel_netstring fails for name(netstring2msg)");
			ha_msg_del(ret);
			return(NULL);
		}
		sp +=  parselen;
		datalen += parselen;
		
		if(strncmp(sp, MSG_END_NETSTRING, endlen) ==0){
			if(config){
				if(!is_auth_netstring(datap, tmp, name,nlen) ){
					ha_log(LOG_ERR, "netstring authentication failed, s=%s, autotoken=%s, sp=%s", s,name, sp);
					ha_log_message(ret);
					ha_msg_del(ret);
					return(NULL);
				}
			}
			return(ret);
		}
		

		if(peel_netstring( sp , smax, &tlen, &type, &parselen) != HA_OK){
			
			ha_log(LOG_ERR, "peel_netstring() error in netstring2msg for type");
			ha_msg_del(ret);
			return(NULL);
		}
		sp +=  parselen;
		datalen += parselen;
		

		if(peel_netstring( sp , smax, &vlen, &value, &parselen) != HA_OK){
			
			ha_log(LOG_ERR, "peel_netstring() error in netstring2msg for value");
			ha_msg_del(ret);
			return(NULL);
		}
		sp +=  parselen;
		datalen += parselen;
		
		if(atoi(type) == FT_STRUCT){
			struct ha_msg	*tmpmsg;
			
			tmpmsg = netstring2msg(value, vlen, 1);
			value = (char*)tmpmsg;
			vlen = sizeof(struct ha_msg);
		}
		
		if (ha_msg_nadd_type(ret, name, nlen, value, vlen, atoi(type)) != HA_OK){
			ha_log(LOG_ERR, "ha_msg_nadd fails(netstring2msg)");
			ha_msg_del(ret);
			return(NULL);
		}

	}
	
	if(!need_auth){
		return(ret);
	}else {
		ha_log(LOG_ERR, "no authentication found in netstring");
		ha_msg_del(ret);
		return(NULL);
	}
	
	
}


int 
is_auth_netstring(const char* datap, size_t datalen, 
		  const char* authstring, size_t authlen){
	
	char	authstr[MAXLINE];
	int	authwhich;
	char	authtoken[MAXLINE];
	struct HBauth_info *	which;
	char	authbuf[MAXLINE];
	
	check_auth_change(config);
	
	strncpy(authstr, authstring, authlen);
	authstr[authlen] = 0;
	if(sscanf(authstr, "%d %s", &authwhich, authtoken) != 2) {
		ha_log(LOG_WARNING, "Bad/invalid auth string");
		return(0);
	}
	
	which = config->auth_config + authwhich;
	
	
	if (authwhich < 0 || authwhich >= MAXAUTH || which->auth == NULL) {
		ha_log(LOG_WARNING
		,	"Invalid authentication type [%d] in message!"
		,	authwhich);
		return(0);
	}
	
	if (!which->auth->auth(which
			       ,	datap, datalen
			       ,	authbuf, DIMOF(authbuf))) {
		ha_log(LOG_ERR, "Failed to compute message authentication");
		return(0);
	}

	if (strcmp(authtoken, authbuf) == 0) {
		return(1);
	}
	
	ha_log(LOG_ERR,"authtoken not match, authtoken=%s, authbuf=%s", authtoken, authbuf);
	return(0);
	
	
}

