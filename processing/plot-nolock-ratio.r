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
startThreshold=90
stepSize=1
endThreshold=100
numSteps=11
noLockString='(no locks held)'

args <- commandArgs(trailingOnly=T)

spec = matrix(c(
  'inputfile', 'i', 1, 'character',
  'type'   , 't', 1, 'character',
  'acceptanceThreshold'   , 's', 1, 'integer'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$inputfile)) {
  inputfname = "all-txns-members-locks-hypo-nostack-subclasses.csv"
} else {
  inputfname = opt$inputfile
}

if (is.null(opt$type)) {
  typeFilter = NULL
} else {
  typeFilter = opt$type
}

if (is.null(opt$acceptanceThreshold)) {
  acceptanceThreshold = 90
} else {
  acceptanceThreshold = opt$acceptanceThreshold
}
startThreshold = acceptanceThreshold
numSteps <- endThreshold - startThreshold / stepSize + 1

raw <- read.csv(inputfname,sep=";")
# Filter out the task_struct since it does not belong to the observed fs subsystem.
raw <- raw[raw$type != 'task_struct',]
raw <- cbind(raw, idx=paste(raw$accesstype,raw$member,sep=":"))
steps <- seq(from=startThreshold,to=endThreshold,by=stepSize)
breaks <- seq(from=startThreshold,to=endThreshold,by=stepSize*2)
# Extract all unique access types
accessTypes <- unlist(raw$accesstype)
accessTypes <- accessTypes[!duplicated(accessTypes)]

if (is.null(typeFilter)) {
  # Extract all unique data types in input data
  dataTypes <- raw[grep('.*:.*', raw$type, invert=TRUE),]$type
  dataTypes <- unlist(dataTypes)
  dataTypes <- dataTypes[!duplicated(dataTypes)]
  nameThresholds = sprintf("nolock-ratio")
} else {
  dataTypes <- c(typeFilter)
  if (length(raw[raw$type==typeFilter,]) == 0) {
    cat(sprintf("No data found for type %s\n",typeFilter))
    quit(status=1)
  }
  nameThresholds = sprintf("nolock-ratio-%s",typeFilter)
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
  # Compute the percentage of accepted hypotheses for each cut-off threshold (aka. steps) by access type
  for (accessType in accessTypes) {
    data[data$datatype == dataType & data$accesstype == accessType,3] <- steps
    totalMembersLocked <- raw[raw$type == dataType & raw$accesstype == accessType & raw$percentage >= acceptanceThreshold,]$member
    totalMembersLocked <- unlist(totalMembersLocked)
    totalMembersLocked <- totalMembersLocked[!duplicated(totalMembersLocked)]
    totalObs <- length(totalMembersLocked)
    for(step in steps) {
      if (totalObs == 0) {
        data[data$datatype == dataType & data$accesstype == accessType & data$threshold == step,4] <- 0
        next
      }
      curMembersLocked <- raw[raw$type == dataType & raw$accesstype == accessType & raw$locks != noLockString & raw$percentage >= step,]$member
      curMembersLocked <- unlist(curMembersLocked)
      curMembersLocked <- curMembersLocked[!duplicated(curMembersLocked)]
      curObsLocked <- length(curMembersLocked)
      data[data$datatype == dataType & data$accesstype == accessType & data$threshold == step,4] <- (totalObs - curObsLocked) / totalObs * 100
    }
  }
}

plot <- ggplot(data,aes(x=threshold,y=percentage,group=datatype,colour=datatype)) + 
        ylab('Fraction of \'no lock\' hypothesis') +
        labs(colour='Data Type') +
        geom_line() +
        geom_point() +
        scale_x_discrete(name="Acceptance Threshold", limits=steps, breaks=breaks) +
#        ggtitle(nameThresholds) + 
        facet_grid(accesstype ~ .)
mySavePlot(plot,nameThresholds)

