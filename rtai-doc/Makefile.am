docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

EXTRA_DIST = \
	appendix \
	development \
	internals \
	intro \
	maintainers \
	services \
	RTAI-RefGuide.tex \
	Doxyfile.in \
	fifos_overview.dox \
	lxrt.dox \
	lxrt_faq.dox \
	schedulers_overview.dox

DOCS=RTAI-RefGuide.ps RTAI-RefGuide.pdf html

if !CONFIG_RTAI_DOC
DOCSDIR = $(srcdir)/generated
else
DOCSDIR = .


# latex documentation
TEX_SRCS=$(shell find $(srcdir) -name "*.tex")

noinst_DATA=RTAI-RefGuide.ps RTAI-RefGuide.pdf

all-local: $(noinst_DATA)

dvi-local: RTAI-RefGuide.dvi
pdf-local: RTAI-RefGuide.pdf
ps-local: RTAI-RefGuide.ps

CLEANFILES = *.aux *.dvi *.log *.idx *.ilg *.ind *.toc *.out
DISTCLEANFILES = *.pdf *.ps

RTAI-RefGuide.dvi RTAI-RefGuide.pdf: $(TEX_SRCS)

# Doxygen documentation
all-local: doxygen-api

doxygen-api: Doxyfile
	@DOXYGEN@ $(DOXYDOC_OPTS)

distclean-local:
	if test -d html ; then $(RM) -R html; fi

# rules for latex  documentation.
RTAIDOC_TEXINPUTS=.:$(srcdir):$(TEXINPUTS)
LATEX=TEXINPUTS=$(RTAIDOC_TEXINPUTS) latex --interaction @LATEX_MODE@
PDFLATEX=TEXINPUTS=$(RTAIDOC_TEXINPUTS) pdflatex --interaction @LATEX_MODE@

SUFFIXES=pdf dvi tex


.tex.pdf:
	@$(PDFLATEX) $<
	@makeindex $*.idx
	@$(PDFLATEX) $<
	@while egrep -q 'Rerun (LaTeX|to get cross-references right)' $*.log; \
	    do $(PDFLATEX) $< || exit 1; \
	done;

.tex.dvi:
	@$(LATEX) $<
	@makeindex $*.idx
	@$(LATEX) $<
	@while egrep -q 'Rerun (LaTeX|to get cross-references right)' $*.log; \
	    do $(LATEX) $< || exit 1; \
	done;

.dvi.ps:
	@dvips -o $@ $<

.DELETE_ON_ERROR:

endif

install-data-local: all-local
	$(mkinstalldirs) $(DESTDIR)$(docdir)
	for f in $(DOCS); do \
            cp -a $(DOCSDIR)/$$f $(DESTDIR)$(docdir); \
	done
	rm -fr `find $(DESTDIR)$(docdir) -name CVS`

dist-hook: all-local
	$(mkinstalldirs) $(distdir)/generated
	for f in $(DOCS); do \
            cp -a $(DOCSDIR)/$$f $(distdir)/generated; \
	done
