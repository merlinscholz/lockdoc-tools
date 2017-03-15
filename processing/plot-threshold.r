library("ggplot2")
library("reshape2")
library("getopt")

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
  if (!file.exists(directory)) {
    cat(sprintf("Creating directory %s ...\n",directory))
    dir.create(directory)
  }
}

raw <- read.csv(inputfname,sep=";")
raw <- raw[raw$type != 'task_struct',]
steps <- seq(from=startThreshold,to=maxThreshold,by=stepSize)

if (is.null(typeFilter)) {
  types <- unlist(raw$type)
  types <- types[!duplicated(types)]
  name = sprintf("thresholds")
} else {
  types <- c(typeFilter)
  if (length(raw[raw$type==typeFilter,]) == 0) {
    cat(sprintf("No data found for type %s\n",typeFilter))
    quit(status=1)
  }
  name = sprintf("thresholds-%s",typeFilter)
}
numTypes = length(types)

data<- data.frame(type=character(numSteps * numTypes),threshold=integer(numSteps * numTypes),num=integer(numSteps * numTypes))
temp<-vector()
for (type in types) {
  temp<-c(temp,rep(type,numSteps))
}
data[,1] <- temp

for(type in types) {
  data[data$type==type,2] <- steps
  totalObs <- nrow(raw[raw$type==type,])
  for(step in steps) {
    data[data$type==type & data$threshold==step,3] <- nrow(raw[raw$type==type & raw$percentage >= step,]) / totalObs * 100
  }
}

plot <- ggplot(data,aes(x=threshold,y=num,group=type,colour=type)) +
        geom_line() +
        geom_point() +
        scale_x_discrete("Threshold", steps, steps) +
        ggtitle(name)

if (is.null(directory)) {
  fname = sprintf("%s.pdf",name)
} else {
  fname = sprintf("%s/%s.pdf",directory,name)
}
cat(sprintf("Creating: %s\n",fname))
ggsave(file=fname,plot,units="cm")