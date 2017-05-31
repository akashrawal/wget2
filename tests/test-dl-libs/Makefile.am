check_LTLIBRARIES = libalpha.la libbeta.la

libalpha_la_SOURCES = alpha.c
libalpha_la_CPPFLAGS = -DPARAM=alpha

libbeta_la_SOURCES = alpha.c
libbeta_la_CPPFLAGS = -DPARAM=beta

AM_LDFLAGS = -avoid-version -module -shared -no-undefined \
	-rpath $(abs_builddir)
