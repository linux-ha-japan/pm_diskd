
/*
 * auth.c: Authentication code for heartbeat
 *
 * Copyright (C) 1999,2000 Mitja Sarp <mitja@lysator.liu.se>
 *	Somewhat mangled by Alan Robertson <alanr@unix.sh>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include "heartbeat.h"
#include "auth.h"
unsigned char result[MAXLINE];

const unsigned char *	calc_hmac_md5	(const struct auth_info *, const char * text);
const unsigned char *	calc_hmac_sha1	(const struct auth_info *, const char * text);
const unsigned char *	calc_crc	(const struct auth_info *, const char * text);

struct auth_type ValidAuths[] = {
	{"md5", 	calc_hmac_md5,	1},
	{"sha1",	calc_hmac_sha1,	1},
	{"crc",		calc_crc,	0},
};

struct auth_type *	findauth(const char * type)
{
	int	j;
	for (j=0; j < DIMOF(ValidAuths); ++j) {
		if (strcmp(type, ValidAuths[j].authname) == 0) {
			return (ValidAuths+j);
		}
	}
	return(NULL);
}

const unsigned char *
calc_hmac_md5(const struct auth_info *t, const char * text)
{
	MD5Context context;
	unsigned char digest[MD5_DIGESTSIZE];
	const char * key = t->key;
	/* inner padding - key XORd with ipad */
	unsigned char k_ipad[65];    
	/* outer padding - * key XORd with opad */
	unsigned char k_opad[65];    
	unsigned char tk[MD5_DIGESTSIZE];
	int i, text_len, key_len;

	text_len = strlen(text);
	key_len = strlen(key);
	
	/* if key is longer than MD5_BLOCKSIZE bytes reset it to key=MD5(key) */
	if (key_len > MD5_BLOCKSIZE) { 
		MD5Context      tctx;   
		MD5Init(&tctx);
		MD5Update(&tctx, key, key_len);
		MD5Final(tk, &tctx); 

		key = tk;
		key_len = MD5_DIGESTSIZE;
	}       
	/* start out by storing key in pads */
	bzero(k_ipad, sizeof k_ipad);
	bzero(k_opad, sizeof k_opad);
	bcopy(key, k_ipad, key_len);
	bcopy(key, k_opad, key_len);

	/* XOR key with ipad and opad values */
	for (i=0; i<MD5_BLOCKSIZE; i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}       
	/* perform inner MD5 */
	MD5Init(&context);                   /* init context for 1st pass */
	MD5Update(&context, k_ipad, MD5_BLOCKSIZE);     /* start with inner pad */
	MD5Update(&context, text, text_len); /* then text of datagram */
	MD5Final(digest, &context);          /* finish up 1st pass */
	/* perform outer MD5 */
	MD5Init(&context);                   /* init context for 2nd pass */
	MD5Update(&context, k_opad, MD5_BLOCKSIZE);     /* start with outer pad */
	MD5Update(&context, digest, MD5_DIGESTSIZE);     /* then results of 1st hash */

	MD5Final(digest, &context);          /* finish up 2nd pass */
	/* And show the result in human-readable form */
	result[0] = '\0';
	for (i = 0; i < MD5_DIGESTSIZE; i++) {
		sprintf(tk, "%02x", digest[i]);
		strcat(result, tk);
	}
	return(result);
}

const unsigned char *
calc_hmac_sha1	(const struct auth_info *info, const char * text)
{
	SHA1_CTX ictx, octx ;
	unsigned char   isha[SHA_DIGESTSIZE]; 
	unsigned char 	osha[SHA_DIGESTSIZE];
	unsigned char   tk[SHA_DIGESTSIZE];
	unsigned char   buf[SHA_BLOCKSIZE];
	int	i, text_len, key_len;
	const char * key = info->key;

	text_len = strlen(text);
	key_len = strlen(key);

	if (key_len > SHA_BLOCKSIZE) {
		SHA1_CTX         tctx ;
		SHA1Init(&tctx);
		SHA1Update(&tctx, key, key_len);
		SHA1Final(key, &tctx);
		key = tk;
		key_len = SHA_DIGESTSIZE;
	}

	/**** Inner Digest ****/

	SHA1Init(&ictx) ;

	/* Pad the key for inner digest */
	for (i = 0 ; i < key_len ; ++i) buf[i] = key[i] ^ 0x36 ;
	for (i = key_len ; i < SHA_BLOCKSIZE ; ++i) buf[i] = 0x36 ;

	SHA1Update(&ictx, buf, SHA_BLOCKSIZE) ;
	SHA1Update(&ictx, text, text_len) ;

	SHA1Final(isha, &ictx) ;

	/**** Outter Digest ****/

	SHA1Init(&octx) ;

	/* Pad the key for outter digest */

	for (i = 0 ; i < key_len ; ++i) buf[i] = key[i] ^ 0x5C ;
	for (i = key_len ; i < SHA_BLOCKSIZE ; ++i) buf[i] = 0x5C ;

	SHA1Update(&octx, buf, SHA_BLOCKSIZE) ;
	SHA1Update(&octx, isha, SHA_DIGESTSIZE) ;
	SHA1Final(osha, &octx) ;

	result[0] = '\0';
	for (i = 0; i < SHA_DIGESTSIZE; i++) {
		sprintf(tk, "%02x", osha[i]);
		strcat(result, tk);
	}

	return(result);
}

const unsigned char *
calc_crc (const struct auth_info *info, const char * value)
{
	unsigned long crc = 0;
	int length=strlen(value);
	(void)info;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;
	while(length--)
		crc = (crc << 8) ^ crctab[((crc >> 24) ^ *(value++)) & 0xFF];

	crc = ~crc & 0xFFFFFFFFul;
	sprintf(result, "%lx", crc);
	return(result);
}
