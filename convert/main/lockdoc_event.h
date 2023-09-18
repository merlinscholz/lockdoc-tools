#ifndef __LOG_EVENT_H__
#define __LOG_EVENT_H__

#define LOG_CHAR_BUFFER_LEN 100
#define PSEUDOLOCK_ADDR_RCU		0x42
#define PSEUDOLOCK_ADDR_PREEMPT	0x43
#define PSEUDOLOCK_ADDR_SOFTIRQ	0x45
#define PSEUDOLOCK_ADDR_HARDIRQ	0x44
#define PSEUDOLOCK_NAME_RCU		"rcu"
#define PSEUDOLOCK_NAME_HARDIRQ	"hardirq"
#define PSEUDOLOCK_NAME_SOFTIRQ	"softirq"
#define PSEUDOLOCK_NAME_PREEMPT	"preempt"
#define PSEUDOLOCK_VAR			"static"
#define PSEUDOLOCK_VAR_RCU		PSEUDOLOCK_VAR
#define PSEUDOLOCK_VAR_HARDIRQ	PSEUDOLOCK_VAR
#define PSEUDOLOCK_VAR_SOFTIRQ	PSEUDOLOCK_VAR
#define PSEUDOLOCK_VAR_PREEMPT	PSEUDOLOCK_VAR

// Lock flags
#define LOCK_FLAGS_RECURSIVE	0x1

/*
 * When altered, this file
 * has to be copied to the according fail experiment
 * as well as to convert!
 */
enum LOCKDOC_OP {
	LOCKDOC_ALLOC		= 'a',
	LOCKDOC_FREE		= 'f',
	LOCKDOC_LOCK_OP		= 'l',
	LOCKDOC_CURRENT_TASK	= 't',
	LOCKDOC_PREEMPT_COUNT	= 'p',
	LOCKDOC_PID_OFFSET	= 'o',
	LOCKDOC_KERNEL_VERSION	= 'v',
	LOCKDOC_READ		= 'r',
	LOCKDOC_WRITE		= 'w'
};

enum LOCK_OP {
	P_READ = 0,
	P_WRITE,
	V_READ,
	V_WRITE
};

struct log_action {
	enum LOCKDOC_OP action;
	int32_t ctx;
	uint32_t lock_op;
	uint32_t ptr;
	uint32_t size;
	char type[LOG_CHAR_BUFFER_LEN]; // allocated data_type or lock type
	char lock_member[LOG_CHAR_BUFFER_LEN];
	char file[LOG_CHAR_BUFFER_LEN];
	int32_t line;
	int32_t flags;
}__attribute__((packed));

#endif /* __LOG_EVENT_H__ */
