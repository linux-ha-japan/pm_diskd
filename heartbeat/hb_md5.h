#define UWORD32 unsigned long

#define MD5_DIGESTSIZE  16
#define MD5_BLOCKSIZE   64
typedef struct MD5Context_st 
{
	UWORD32 buf[4];
	UWORD32 bytes[2];
	UWORD32 in[16];
} MD5Context;

