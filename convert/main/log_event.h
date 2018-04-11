#ifndef __LOG_EVENT_H__
#define __LOG_EVENT_H__

#define LOG_CHAR_BUFFER_LEN 60
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

/*
 * When altered, this file
 * has to be copied to the according fail experiment
 * as well as to convert!
 */

enum LOCK_OP {
	P_READ = 0,
	P_WRITE,
	V_READ,
	V_WRITE
};

enum IRQ_SYNC {
	LOCK_NONE = 0,
	LOCK_IRQ,
	LOCK_IRQ_NESTED,  /* aka irqsave and irqrestore */
	LOCK_BH
};

struct log_action {
	char action;
	uint32_t lock_op;
	uint32_t ptr;
	uint32_t size;
	char type[LOG_CHAR_BUFFER_LEN]; // allocated data_type or lock type
	char lock_member[LOG_CHAR_BUFFER_LEN];
	char file[LOG_CHAR_BUFFER_LEN];
	int32_t line;
	char function[LOG_CHAR_BUFFER_LEN]; 
	int32_t preempt_count;
	int32_t irq_sync;
}__attribute__((packed));

#endif /* __LOG_EVENT_H__ */
