#include <stdlib.h>
struct TestParms {
	int	enable_send_pkt_loss;
	int	enable_rcv_pkt_loss;
	float	send_loss_prob;
	float	rcv_loss_prob;
};

struct TestParms *	TestOpts;

#define	TESTSEND	(TestOpts && TestOpts->enable_send_pkt_loss)
#define	TESTRCV		(TestOpts && TestOpts->enable_rcv_pkt_loss)

#define RandThresh(p) (rand() <= (int)((((float)RAND_MAX) * (p)) + 0.5))

#define TestRand(field)	(TestOpts && RandThresh(TestOpts->field))
void ParseTestOpts(void);
