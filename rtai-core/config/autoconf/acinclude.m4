dnl AC_PATH_XREQUIRED() requires X libs. This frag has been
dnl lifted nearly "as is" from Postgresql's configure.in script.

AC_DEFUN(AC_PATH_XREQUIRED,
[
	save_LIBS="$LIBS"
	save_CFLAGS="$CFLAGS"
	save_CPPFLAGS="$CPPFLAGS"
	save_LDFLAGS="$LDFLAGS"

	AC_PATH_X
	AC_PATH_XTRA

	LIBS="$LIBS $X_EXTRA_LIBS"
	CFLAGS="$CFLAGS $X_CFLAGS"
	CPPFLAGS="$CPPFLAGS $X_CFLAGS"
	LDFLAGS="$LDFLAGS $X_LIBS"

	dnl Check for X library

	X11_LIBS=""
	AC_CHECK_LIB(X11, XOpenDisplay, X11_LIBS="-lX11",,${X_PRE_LIBS})
	if test "$X11_LIBS" = ""; then
		dnl Not having X is bad news, period. Let the user fix this.
		AC_MSG_ERROR([The X11 library '-lX11' could not be found,
 so I won't go further. Please use the configure
 options '--x-includes=DIR' and '--x-libraries=DIR'
 to specify the X location. See the file 'config.log'
 for further diagnostics.])
	fi
	AC_SUBST(X_LIBS)
	AC_SUBST(X11_LIBS)
	AC_SUBST(X_PRE_LIBS)

	LIBS="$save_LIBS"
	CFLAGS="$save_CFLAGS"
	CPPFLAGS="$save_CPPFLAGS"
	LDFLAGS="$save_LDFLAGS"
])

dnl AC_POSIX_SIGHANDLER() determines whether
dnl signal handlers are posix compliant. This frag
dnl has been adapted from readline's aclocal.m4.

AC_DEFUN(AC_POSIX_SIGHANDLER,
[AC_MSG_CHECKING([if signal handlers are posix compliant])
AC_CACHE_VAL(ac_cv_posix_sighandler,
[AC_TRY_COMPILE([#include <sys/types.h>
#include <signal.h>
#ifdef signal
#undef signal
#endif
#ifdef __cplusplus
extern "C"
#endif
void (*signal(void))(void);],
[int i;], ac_cv_posix_sighandler=no, ac_cv_posix_sighandler=yes)])dnl
AC_MSG_RESULT($ac_cv_posix_sighandler)
if test $ac_cv_posix_sighandler = yes; then
AC_DEFINE(HAVE_POSIX_SIGHANDLER,1,[Kconfig])
fi
])

dnl AC_GCC_MVM_MOREFLAGS() determines whether
dnl the current compiler is GCC and accepts some
dnl specific additional flags we need to build the MVM.

