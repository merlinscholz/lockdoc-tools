library("getopt")
library("plyr")
source('./common.inc.R')

# Author: Alexander Lochmann, 2019
# TODO
# The optional argument -t my restrict the data to a certain data type.

startPercentage=70

spec = matrix(c(
  'inputFile', 'i', 1, 'character',
  'type'   , 't', 1, 'character'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$inputFile)) {
  inputfname = "all-txns-members-locks-hypo-nostack-nosubclasses.csv"
} else {
  inputfname = opt$inputFile
}

if (is.null(opt$type)) {
  typeFilter = NULL
} else {
  typeFilter = opt$type
}

raw <- read.csv(inputfname,sep=";")
# Filter out the task_struct since it does not belong to the observed fs subsystem.
raw <- raw[raw$type != 'task_struct',]
raw <- cbind(raw, idx=paste(raw$accesstype,raw$member,sep=":"))

if (is.null(typeFilter)) {
  # Extract all unique data types in input data
#  dataTypes <- raw[grep('.*:.*', raw$type, invert=TRUE),]$type
  dataTypes <- raw$type
  dataTypes <- unlist(dataTypes)
  dataTypes <- dataTypes[!duplicated(dataTypes)]
} else {
  dataTypes <- c(typeFilter)
  if (length(raw[raw$type==typeFilter,]) == 0) {
    cat(sprintf("No data found for type %s\n",typeFilter))
    quit(status=1)
  }
}

for(dataType in dataTypes) {
  plot <-  ggplot(raw[raw$percentage >= startPercentage & raw$locks != noLockString & raw$type == dataType & raw$accesstype == 'w',],aes(x=percentage))+
    geom_histogram(color="black", fill="yellow", binwidth=1) + facet_wrap(~member, ncol=5, scales="free_y")
  outputFile = sprintf("hypotheses-histogramm-%s.pdf", dataType)
  lockDocSavePlot(plot,outputFile, width=40, height=20)
}

plot <- ggplot(raw[raw$percentage >= startPercentage & raw$locks != noLockString & raw$accesstype == 'w',],aes(x=percentage))  +
  geom_histogram(color="black", fill="yellow", binwidth=1) + facet_wrap(~type, ncol=5, scales="free_y")
outputFile = sprintf("hypotheses-histogramm-overview.pdf")
lockDocSavePlot(plot,outputFile, width=40, height=20)
