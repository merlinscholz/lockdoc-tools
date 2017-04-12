library("stringr")
library("getopt")

# Author: Alexander Lochmann, 2017
# This script calculates the percentage of correct determined hypotheses.
# It therefore takes the output of the hypothesizer (--report winner) and the csv with the ground truth as input.

spec = matrix(c(
  'winner', 'w', 1, 'character',
  'groundtruth'   , 't', 1, 'character',
  'verbose'   , 'v', 0, 'integer'
), byrow=TRUE, ncol=4);
opt = getopt(spec);

if (is.null(opt$groundtruth)) {
#  cat(getopt(spec, usage=TRUE));
#  q(status=1);
  groundtruthFname = '../data/ground-truth.csv'
} else {
  groundtruthFname = opt$groundtruth
}

if (is.null(opt$winner)) {
#  cat(getopt(spec, usage=TRUE));
#  q(status=1);
  winnerFname = '../data/all_txns_members_locks_hypo_winner.csv'
} else {
  winnerFname = opt$winner
}

if (is.null(opt$verbose)) {
  verbose = 0
} else {
  verbose = 1
}


data <- read.csv(winnerFname, sep = ';')
groundtruth <- read.csv(groundtruthFname, sep = ';', allowEscapes = F)
# Get all available data types
dataTypes <- unlist(groundtruth$datatype)
dataTypes <- dataTypes[!duplicated(dataTypes)]
# Get all available access types
accessTypes <- unlist(groundtruth$accesstype)
accessTypes <- accessTypes[!duplicated(accessTypes)]

numTypes <- length(dataTypes)
results <- data.frame(datatype = character(numTypes), total = integer(numTypes), correct = integer(numTypes), wrong = integer(numTypes), percentage = integer(numTypes))
results$datatype <- dataTypes

for (dataType in dataTypes) {
  members <- unlist(groundtruth[groundtruth$datatype == dataType,]$member)
  members <- members[!duplicated(members)]
  
  totalObservations <- 0
  correct <- 0
  wrong <- 0
  
  for (member in members) {
    for (accessType in accessTypes) {
      if (nrow(data[data$type == dataType & data$member == member & data$accesstype == accessType,]) == 0 ||
          nrow(groundtruth[groundtruth$datatype == dataType & groundtruth$member == member & groundtruth$accesstype == accessType,]) == 0) {
        next
      }
      totalObservations <- totalObservations + 1
      
      if (str_detect(data[data$type == dataType & data$member == member & data$accesstype == accessType,]$locks,
                     as.character(groundtruth[groundtruth$datatype == dataType & groundtruth$member == member & groundtruth$accesstype == accessType,]$rule))) {
        correct <- correct + 1
        if (verbose) {
          cat(sprintf("! datatype: %s, member: %s , accesstype: %s, hypothesis: %s, rule: %s\n", dataType, member, accessType,
                     data[data$type == dataType & data$member == member & data$accesstype == accessType,]$locks,
                      as.character(groundtruth[groundtruth$datatype == dataType & groundtruth$member == member & groundtruth$accesstype == accessType,]$rule)))
        }
      } else {
        wrong <- wrong + 1
        if (verbose) {
          cat(sprintf("X datatype: %s, member: %s , accesstype: %s, hypothesis: %s, rule: %s\n", dataType, member, accessType,
                      data[data$type == dataType & data$member == member & data$accesstype == accessType,]$locks,
                      as.character(groundtruth[groundtruth$datatype == dataType & groundtruth$member == member & groundtruth$accesstype == accessType,]$rule)))
        }
      }
    }
  }
  
  percentage <- correct / totalObservations * 100
  results[results$datatype == dataType,]$total <- totalObservations
  results[results$datatype == dataType,]$correct <- correct
  results[results$datatype == dataType,]$wrong <- wrong
  results[results$datatype == dataType,]$percentage <- percentage
  cat(sprintf("datatype: %s, total: %d, correct: %d, wrong: %d, percentage: %.2f\n", dataType, totalObservations, correct, wrong, percentage))
}