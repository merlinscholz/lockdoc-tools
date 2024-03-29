library("ggplot2")
library("reshape2")
library("getopt")

args <- commandArgs(trailingOnly=T)

spec = matrix(c(
  'inputfile', 'i', 1, 'character',
  'member'   , 'm', 1, 'character',
  'directory'   , 'd', 1, 'character'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$inputfile)) {
  inputfname = "dists.txt"
} else {
  inputfname = opt$inputfile
}

if (is.null(opt$member)) {
  memberFilter = NULL
} else {
  memberFilter = opt$member
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

if (inputfname == "-") {
  inputfname = "stdin"
}
inputfile <- file(inputfname);
open(inputfile)
cat(sprintf("Reading %s ...\n",inputfname))
data <- read.csv(inputfile,header=T,sep=";")
ac_types <- c("r","w")
members <- unlist(data$member)
members <- members[!duplicated(members)]
database <- unlist(data$db)
database <- database[!duplicated(database)]
dt_name <- unlist(data$dt_name)
dt_name <- dt_name[!duplicated(dt_name)]
for (member in members) {
  if (!is.null(memberFilter) && member != memberFilter) {
    next
  }
  name = sprintf("%s-%s-%s",dt_name,member,database)
  if (is.null(directory)) {
    fname = sprintf("%s.pdf",name)
  } else {
    fname = sprintf("%s/%s.pdf",directory,name)
  }
  cat(sprintf("Creating: %s\n",fname))
  plot <- ggplot(data[data$member==member,],aes(x=lock_member,y=num)) +
  geom_bar(position="dodge",stat="identity") +
  facet_grid(ac_type ~ context, scales = "free_y") +
  scale_y_log10() + 
  theme(axis.text.x= element_text(angle=90,hjust=1)) +
  ggtitle(name)
  ggsave(file=fname,plot,units="cm")
}

