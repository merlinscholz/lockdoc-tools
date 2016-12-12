library("ggplot2")
library("reshape2")
library("getopt")

args <- commandArgs(trailingOnly=T)

spec = matrix(c(
  'inputfile', 'i', 1, 'character',
  'member'   , 'm', 1, 'character'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$inputfile)) {
  inputfile = "dists.txt"
} else {
  inputfile = opt$inputfile
}

if (is.null(opt$member)) {
  memberFilter = NULL
} else {
  memberFilter = opt$member
}

cat(sprintf("Reading %s ...\n",inputfile))
data <- read.csv(inputfile,header=T,sep=";")
ac_types <- c("r","w")
members <- unlist(data$member)
members <- members[!duplicated(members)]
for (member in members) {
  if (!is.null(memberFilter) && member != memberFilter) {
    next
  }
  name = sprintf("dists-%s",member)
  fname = sprintf("%s.pdf",name)
  cat(sprintf("Creating: %s\n",name))
  plot <- ggplot(data[data$member==member,],aes(x=lock_types,y=num)) +
  geom_bar(aes(fill=embedded_in_same),position="dodge",stat="identity") +
  facet_grid(ac_type ~ context, scales = "free_y") +
  scale_y_log10() + 
  theme(axis.text.x= element_text(angle=45,hjust=1)) +
  ggtitle(name)
  ggsave(file=fname,plot,units="cm")
}

