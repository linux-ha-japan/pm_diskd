#define UWORD32 unsigned long

#define SHA_DIGESTSIZE  20
#define SHA_BLOCKSIZE   64

typedef struct SHA1Context_st{
	    UWORD32 state[5];
	    UWORD32 count[2];
	    unsigned char buffer[64];
} SHA1_CTX;

