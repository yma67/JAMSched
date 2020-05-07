#include <signal.h>
#define	setcontext(u)	_setmcontext(&(u)->mc)
#define	getcontext(u)	_getmcontext(&(u)->mc)
typedef struct jam_mcontext jam_mcontext_t;
typedef struct jam_ucontext jam_ucontext_t;
struct jam_mcontext
{
	unsigned long	pc;		/* lr */
	unsigned long	cr;		/* mfcr */
	unsigned long	ctr;		/* mfcr */
	unsigned long	xer;		/* mfcr */
	unsigned long	sp;		/* callee saved: r1 */
	unsigned long	toc;		/* callee saved: r2 */
	unsigned long	r3;		/* first arg to function, return register: r3 */
	unsigned long	gpr[19];	/* callee saved: r13-r31 */
/*
// XXX: currently do not save vector registers or floating-point state
//	ulong	pad;
//	uvlong	fpr[18];	/ * callee saved: f14-f31 * /
//	ulong	vr[4*12];	/ * callee saved: v20-v31, 256-bits each * /
*/
};

struct jam_ucontext
{
	struct {
		void *ss_sp;
		unsigned int ss_size;
	} uc_stack;
	sigset_t uc_sigmask;
	jam_mcontext_t mc;
};

void makecontext(jam_ucontext_t*, void(*)(void), int, ...);
int swapcontext(jam_ucontext_t*, const jam_ucontext_t*);
int _getmcontext(jam_mcontext_t*);
void _setmcontext(const jam_mcontext_t*);

