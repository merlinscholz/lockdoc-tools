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
  inputfname = "all_txns_members_locks_hypo_winner2.csv"
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
raw <- cbind(raw, idx=paste(raw$accesstype,raw$member,sep=":"))
steps <- seq(from=startThreshold,to=maxThreshold,by=stepSize)
# Extract all unique access types
accessTypes <- unlist(raw$accesstype)
accessTypes <- accessTypes[!duplicated(accessTypes)]

if (is.null(typeFilter)) {
  # Extract all unique data types in input data
  dataTypes <- unlist(raw$type)
  dataTypes <- dataTypes[!duplicated(dataTypes)]
  nameThresholds = sprintf("thresholds")
} else {
  dataTypes <- c(typeFilter)
  if (length(raw[raw$type==typeFilter,]) == 0) {
    cat(sprintf("No data found for type %s\n",typeFilter))
    quit(status=1)
  }
  nameThresholds = sprintf("thresholds-%s",typeFilter)
}

numTypes = length(dataTypes)
numAccessTypes = length(accessTypes)
extractMemberName <- function(x) sub("[^_]*:","",x )  

# Create an empty data frame for the percentage values. Since we want numSteps for each data type, it must contain numSteps * numTypes * numAccessTypes rows.
data <- data.frame(datatype=character(numSteps * numTypes * numAccessTypes),accesstype=character(numSteps * numTypes * numAccessTypes),threshold=integer(numSteps * numTypes * numAccessTypes),percentage=integer(numSteps * numTypes))
# Create a vector with numSteps * numTypes * numAccessTypes entries. Each row of numSteps * numAccessTypes rows contains the same type.
temp <- vector()
for (dataType in dataTypes) {
  temp<-c(temp,rep(dataType,numSteps * numAccessTypes))
}
# Add those rows to newly created data frame
data$datatype <- temp
# Create a vector with numSteps * numTypes * numAccessTypes entries. Each row of numSteps rows contains the same access type.
temp <- vector()
for (dataType in dataTypes) {
  for (accessType in accessTypes) {
    temp<-c(temp,rep(accessType,numSteps))
  }
}
data$accesstype <- temp

for(dataType in dataTypes) {
  # Compute the percentage of accepted hypotheses for each cut-off threshold (aka. steps) by access type
  for (accessType in accessTypes) {
    data[data$datatype == dataType & data$accesstype == accessType,3] <- steps
    totalObs <- nrow(raw[raw$type == dataType & raw$accesstype == accessType,])
    for(step in steps) {
      data[data$datatype == dataType & data$accesstype == accessType & data$threshold == step,4] <- nrow(raw[raw$type == dataType & raw$accesstype == accessType & raw$percentage >= step,]) / totalObs * 100
    }
  }

  # Plot the distribution of percentages for the winner hypothesis for each member, one for each access type
  # reorder(member,percentage) sorts the members by percentage in ascending order
  # To create a facet plot with every single facet having an ordered x axis, a little hack as described here (http://stackoverflow.com/questions/26238687/r-reorder-facet-wrapped-x-axis-with-free-x-in-ggplot2)
  # is necessary
  plot <- ggplot(raw[raw$type == dataType,],aes(x=reorder(idx,percentage),y=percentage,fill=occurrences)) +
  geom_bar(stat='identity') +
  theme(axis.text.x= element_text(angle=90,hjust=1)) +
  xlab("Member") +
  ylab("Percentage") +
  scale_fill_gradient(name="Occurrences",trans = "log") +
  facet_grid(~ accesstype, scales = "free_x") +
  scale_x_discrete(labels=extractMemberName)
  
  nameDist = sprintf("dist-percentages-%s",dataType)
  mySavePlot(plot,nameDist,directory)
}

plot <- ggplot(data,aes(x=threshold,y=percentage,group=datatype,colour=datatype)) +
        geom_line() +
        geom_point() +
        scale_x_discrete(name="Threshold", limits=steps, breaks=steps) +
        ggtitle(nameThresholds) + 
        facet_grid(accesstype ~ .)
mySavePlot(plot,nameThresholds,directory)