AC_DEFUN(AC_GCC_MVM_MOREFLAGS,
[AC_MSG_CHECKING([if C compiler is GNU C])
AC_CACHE_VAL(ac_cv_using_gcc_for_mvm_c,
[AC_LANG_C
AC_TRY_COMPILE([],
[#ifdef __GNUC__
  yes;
#endif],
ac_cv_using_gcc_for_mvm_c=no, ac_cv_using_gcc_for_mvm_c=yes)])dnl
if test $ac_cv_using_gcc_for_mvm_c = yes; then
AC_MSG_RESULT(yes)
AC_MSG_CHECKING([if C compiler supports -fwritable-strings -fdollars-in-identifiers])
save_CFLAGS="$CFLAGS"
CFLAGS="-fwritable-strings -fdollars-in-identifiers -Werror"
AC_CACHE_VAL(ac_cv_cc_mvm_moreflags,
[AC_TRY_COMPILE([],
[int i = 0; return i; ],
ac_cv_cc_mvm_moreflags="-fwritable-strings -fdollars-in-identifiers", ac_cv_cc_mvm_moreflags="")])dnl
RTAI_MVM_CFLAGS="$ac_cv_cc_mvm_moreflags"
CFLAGS="$save_CFLAGS"
else
AC_MSG_RESULT(no)
fi
if test -z "$ac_cv_cc_mvm_moreflags"; then
AC_MSG_RESULT(no)
else
AC_MSG_RESULT(yes)
fi
AC_MSG_CHECKING([if C++ compiler is GNU C++])
AC_CACHE_VAL(ac_cv_using_gcc_for_mvm_cxx,
[AC_LANG_CPLUSPLUS
AC_TRY_COMPILE([],
[#ifdef __GNUC__
  yes;
#endif],
ac_cv_using_gcc_for_mvm_cxx=no, ac_cv_using_gcc_for_mvm_cxx=yes)])dnl
if test $ac_cv_using_gcc_for_mvm_cxx = yes; then
AC_MSG_RESULT(yes)
save_CXXFLAGS="$CXXFLAGS"
AC_MSG_CHECKING([if C++ compiler supports -fno-exceptions])
CXXFLAGS="-fno-exceptions -Werror"
AC_CACHE_VAL(ac_cv_cxx_mvm_noex,
[AC_TRY_COMPILE([],
[int i = 0; return i; ],
ac_cv_cxx_mvm_noex="-fno-exceptions", ac_cv_cxx_mvm_noex="")])dnl
if test -z "$ac_cv_cxx_mvm_noex"; then
AC_MSG_RESULT(no)
else
AC_MSG_RESULT(yes)
fi
AC_MSG_CHECKING([if C++ compiler supports -fnonnull-objects])
CXXFLAGS="-fnonnull-objects -Werror"
AC_CACHE_VAL(ac_cv_cxx_mvm_nonnull,
[AC_TRY_COMPILE([],
[int i = 0; return i; ],
ac_cv_cxx_mvm_nonnull="-fnonnull-objects",ac_cv_cxx_mvm_nonnull="")])dnl
if test -z "$ac_cv_cxx_mvm_nonnull"; then
AC_MSG_RESULT(no)
else
AC_MSG_RESULT(yes)
fi
RTAI_MVM_CXXFLAGS="-fwritable-strings -fdollars-in-identifiers $ac_cv_cxx_mvm_noex $ac_cv_cxx_mvm_nonnull"
CXXFLAGS="$save_CXXFLAGS"
else
AC_MSG_RESULT(no)
fi
AC_LANG_C])

#------------------------------------------------------------------------
# SC_PATH_TCLCONFIG --
#
#	Locate the tclConfig.sh file and perform a sanity check on
#	the Tcl compile flags
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--with-tcl=...
#
#	Defines the following vars:
#		TCL_BIN_DIR	Full path to the directory containing
#				the tclConfig.sh file
#------------------------------------------------------------------------

AC_DEFUN(SC_PATH_TCLCONFIG, [
    #
    # Ok, lets find the tcl configuration
    # First, look for one uninstalled.
    # the alternative search directory is invoked by --with-tcl
    #

    if test x"${no_tcl}" = x ; then
	# we reset no_tcl in case something fails here
	no_tcl=true
	AC_ARG_WITH(tcl, [  --with-tcl              directory containing tcl configuration (tclConfig.sh)], with_tclconfig=${withval})
	AC_MSG_CHECKING([for Tcl configuration])
	AC_CACHE_VAL(ac_cv_c_tclconfig,[

	    # First check to see if --with-tcl was specified.
	    if test x"${with_tclconfig}" != x ; then
		if test -f "${with_tclconfig}/tclConfig.sh" ; then
		    ac_cv_c_tclconfig=`(cd ${with_tclconfig}; pwd)`
		else
		    AC_MSG_ERROR([${with_tclconfig} directory doesn't contain tclConfig.sh])
		fi
	    fi

	    # then check for a private Tcl installation
	    if test x"${ac_cv_c_tclconfig}" = x ; then
		for i in \
			../tcl \
			`ls -dr ../tcl[[8-9]].[[0-9]]* 2>/dev/null` \
			../../tcl \
			`ls -dr ../../tcl[[8-9]].[[0-9]]* 2>/dev/null` \
			../../../tcl \
			`ls -dr ../../../tcl[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/unix/tclConfig.sh" ; then
			ac_cv_c_tclconfig=`(cd $i/unix; pwd)`
			break
		    fi
		done
	    fi

	    # check in a few common install locations
	    if test x"${ac_cv_c_tclconfig}" = x ; then
		for i in ${prefix}/lib /usr/local/lib /usr/pkg/lib /usr/lib \
			`ls -dr /usr/lib/tcl[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/tclConfig.sh" ; then
			ac_cv_c_tclconfig=`(cd $i; pwd)`
			break
		    fi
		done
	    fi

	    # check in a few other private locations
	    if test x"${ac_cv_c_tclconfig}" = x ; then
		for i in \
			${srcdir}/../tcl \
			`ls -dr ${srcdir}/../tcl[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/unix/tclConfig.sh" ; then
		    ac_cv_c_tclconfig=`(cd $i/unix; pwd)`
		    break
		fi
		done
	    fi
	])

	if test x"${ac_cv_c_tclconfig}" = x ; then
	    TCL_BIN_DIR="# no Tcl configs found"
	    AC_MSG_WARN(Can't find Tcl configuration definitions)
	    exit 1
	else
	    no_tcl=
	    TCL_BIN_DIR=${ac_cv_c_tclconfig}
	    AC_MSG_RESULT(found $TCL_BIN_DIR/tclConfig.sh)
	fi
    fi
])

#------------------------------------------------------------------------
# SC_PATH_TKCONFIG --
#
#	Locate the tkConfig.sh file
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--with-tk=...
#
#	Defines the following vars:
#		TK_BIN_DIR	Full path to the directory containing
#				the tkConfig.sh file
#------------------------------------------------------------------------

AC_DEFUN(SC_PATH_TKCONFIG, [
    #
    # Ok, lets find the tk configuration
    # First, look for one uninstalled.
    # the alternative search directory is invoked by --with-tk
    #

    if test x"${no_tk}" = x ; then
	# we reset no_tk in case something fails here
	no_tk=true
	AC_ARG_WITH(tk, [  --with-tk               directory containing tk configuration (tkConfig.sh)], with_tkconfig=${withval})
	AC_MSG_CHECKING([for Tk configuration])
	AC_CACHE_VAL(ac_cv_c_tkconfig,[

	    # First check to see if --with-tkconfig was specified.
	    if test x"${with_tkconfig}" != x ; then
		if test -f "${with_tkconfig}/tkConfig.sh" ; then
		    ac_cv_c_tkconfig=`(cd ${with_tkconfig}; pwd)`
		else
		    AC_MSG_ERROR([${with_tkconfig} directory doesn't contain tkConfig.sh])
		fi
	    fi

	    # then check for a private Tk library
	    if test x"${ac_cv_c_tkconfig}" = x ; then
		for i in \
			../tk \
			`ls -dr ../tk[[8-9]].[[0-9]]* 2>/dev/null` \
			../../tk \
			`ls -dr ../../tk[[8-9]].[[0-9]]* 2>/dev/null` \
			../../../tk \
			`ls -dr ../../../tk[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/unix/tkConfig.sh" ; then
			ac_cv_c_tkconfig=`(cd $i/unix; pwd)`
			break
		    fi
		done
	    fi
	    # check in a few common install locations
	    if test x"${ac_cv_c_tkconfig}" = x ; then
		for i in ${prefix}/lib /usr/local/lib /usr/pkg/lib /usr/lib \
			`ls -dr /usr/lib/tk[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/tkConfig.sh" ; then
			ac_cv_c_tkconfig=`(cd $i; pwd)`
			break
		    fi
		done
	    fi
	    # check in a few other private locations
	    if test x"${ac_cv_c_tkconfig}" = x ; then
		for i in \
			${srcdir}/../tk \
			`ls -dr ${srcdir}/../tk[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/unix/tkConfig.sh" ; then
			ac_cv_c_tkconfig=`(cd $i/unix; pwd)`
			break
		    fi
		done
	    fi
	])
	if test x"${ac_cv_c_tkconfig}" = x ; then
	    TK_BIN_DIR="# no Tk configs found"
	    AC_MSG_WARN(Can't find Tk configuration definitions)
	    exit 1
	else
	    no_tk=
	    TK_BIN_DIR=${ac_cv_c_tkconfig}
	    AC_MSG_RESULT(found $TK_BIN_DIR/tkConfig.sh)
	fi
    fi

])

#------------------------------------------------------------------------
# SC_LOAD_TCLCONFIG --
#
#	Load the tclConfig.sh file
#
# Arguments:
#	
#	Requires the following vars to be set:
#		TCL_BIN_DIR
#
# Results:
#
#	Subst the following vars:
#		TCL_BIN_DIR
#		TCL_SRC_DIR
#		TCL_LIB_FILE
#
#------------------------------------------------------------------------

AC_DEFUN(SC_LOAD_TCLCONFIG, [
    AC_MSG_CHECKING([for existence of $TCL_BIN_DIR/tclConfig.sh])

    if test -f "$TCL_BIN_DIR/tclConfig.sh" ; then
        AC_MSG_RESULT([loading])
	. $TCL_BIN_DIR/tclConfig.sh
    else
        AC_MSG_ERROR([not found])
    fi

    AC_PATH_PROG(TCL_SCRIPT, tclsh${TCL_VERSION}, tclsh)

    AC_SUBST(TCL_BIN_DIR)
    AC_SUBST(TCL_SRC_DIR)
    AC_SUBST(TCL_LIB_FILE)
    AC_SUBST(TCL_LIBS)
    AC_SUBST(TCL_DEFS)
    AC_SUBST(TCL_SHLIB_LD_LIBS)
    AC_SUBST(TCL_EXTRA_CFLAGS)
    AC_SUBST(TCL_LD_FLAGS)
    AC_SUBST(TCL_LIB_FILE)
    AC_SUBST(TCL_STUB_LIB_FILE)
    AC_SUBST(TCL_LIB_SPEC)
    AC_SUBST(TCL_BUILD_LIB_SPEC)
    AC_SUBST(TCL_STUB_LIB_SPEC)
    AC_SUBST(TCL_BUILD_STUB_LIB_SPEC)
    AC_SUBST(TCL_DBGX)
])

#------------------------------------------------------------------------
# SC_LOAD_TKCONFIG --
#
#	Load the tkConfig.sh file
#
# Arguments:
#	
#	Requires the following vars to be set:
#		TK_BIN_DIR
#
# Results:
#
#	Sets the following vars that should be in tkConfig.sh:
#		TK_BIN_DIR
#------------------------------------------------------------------------

AC_DEFUN(SC_LOAD_TKCONFIG, [
    AC_MSG_CHECKING([for existence of $TK_BIN_DIR/tkConfig.sh])

    if test -f "$TK_BIN_DIR/tkConfig.sh" ; then
        AC_MSG_RESULT([loading])
	. $TK_BIN_DIR/tkConfig.sh
    else
        AC_MSG_ERROR([not found])
    fi

    AC_SUBST(TK_BIN_DIR)
    AC_SUBST(TK_SRC_DIR)
    AC_SUBST(TK_LIB_FILE)
    AC_SUBST(TK_LIB_FLAG)
    AC_SUBST(TK_LIB_SPEC)
    AC_SUBST(TK_DBGX)
])

#------------------------------------------------------------------------
# SC_PATH_TIX --
#
#	Locate the Tix installation.
#
# Arguments:
#	None.
#
# Results:
#
#	Substs the following vars:
#		TIX_TCL_LIB
#		TIX_LIB_SPEC
#------------------------------------------------------------------------

AC_DEFUN(SC_PATH_TIX, [
    AC_MSG_CHECKING(for Tix's Tcl library)

    AC_ARG_WITH(tixlibrary, [  --with-tixlibrary      directory containing the Tix library files.], with_tixlibrary=${withval})

    if test x"${with_tixlibrary}" != x ; then
	if test -f "${with_tixlibrary}/Init.tcl" ; then
	    ac_cv_tix_libdir=${with_tixlibrary}
	else
	    AC_MSG_ERROR([${with_tixlibrary} directory does not contain Tix's init file Init.tcl])
	fi
    else
	AC_CACHE_VAL(ac_cv_tix_libdir, [
	    for d in \
	    `ls -dr /usr/local/lib/tix[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/local/share/tix[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/pkg/lib/tix[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/lib/tix[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/share/tix[[0-9]]* 2>/dev/null ` ; do
		if test -f "$d/Init.tcl" ; then
		ac_cv_tix_libdir=$d
	        break
	        fi
	    done
        ])
    fi

    AC_MSG_RESULT($ac_cv_tix_libdir)
    TIX_TCL_LIB=$ac_cv_tix_libdir
    AC_SUBST(TIX_TCL_LIB)

    SC_LIB_SPEC(tix)
    TIX_LIB_SPEC=$tix_LIB_SPEC
    AC_SUBST(TIX_LIB_SPEC)
])

#------------------------------------------------------------------------
# SC_LIB_SPEC --
#
#	Compute the name of an existing object library located in libdir
#	from the given base name and produce the appropriate linker flags.
#
# Arguments:
#	basename	The base name of the library without version
#			numbers, extensions, or "lib" prefixes.
#
#	Requires:
#
# Results:
#
#	Defines the following vars:
#		${basename}_LIB_NAME	The computed library name.
#		${basename}_LIB_SPEC	The computed linker flags.
#------------------------------------------------------------------------

AC_DEFUN(SC_LIB_SPEC, [
    AC_MSG_CHECKING(for $1 library)
    eval "sc_lib_name_dir=${libdir}"
    for i in \
	    `ls -dr ${sc_lib_name_dir}/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr ${sc_lib_name_dir}/lib$1.* 2>/dev/null ` \
	    `ls -dr ${sc_lib_name_dir}/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/pkg/*/lib$1.so 2>/dev/null ` \
	    `ls -dr /usr/pkg/*/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/pkg/lib/lib$1.so 2>/dev/null ` \
	    `ls -dr /usr/pkg/lib/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/lib/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr /usr/lib/lib$1.so 2>/dev/null ` \
	    `ls -dr /usr/lib/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/local/lib/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr /usr/local/lib/lib$1.so 2>/dev/null ` \
	    `ls -dr /usr/local/lib/lib$1[[0-9]]* 2>/dev/null ` ; do
	if test -f "$i" ; then
	    sc_lib_name_dir=`dirname $i`
	    $1_LIB_NAME=`basename $i`
	    break
	fi
    done

    case "`uname -s`" in
	*win32* | *WIN32* | *CYGWIN_NT*)
	    $1_LIB_SPEC=${$1_LIB_NAME}
	    ;;
	*)
	    # Strip off the leading "lib" and trailing ".a" or ".so"
	    sc_lib_name_lib=`echo ${$1_LIB_NAME}|sed -e 's/^lib//' -e 's/\.so.*$//' -e 's/\.a$//'`
	    $1_LIB_SPEC="-L${sc_lib_name_dir} -l${sc_lib_name_lib}"
	    ;;
    esac
    if test "x${sc_lib_name_lib}" = x ; then
	AC_MSG_ERROR(not found)
    else
	AC_MSG_RESULT(${$1_LIB_SPEC})
    fi
])

#------------------------------------------------------------------------
# SC_PUBLIC_TCL_HEADERS --
#
#	Locate the installed public Tcl header files
#
# Arguments:
#	None.
#
# Requires:
#
# Results:
#
#	Adds a --with-tclinclude switch to configure.
#	Result is cached.
#
#	Substs the following vars:
#		TCL_INCLUDES
#------------------------------------------------------------------------

AC_DEFUN(SC_PUBLIC_TCL_HEADERS, [
    AC_MSG_CHECKING(for Tcl public headers)

    AC_ARG_WITH(tclinclude, [  --with-tclinclude      directory containing the public Tcl header files.], with_tclinclude=${withval})

    if test x"${with_tclinclude}" != x ; then
	if test -f "${with_tclinclude}/tcl.h" ; then
	    ac_cv_c_tclh=${with_tclinclude}
	else
	    AC_MSG_ERROR([${with_tclinclude} directory does not contain Tcl public header file tcl.h])
	fi
    else
	AC_CACHE_VAL(ac_cv_c_tclh, [
	    # Use the value from --with-tclinclude, if it was given

	    if test x"${with_tclinclude}" != x ; then
		ac_cv_c_tclh=${with_tclinclude}
	    else
		# Check in the includedir, if --prefix was specified

		eval "temp_includedir=${includedir}"
		for i in \
			${temp_includedir} /usr/local/include /usr/include /usr/pkg/include \
			`ls -dr /usr/include/tcl[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/tcl.h" ; then
			ac_cv_c_tclh=$i
			break
		    fi
		done
	    fi
	])
    fi

    # Print a message based on how we determined the include path

    if test x"${ac_cv_c_tclh}" = x ; then
	AC_MSG_ERROR(tcl.h not found.  Please specify its location with --with-tclinclude)
    else
	AC_MSG_RESULT(${ac_cv_c_tclh})
    fi

    # Convert to a native path and substitute into the output files.

    INCLUDE_DIR_NATIVE=`echo ${ac_cv_c_tclh}`

    TCL_INCLUDES="-I${INCLUDE_DIR_NATIVE}"

    AC_SUBST(TCL_INCLUDES)
])

#------------------------------------------------------------------------
# SC_PUBLIC_TK_HEADERS --
#
#	Locate the installed public Tk header files
#
# Arguments:
#	None.
#
# Requires:
#
# Results:
#
#	Adds a --with-tkinclude switch to configure.
#	Result is cached.
#
#	Substs the following vars:
#		TK_INCLUDES
#------------------------------------------------------------------------

AC_DEFUN(SC_PUBLIC_TK_HEADERS, [
    AC_MSG_CHECKING(for Tk public headers)

    AC_ARG_WITH(tkinclude, [  --with-tkinclude      directory containing the public Tk header files.], with_tkinclude=${withval})

    if test x"${with_tkinclude}" != x ; then
	if test -f "${with_tkinclude}/tk.h" ; then
	    ac_cv_c_tkh=${with_tkinclude}
	else
	    AC_MSG_ERROR([${with_tkinclude} directory does not contain Tk public header file tk.h])
	fi
    else
	AC_CACHE_VAL(ac_cv_c_tkh, [
	    # Use the value from --with-tkinclude, if it was given

	    if test x"${with_tkinclude}" != x ; then
		ac_cv_c_tkh=${with_tkinclude}
	    else
		# Check in the includedir, if --prefix was specified

		eval "temp_includedir=${includedir}"
		for i in \
			${temp_includedir} /usr/local/include /usr/include /usr/pkg/include \
			`ls -dr /usr/include/tk[[8-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr /usr/include/tcl[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test -f "$i/tk.h" ; then
			ac_cv_c_tkh=$i
			break
		    fi
		done
	    fi
	])
    fi

    # Print a message based on how we determined the include path

    if test x"${ac_cv_c_tkh}" = x ; then
	AC_MSG_ERROR(tk.h not found.  Please specify its location with --with-tkinclude)
    else
	AC_MSG_RESULT(${ac_cv_c_tkh})
    fi

    # Convert to a native path and substitute into the output files.

    INCLUDE_DIR_NATIVE=`echo ${ac_cv_c_tkh}`

    TK_INCLUDES="-I${INCLUDE_DIR_NATIVE}"

    AC_SUBST(TK_INCLUDES)
])

dnl ########################### -*- Mode: M4 -*- #######################
dnl Copyright (C) 98, 1999 Matthew D. Langston <langston@SLAC.Stanford.EDU>
dnl
dnl This macro is free software; you can redistribute it and/or modify it
dnl under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl
dnl This file is distributed in the hope that it will be useful, but
dnl WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this file; if not, write to:
dnl
dnl   Free Software Foundation, Inc.
dnl   Suite 330
dnl   59 Temple Place
dnl   Boston, MA 02111-1307, USA.
dnl ####################################################################
dnl @synopsis MDL_HAVE_OPENGL
dnl 
dnl Search for OpenGL.  We search first for Mesa (a GPL'ed version of
dnl OpenGL) before a vendor's version of OpenGL, unless we were
dnl specifically asked not to with `--with-Mesa=no' or `--without-Mesa'.
dnl
dnl The four "standard" OpenGL libraries are searched for: "-lGL",
dnl "-lGLU", "-lGLX" (or "-lMesaGL", "-lMesaGLU" as the case may be) and
dnl "-lglut".
dnl
dnl All of the libraries that are found (since "-lglut" or "-lGLX" might
dnl be missing) are added to the shell output variable "GL_LIBS", along
dnl with any other libraries that are necessary to successfully link an
dnl OpenGL application (e.g. the X11 libraries).  Care has been taken to
dnl make sure that all of the libraries in "GL_LIBS" are listed in the
dnl proper order.
dnl
dnl Additionally, the shell output variable "GL_CFLAGS" is set to any
dnl flags (e.g. "-I" flags) that are necessary to successfully compile
dnl an OpenGL application.
dnl
dnl The following shell variable (which are not output variables) are
dnl also set to either "yes" or "no" (depending on which libraries were
dnl found) to help you determine exactly what was found.
dnl
dnl   have_GL
dnl   have_GLU
dnl   have_GLX
dnl   have_glut
dnl
dnl A complete little toy "Automake `make distcheck'" package of how to
dnl use this macro is available at:
dnl
dnl   ftp://ftp.slac.stanford.edu/users/langston/autoconf/ac_opengl-0.01.tar.gz
dnl
dnl Please note that as the ac_opengl macro and the toy example evolves,
dnl the version number increases, so you may have to adjust the above
dnl URL accordingly.
dnl
dnl @version 0.01 $Id: acinclude.m4,v 1.1 2004/06/06 14:10:49 rpm Exp $
dnl @author Matthew D. Langston <langston@SLAC.Stanford.EDU>
dnl
dnl Patched by <rpm@xenomai.org> to suit RTAI's requirements.

AC_DEFUN(MDL_HAVE_OPENGL,
[
  AC_REQUIRE([AC_PATH_X])
  AC_REQUIRE([AC_PATH_XTRA])
  AC_CACHE_CHECK([for OpenGL], mdl_cv_have_OpenGL,
  [
dnl Check for Mesa first, unless we were asked not to.
    AC_ARG_ENABLE(Mesa, [], use_Mesa=$enableval, use_Mesa=yes)

    if test x"$use_Mesa" = xyes; then
       GL_search_list="MesaGL   GL"
      GLU_search_list="MesaGLU GLU"
      GLX_search_list="MesaGLX GLX"
    else
       GL_search_list="GL  MesaGL"
      GLU_search_list="GLU MesaGLU"
      GLX_search_list="GLX MesaGLX"
    fi      

    AC_LANG_SAVE
    AC_LANG_C

dnl If we are running under X11 then add in the appropriate libraries.
    if ! test x"$no_x" = xyes; then
dnl Add everything we need to compile and link X programs to GL_CFLAGS
dnl and GL_X_LIBS.
      GL_CFLAGS="$X_CFLAGS"
      GL_X_LIBS="$X_PRE_LIBS $X_LIBS -lX11 -lXext -lXmu -lXt -lXi $X_EXTRA_LIBS -lm"
    fi
    GL_save_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$GL_CFLAGS"

    GL_save_LIBS="$LIBS"
    LIBS="$GL_X_LIBS"

    # Save the "AC_MSG_RESULT file descriptor" to FD 8.
    exec 8>&AC_FD_MSG

    # Temporarily turn off AC_MSG_RESULT so that the user gets pretty
    # messages.
    exec AC_FD_MSG>/dev/null

    AC_SEARCH_LIBS(glAccum,          $GL_search_list, have_GL=yes,   have_GL=no)
    AC_SEARCH_LIBS(gluBeginCurve,   $GLU_search_list, have_GLU=yes,  have_GLU=no)
    AC_SEARCH_LIBS(glXChooseVisual, $GLX_search_list, have_GLX=yes,  have_GLX=no)
    AC_SEARCH_LIBS(glutInit,        glut,             have_glut=yes, have_glut=no)

    # Restore pretty messages.
    exec AC_FD_MSG>&8

    if test -n "$LIBS"; then
      mdl_cv_have_OpenGL=yes
      GL_LIBS="$LIBS"
      AC_SUBST(GL_CFLAGS)
      AC_SUBST(GL_LIBS)
    else
      mdl_cv_have_OpenGL=no
      GL_CFLAGS=
    fi

dnl Reset GL_X_LIBS regardless, since it was just a temporary variable
dnl and we don't want to be global namespace polluters.
    GL_X_LIBS=

    LIBS="$GL_save_LIBS"
    CPPFLAGS="$GL_save_CPPFLAGS"

    AC_LANG_RESTORE
  ])
])

AC_DEFUN(SC_PATH_EFLTK, [

dnl EFLTK dir passed to the command line overrides the Kconfig setting

AC_ARG_WITH(efltk, [  --with-efltk              directory containing EFLTK], with_efltk=${withval})

dnl Defaults to the Kconfig setting (CONFIG_RTAI_EFLTK_DIR) if unset

test x$with_efltk = x && with_efltk=$1

AC_MSG_CHECKING([for EFLTK])

AC_CACHE_VAL(ac_cv_efltk,[

    # First check to see if --with-efltk was specified.
    if test x"${with_efltk}" != x ; then
	if test -f "${with_efltk}/bin/efltk-config" ; then
	    ac_cv_efltk=`(cd ${with_efltk}; pwd)`
	else
	    AC_MSG_ERROR([${with_efltk}/bin directory doesn't contain efltk-config])
	fi
    fi

    # then check for a private EFLTK installation
    if test x"${ac_cv_efltk}" = x ; then
	for i in \
		../efltk \
		`ls -dr ../efltk* 2>/dev/null` \
		../../efltk \
		`ls -dr ../../efltk* 2>/dev/null` \
		../../../efltk \
		`ls -dr ../../../efltk* 2>/dev/null` ; do
	    if test -f "$i/bin/efltk-config" ; then
		ac_cv_efltk=`(cd $i; pwd)`
		break
	    fi
	done
    fi

    # check in a few common install locations
    if test x"${ac_cv_efltk}" = x ; then
	for i in ${prefix}/efltk* /usr/local/efltk* /usr/pkg/efltk* /usr \
		`ls -dr /usr/lib/efltk* 2>/dev/null` ; do
	    if test -f "$i/bin/efltk-config" ; then
		ac_cv_efltk=`(cd $i; pwd)`
		break
	    fi
	done
    fi

    # check in a few other private locations
    if test x"${ac_cv_efltk}" = x ; then
	for i in \
		${srcdir}/../efltk \
		`ls -dr ${srcdir}/../efltk* 2>/dev/null` ; do
	    if test -f "$i/bin/efltk-config" ; then
	    ac_cv_efltk=`(cd $i; pwd)`
	    break
	fi
	done
    fi
    ])

if test x"${ac_cv_efltk}" = x ; then
    EFLTK_DIR="# no EFLTK installation found"
    AC_MSG_ERROR(Can't find EFLTK-devel installation (missing bin/efltk-config?))
else
    EFLTK_DIR=${ac_cv_efltk}
    AC_MSG_RESULT(found $EFLTK_DIR)
fi

])

AC_DEFUN(SC_PATH_COMEDI, [

dnl COMEDI dir passed to the command line overrides the Kconfig setting

AC_ARG_WITH(comedi, [  --with-comedi              directory containing COMEDI], with_comedi=${withval})

dnl Defaults to the Kconfig setting (CONFIG_RTAI_COMEDI_DIR) if unset

test x$with_comedi = x && with_comedi=$1

AC_MSG_CHECKING([for COMEDI])

AC_CACHE_VAL(ac_cv_comedi,[

    # First check to see if --with-comedi was specified.
    if test x"${with_comedi}" != x ; then
	if test -f "${with_comedi}/include/linux/comedilib.h" ; then
	    ac_cv_comedi=`(cd ${with_comedi}; pwd)`
	else
	    AC_MSG_ERROR([${with_comedi}/include directory doesn't contain linux/comedilib.h])
	fi
    fi

    # then check for a private COMEDI installation
    if test x"${ac_cv_comedi}" = x ; then
	for i in \
		../comedi \
		`ls -dr ../comedi* 2>/dev/null` \
		../../comedi \
		`ls -dr ../../comedi* 2>/dev/null` \
		../../../comedi \
		`ls -dr ../../../comedi* 2>/dev/null` ; do
	    if test -f "$i/include/linux/comedilib.h" ; then
		ac_cv_comedi=`(cd $i; pwd)`
		break
	    fi
	done
    fi

    # check in a few common install locations
    if test x"${ac_cv_comedi}" = x ; then
	for i in ${prefix}/comedi* /usr/local/comedi* /usr/pkg/comedi* /usr \
		`ls -dr /usr/lib/comedi* 2>/dev/null` ; do
	    if test -f "$i/include/linux/comedilib.h" ; then
		ac_cv_comedi=`(cd $i; pwd)`
		break
	    fi
	done
    fi

    # check in a few other private locations
    if test x"${ac_cv_comedi}" = x ; then
	for i in \
		${srcdir}/../comedi \
		`ls -dr ${srcdir}/../comedi* 2>/dev/null` ; do
	    if test -f "$i/include/linux/comedilib.h" ; then
	    ac_cv_comedi=`(cd $i; pwd)`
	    break
	fi
	done
    fi
    ])

if test x"${ac_cv_comedi}" = x ; then
    COMEDI_DIR="# no COMEDI installation found"
    AC_MSG_ERROR(Can't find COMEDI installation ( missing linux/comedilib.h ) )
else
    COMEDI_DIR=${ac_cv_comedi}
    AC_MSG_RESULT(found $COMEDI_DIR)
fi

])
