/* Minimal mainline compatibility for the vendor Android wakelock API */
#ifndef _DBMDX_WAKELOCK_H
#define _DBMDX_WAKELOCK_H
#include <linux/types.h>
#define WAKE_LOCK_SUSPEND 0
struct wake_lock { int dummy; };
static inline void wake_lock_init(struct wake_lock *l, int type, const char *name) { }
static inline void wake_lock_destroy(struct wake_lock *l) { }
static inline void wake_lock(struct wake_lock *l) { }
static inline void wake_unlock(struct wake_lock *l) { }
static inline void wake_lock_timeout(struct wake_lock *l, long timeout) { }
static inline int wake_lock_active(struct wake_lock *l) { return 0; }
#endif
