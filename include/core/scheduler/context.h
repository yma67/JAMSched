#ifndef CONTEXT_H
#define CONTEXT_H
#if defined(__sun__)
#	define __EXTENSIONS__ 1 /* SunOS */
#	if defined(__SunOS5_6__) || defined(__SunOS5_7__) || defined(__SunOS5_8__)
		/* NOT USING #define __MAKECONTEXT_V2_SOURCE 1 / * SunOS */
#	else
#		define __MAKECONTEXT_V2_SOURCE 1
#	endif
#endif

#define USE_UCONTEXT 1

#if defined(__OpenBSD__) || defined(__mips__) || defined(__x86_64__)
#undef USE_UCONTEXT
#define USE_UCONTEXT 0
#endif

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#if defined(MAC_OS_X_VERSION_10_5)
#undef USE_UCONTEXT
#define USE_UCONTEXT 0
#endif
#endif

#if USE_UCONTEXT
#include <ucontext.h>
typedef mcontext_t jam_mcontext_t;
typedef ucontext_t jam_ucontext_t;
#endif

#if defined(__FreeBSD__) && __FreeBSD__ < 5
extern	int		getmcontext(jam_mcontext_t*);
extern	void		setmcontext(const jam_mcontext_t*);
#define	setcontext(u)	setmcontext(&(u)->uc_mcontext)
#define	getcontext(u)	getmcontext(&(u)->uc_mcontext)
extern	int		swapcontext(jam_ucontext_t*, const jam_ucontext_t*);
extern	void		makecontext(jam_ucontext_t*, void(*)(), int, ...);
#endif

#if defined(__APPLE__)
#	define jam_mcontext libthread_mcontext
#	define jam_mcontext_t libthread_mcontext_t
#	define jam_ucontext libthread_ucontext
#	define jam_ucontext_t libthread_ucontext_t
#	if defined(__i386__)
#		include "core/ucontext/386-ucontext.h"
#	elif defined(__x86_64__)
#		include "core/ucontext/amd64-ucontext.h"
#	else
#		include "core/ucontext/power-ucontext.h"
#	endif	
#endif

#if defined(__OpenBSD__)
#	define jam_mcontext libthread_mcontext
#	define jam_mcontext_t libthread_mcontext_t
#	define jam_ucontext libthread_ucontext
#	define jam_ucontext_t libthread_ucontext_t
#	if defined __i386__
#		include "386-ucontext.h"
#	else
#		include "power-ucontext.h"
#	endif
extern pid_t rfork_thread(int, void*, int(*)(void*), void*);
#endif

#if 0 &&  defined(__sun__)
#	define jam_mcontext libthread_mcontext
#	define jam_mcontext_t libthread_mcontext_t
#	define jam_ucontext libthread_ucontext
#	define jam_ucontext_t libthread_ucontext_t
#	include "sparc-ucontext.h"
#endif

#if defined(__arm__)
int getmcontext(jam_mcontext_t*);
void setmcontext(const jam_mcontext_t*);
#define	setcontext(u)	setmcontext(&(u)->uc_mcontext)
#define	getcontext(u)	getmcontext(&(u)->uc_mcontext)
#endif

#if defined(__mips__)
#include "mips-ucontext.h"
int getmcontext(jam_mcontext_t*);
void setmcontext(const jam_mcontext_t*);
#define	setcontext(u)	setmcontext(&(u)->uc_mcontext)
#define	getcontext(u)	getmcontext(&(u)->uc_mcontext)
#endif
#endif

#if defined(__linux__) && defined(__x86_64__)
#include "core/ucontext/amd64-ucontext.h"
int getmcontext(jam_mcontext_t*);
void setmcontext(const jam_mcontext_t*);
#define	setcontext(u)	setmcontext(&(u)->uc_mcontext)
#define	getcontext(u)	getmcontext(&(u)->uc_mcontext)
#endif