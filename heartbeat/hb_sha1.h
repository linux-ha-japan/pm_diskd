/*
 * Copyright (C) 2000 Mitja Sarp.
 * This software licensed under the GNU LGPL.
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
#define UWORD32 unsigned long

#define SHA_DIGESTSIZE  20
#define SHA_BLOCKSIZE   64

typedef struct SHA1Context_st{
	    UWORD32 state[5];
	    UWORD32 count[2];
	    unsigned char buffer[64];
} SHA1_CTX;

