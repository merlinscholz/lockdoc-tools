library("ggplot2")
library("reshape2")
library("getopt")
library("plyr")

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
      dir.create(directory, recursive = T)
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
  'directory'   , 'd', 1, 'character',
  'acceptanceThreshold'   , 's', 1, 'integer'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$inputfile)) {
  inputfname = "all_txns_members_locks_hypo_winner_nostack.csv"
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

if (is.null(opt$acceptanceThreshold)) {
  acceptanceThreshold = 90
} else {
  acceptanceThreshold = opt$acceptanceThreshold
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
cat(sprintf("Acceptance threshold: %d\n",acceptanceThreshold))
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
  rejectRange <- data.frame(accesstype=integer(length(accessTypes)),last_member=integer(length(accessTypes)))
  rejectRange$accesstype <- accessTypes
  # Compute the percentage of accepted hypotheses for each cut-off threshold (aka. steps) by access type
  for (accessType in accessTypes) {
    data[data$datatype == dataType & data$accesstype == accessType,3] <- steps
    totalObs <- nrow(raw[raw$type == dataType & raw$accesstype == accessType,])
    for(step in steps) {
      data[data$datatype == dataType & data$accesstype == accessType & data$threshold == step,4] <- nrow(raw[raw$type == dataType & raw$accesstype == accessType & raw$percentage >= step,]) / totalObs * 100
    }
    
    # Get all hypotheses which would be rejected, because their winning hypothesis is below the acceptance threshold.
    temp <- raw[raw$type == dataType & raw$accesstype == accessType & raw$percentage < acceptanceThreshold,]
    temp <- temp[ order(temp$percentage,decreasing = T),]
    # Count those rejected hypotheses
    rejectRange[rejectRange$accesstype == accessType,]$last_member <- nrow(temp)
  }
  
  means <- ddply(raw[raw$type == dataType,],"accesstype",summarise,percentage_mean=mean(percentage))
  
  # Plot the distribution of percentages for the winner hypothesis for each member, one for each access type
  # reorder(member,percentage) sorts the members by percentage in ascending order
  # To create a facet plot with every single facet having an ordered x axis, a little hack, as described here (http://stackoverflow.com/questions/26238687/r-reorder-facet-wrapped-x-axis-with-free-x-in-ggplot2),
  # is necessary
  # Furthermore, it draws a line for the mean value of each access type, and draws a rectangle to mark rejected members.
  foo <- data.frame(acceptanceThreshold=c(acceptanceThreshold))
  plot <- ggplot() +
#  geom_rect(data=rejectRange,xmin=0,ymin=0,ymax=100,fill='red',alpha=0.1,mapping=aes(xmax=last_member)) +
  geom_bar(data=raw[raw$type == dataType,],mapping=aes(x=reorder(idx,percentage),y=percentage,fill=occurrences),stat='identity') +
  theme(axis.text.x= element_blank()) + #element_text(angle=90,hjust=1)) +
  xlab("Member") +
  ylab("Support") +
  scale_fill_gradient(name="Observations",trans = "log", breaks=c(min(raw[raw$type == dataType,]$occurrences),max(raw[raw$type == dataType,]$occurrences))) +
  facet_grid(~ accesstype, scales = "free_x") +
#  scale_x_discrete(labels=extractMemberName) + 
  scale_x_discrete() +
  geom_hline(data=means,aes(yintercept = percentage_mean,linetype=factor(round(percentage_mean,2))),show.legend = T) +
  scale_linetype_discrete(name = "Mean") +
  geom_hline(data=foo,mapping=aes(yintercept=acceptanceThreshold, color='red'), show.legend = T) +
  scale_color_discrete(labels=c(acceptanceThreshold), name='Threshold')
  
  nameDist = sprintf("dist-percentages-%s",dataType)
  mySavePlot(plot,nameDist,directory)
}

plot <- ggplot(data,aes(x=threshold,y=percentage,group=datatype,colour=datatype)) + 
        ylab('Percentage of accepted hypothesis') +
        labs(colour='Data Type') +
        geom_line() +
        geom_point() +
        scale_x_discrete(name="Acceptance Threshold", limits=steps, breaks=steps) +
#        ggtitle(nameThresholds) + 
        facet_grid(accesstype ~ .)
mySavePlot(plot,nameThresholds,directory)

