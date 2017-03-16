library("ggplot2")
library("reshape2")
library("getopt")

# Author: Alexander Lochmann, 2017
# This script creates two types of plots:
# First, it plots the distribution of percentages for the winner hypothesis for each member.
# Second, it plots the development of accepted hypotheses (percentaged) by cut-off threshold. This plot my contain every data type found in the input data.
# The optional argument -t my restrict the data to a certain data type.

mySavePlot <- function(plot, name, directory=NULL) {
  
  if (is.null(directory)) {
    fname = sprintf("%s.pdf",name)
  } else {
    if (!file.exists(directory)) {
      cat(sprintf("Creating directory %s ...\n",directory))
      dir.create(directory)
    }
    fname = sprintf("%s/%s.pdf",directory,name)
  }
  
  cat(sprintf("Creating: %s\n",fname))
  ggsave(file=fname,plot,units="cm")
}

# Parameters for the development of accepted hypotheses plot
startThreshold=0
stepSize=5
maxThreshold=100
numSteps=21

args <- commandArgs(trailingOnly=T)

spec = matrix(c(
  'inputfile', 'i', 1, 'character',
  'type'   , 't', 1, 'character',
  'directory'   , 'd', 1, 'character'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$inputfile)) {
  inputfname = "all_txns_members_locks_hypo_winner.csv"
} else {
  inputfname = opt$inputfile
}

if (is.null(opt$type)) {
  typeFilter = NULL
} else {
  typeFilter = opt$type
}

if (is.null(opt$directory)) {
  directory = NULL
} else {
  directory = opt$directory
}

raw <- read.csv(inputfname,sep=";")
# Filter out the task_struct since it does not belong to the observed fs subsystem.
raw <- raw[raw$type != 'task_struct',]
steps <- seq(from=startThreshold,to=maxThreshold,by=stepSize)

if (is.null(typeFilter)) {
  # Extract all unique data types in input data
  types <- unlist(raw$type)
  types <- types[!duplicated(types)]
  nameThresholds = sprintf("thresholds")
} else {
  types <- c(typeFilter)
  if (length(raw[raw$type==typeFilter,]) == 0) {
    cat(sprintf("No data found for type %s\n",typeFilter))
    quit(status=1)
  }
  nameThresholds = sprintf("thresholds-%s",typeFilter)
}

numTypes = length(types)
# Create an empty data frame for the percentage values. Since we want numSteps for each data type, it must contain numSteps * numTypes rows.
data <- data.frame(type=character(numSteps * numTypes),threshold=integer(numSteps * numTypes),percentage=integer(numSteps * numTypes))
# Create a vector with numSteps * numTypes entries. Each row of numSteps rows contains the same type.
temp <- vector()
for (type in types) {
  temp<-c(temp,rep(type,numSteps))
}
# Add those rows to newly created data frame
data[,1] <- temp

for(type in types) {
  data[data$type==type,2] <- steps
  totalObs <- nrow(raw[raw$type==type,])
  # Compute the percentage of accepted hypotheses for each cut-off threshold (aka. steps)
  for(step in steps) {
    data[data$type == type & data$threshold == step,3] <- nrow(raw[raw$type == type & raw$percentage >= step,]) / totalObs * 100
  }

  # Plot the distribution of percentages for the winner hypothesis for each member
  # reorder(member,percentage) sorts the members by percentage in ascending order
  plot <- ggplot(raw[raw$type == type,],aes(x=reorder(member,percentage),y=percentage,fill=occurrences)) +
  geom_bar(stat='identity') +
  theme(axis.text.x= element_text(angle=90,hjust=1)) +
  xlab("Member") +
  ylab("Percentage") +
  scale_fill_gradient(name="Occurrences",trans = "log")
  
  nameDist = sprintf("dist-percentages-%s",type)
  mySavePlot(plot,nameDist,directory)
}

plot <- ggplot(data,aes(x=threshold,y=percentage,group=type,colour=type)) +
        geom_line() +
        geom_point() +
        scale_x_discrete(name="Threshold", limits=steps, breaks=steps) +
        ggtitle(nameThresholds)
mySavePlot(plot,nameThresholds,directory)

