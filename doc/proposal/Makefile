#****************************************************************************
#
# Makefile for pdf article.
#
#****************************************************************************

INPUT:=acceleraTor

pdf:
	pdflatex -interaction=nonstopmode ${INPUT}.tex
	bibtex ${INPUT}.aux
	pdflatex -interaction=nonstopmode ${INPUT}.tex
	pdflatex -interaction=nonstopmode ${INPUT}.tex
#	dvipdfm ${INPUT}.dvi
	evince ${INPUT}.pdf

clean:
	- rm -f ${INPUT}.aux ${INPUT}.bbl ${INPUT}.blg ${INPUT}.log ${INPUT}.out ${INPUT}.toc ${INPUT}.idx *.aux *.lof *.lot *.brf *.cpt *.dvi *~

