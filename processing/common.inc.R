library("ggplot2")
library("reshape2")

global_plot_width = 4.5
global_plot_height = 3

lockDocSavePlot <- function(plot, name, directory=NULL, width=global_plot_width, height=global_plot_height) {
  
  if (is.null(directory)) {
    fname = sprintf("%s",name)
  } else {
    if (!file.exists(directory)) {
      cat(sprintf("Creating directory %s ...\n",directory))
      dir.create(directory, recursive = T)
    }
    fname = sprintf("%s/%s",directory,name)
  }
  
  cat(sprintf("Creating: %s\n",fname))
  ggsave(file=fname,plot,device="pdf",units="cm", width=width, height=height, useDingbats=FALSE)
  embedFonts(fname, "pdfwrite")
  system(paste("pdfcrop", fname))
}

lockDocTheme = function()
{
	theme_text = function(...)
		ggplot2::element_text(family="Helvetica", size=9, ...)
	theme_bw() + theme(plot.margin = unit(c(0,0,0,0), "cm"), text = theme_text())
}

#initial.options <- commandArgs(trailingOnly = FALSE)
#file.arg.name <- "--file="
#script.name <- sub(file.arg.name, "", initial.options[grep(file.arg.name, initial.options)])
#script.basename <- dirname(script.name)
#source(paste(script.basename, './common.inc.R', sep = "/"))
