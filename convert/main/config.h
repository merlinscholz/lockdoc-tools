#ifndef __CONFIG_H__
#define __CONFIG_H__

#define ELF_SECTIONS {".bss", ".data", ".data.cacheline_aligned", ".data.read_mostly"}
#define DELIMITER_CHAR ';'
#define DELIMITER_BLACKLISTS ';'
#define MAX_COLUMNS 14
#define LOOK_BEHIND_WINDOW 2
#define SKIP_EMPTY_TXNS 1
//#define VERBOSE
//#define DEBUG_DATASTRUCTURE_GROWTH
#define DELIMITER_MSG_ERROR	":"
#define DELIMITER_MSG_DEBUG	":"
#define DELIMITER_SUBCLASS	":"
#define DUMMY_EXECUTION_CONTEXT 0

// The PRINT_* macros are a bit ugly... At least they ensure a consistent format.
// However, they induce less overhead than a dedicated logger class which performs runtime checks.
#define PRINT_ERROR(ctx, msg) cerr << "error" << DELIMITER_MSG_ERROR << __func__ << DELIMITER_MSG_ERROR << dec << __LINE__ << DELIMITER_MSG_ERROR << ctx << DELIMITER_MSG_ERROR << msg << endl;

#ifdef VERBOSE
#define PRINT_DEBUG(ctx,msg) cout << "debug" << DELIMITER_MSG_DEBUG << __func__ << DELIMITER_MSG_DEBUG << dec << __LINE__ << ctx << DELIMITER_MSG_DEBUG << msg << endl;
#else
#define PRINT_DEBUG(ctx,msg) 
#endif

extern char delimiter;

#endif // __CONFIG_H__
