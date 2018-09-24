library("ggplot2")

nostack <- read.csv("all_txns_members_locks_hypo_winner_nostack.csv",sep=";")
stack_instrptr <- read.csv("instrptr/all_txns_members_locks_hypo_winner_stack.csv",sep=";")
stack_txn <- read.csv("txn/all_txns_members_locks_hypo_winner_stack.csv",sep=";")
data <- data.frame(confidence=numeric(length(nostack$confidence) + length(stack_instrptr$confidence) + length(stack_txn$confidence)), variant=character(length(nostack$confidence) + length(stack_instrptr$confidence) + length(stack_txn$confidence)))
temp <- vector()
temp<-c(temp,rep("nostack",length(nostack$confidence)))
temp<-c(temp,rep("stack_instrptr",length(stack_instrptr$confidence)))
temp<-c(temp,rep("stack_txn",length(stack_txn$confidence)))
data$variant <- temp
data[data$variant == "nostack",]$confidence <- data.frame(nostack$confidence)$nostack.confidence
data[data$variant == "stack_instrptr",]$confidence <- data.frame(stack_instrptr$confidence)$stack_instrptr
data[data$variant == "stack_txn",]$confidence <- data.frame(stack_txn$confidence)$stack_txn
ggplot() + geom_boxplot(data=data,aes(y=confidence,x=variant))