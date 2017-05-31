check_LTLIBRARIES = libalpha.la libbeta.la

libalpha_la_SOURCES = alpha.c
libalpha_la_CPPFLAGS = -DPARAM=alpha
libalpha_la_LDFLAGS = -avoid-version -module -shared -rpath $(abs_builddir)

libbeta_la_SOURCES = alpha.c
libbeta_la_CPPFLAGS = -DPARAM=beta
libbeta_la_LDFLAGS = -avoid-version -module -shared -rpath $(abs_builddir)
