#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DELIMITER_CHAR ';'
#define DELIMITER_BLACKLISTS ';'
#define NEED_BSS_DATA
#define MAX_COLUMNS 14
#define LOOK_BEHIND_WINDOW 2
#define SKIP_EMPTY_TXNS 1
//#define VERBOSE
//#define DEBUG_DATASTRUCTURE_GROWTH
#define PRINT_CONTEXT " (action=" << action << ",type=" << typeStr << ",ts=" << dec << ts << ")"

extern char delimiter;

#endif // __CONFIG_H__
