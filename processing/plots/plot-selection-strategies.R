source('./common.inc.R')
args <- commandArgs(trailingOnly=T)

spec = matrix(c(
  'inputFile', 'i', 1, 'character',
  'outputFile'   , 'o', 1, 'character'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$inputFile)) {
  cat("No input file given!\n")
  quit(save = "no")
} else {
  inputfname = opt$inputFile
}
if (is.null(opt$outputFile)) {
  outputFile = "selection-strategy-results.pdf"
} else {
  outputFile = opt$outputFile
}

raw <- read.csv(inputfname,sep=";")

locksetPercentage <- raw[raw$strategy == "lockset" & raw$parameter == 0,]$percentage
numRows <- length(raw[raw$strategy != "lockset",]$strategy)
data <- data.frame(strategy = raw[raw$strategy != "lockset",]$strategy,
          parameter = raw[raw$strategy != "lockset",]$parameter,
          percentage = raw[raw$strategy != "lockset",]$percentage,
          lockset_percentage = rep(locksetPercentage, numRows))
labels <- c(topdown = 'Top Down', bottomup = 'Bottom Up', sharpen = 'Sharpen')
minPercentage <- min(data$percentage)
if (locksetPercentage < minPercentage) {
  minPercentage <- locksetPercentage
}
maxPercentage <- max(data$percentage)
startTicks <- 2 * round(minPercentage / 2) - 2
endTicks <- 2 * round(maxPercentage / 2) + 2
steps <- seq(from=startTicks,to=endTicks,by=2)
breaks <- seq(from=startTicks,to=endTicks,by=2)
ann_text <- data.frame(parameter = min(data[data$strategy == "bottomup",]$parameter), percentage = locksetPercentage, lab = "LockSet", strategy = factor("bottomup",levels = c("bottomup","topdown","sharpen")))

plot <- ggplot(data = data,aes(x = parameter,y = percentage)) + 
  geom_line(size = 1, color = "dodgerblue2") +
  geom_point(size = 3, color = "dodgerblue2") +
  facet_wrap(strategy ~ ., scales = "free_x", labeller = labeller(strategy = labels)) +
  ylab("Korrekte Sperren-Regeln (%)") +
  xlab("Parameterbereich des jeweiligen Verfahrens") +
  geom_hline(data=data, aes(yintercept = lockset_percentage), linetype = "dashed", color = "black") +
  geom_text(data = ann_text,label = "LockSet-Ergebnis", nudge_x = 1.2, nudge_y = .4) +
  scale_y_discrete(breaks = breaks, limits = steps) +
  coord_cartesian(ylim = c(startTicks, endTicks)) +
  theme(
    legend.background = element_rect(size=.2, color="black"),
    legend.title = element_text(size=6),
    legend.text = element_text(size=6),
    legend.key.width = unit(.7, "cm"),
    legend.key.height = unit(.2, "cm"),
    legend.position = c(.95,.45),
    legend.justification = "right",
    legend.direction = "vertical",
  )

lockDocSavePlot(plot, outputFile, width=20, height=10)
