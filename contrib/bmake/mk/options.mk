# $Id: options.mk,v 1.7 2013/04/17 20:32:38 sjg Exp $
#
#	@(#) Copyright (c) 2012, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

# Inspired by FreeBSD bsd.own.mk, but intentionally simpler and more flexible.

# Options are normally listed in either OPTIONS_DEFAULT_{YES,NO}
# We convert these to ${OPTION}/{yes,no} in OPTIONS_DEFAULT_VALUES.
# We add the OPTIONS_DEFAULT_NO first so they take precedence.
# This allows override of an OPTIONS_DEFAULT_YES by adding it to
# OPTIONS_DEFAULT_NO or adding ${OPTION}/no to OPTIONS_DEFAULT_VALUES.
# An OPTIONS_DEFAULT_NO option can only be overridden by putting
# ${OPTION}/yes in OPTIONS_DEFAULT_VALUES.
# A makefile may set NO_* (or NO*) to indicate it cannot do something.
# User sets WITH_* and WITHOUT_* to indicate what they want.
# We set ${OPTION_PREFIX:UMK_}* which is then all we need care about.
OPTIONS_DEFAULT_VALUES += \
	${OPTIONS_DEFAULT_NO:O:u:S,$,/no,} \
	${OPTIONS_DEFAULT_YES:O:u:S,$,/yes,}

OPTION_PREFIX ?= MK_
.for o in ${OPTIONS_DEFAULT_VALUES:M*/*}
.if ${o:T:tl} == "no"
.if defined(WITH_${o:H}) && !defined(NO_${o:H}) && !defined(NO${o:H})
${OPTION_PREFIX}${o:H} ?= yes
.else
${OPTION_PREFIX}${o:H} ?= no
.endif
.else
.if defined(WITHOUT_${o:H}) || defined(NO_${o:H}) || defined(NO${o:H})
${OPTION_PREFIX}${o:H} ?= no
.else
${OPTION_PREFIX}${o:H} ?= yes
.endif
.endif
.endfor

# OPTIONS_DEFAULT_DEPENDENT += FOO_UTILS/FOO
# if neither WITH[OUT]_FOO_UTILS is set, use value of ${OPTION_PREFIX}FOO
.for o in ${OPTIONS_DEFAULT_DEPENDENT:M*/*:O:u}
.if defined(WITH_${o:H}) && !defined(NO_${o:H}) && !defined(NO${o:H})
${OPTION_PREFIX}${o:H} ?= yes
.elif defined(WITHOUT_${o:H}) || defined(NO_${o:H}) || defined(NO${o:H})
${OPTION_PREFIX}${o:H} ?= no
.else
${OPTION_PREFIX}${o:H} ?= ${${OPTION_PREFIX}${o:T}}
.endif
.endfor
