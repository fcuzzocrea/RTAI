docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)

EXTRA_DIST = \
	development \
	internals \
	intro \
	maintainers \
	services \
	Doxyfile.in \
	fifos_overview.dox \
	lxrt.dox \
	lxrt_faq.dox \
	schedulers_overview.dox

DOCS=html

if !CONFIG_RTAI_DOC
DOCSDIR = $(srcdir)/generated
else
DOCSDIR = .


DISTCLEANFILES = *.pdf *.ps

# Doxygen documentation
all-local: doxygen-api

doxygen-api: Doxyfile
	@DOXYGEN@ $(DOXYDOC_OPTS)

distclean-local:
	if test -d html ; then $(RM) -R html; fi

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

