#!/usr/local/bin/ksh93 -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD$

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)hotspare_remove_003_neg.ksh	1.3	09/06/22 SMI"
#
. $STF_SUITE/tests/hotspare/hotspare.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: hotspare_remove_003_neg
#
# DESCRIPTION:
# Executing 'zpool remove' command with bad options fails.
#
# STRATEGY:
# 1. Create an array of badly formed 'zpool remove' options.
# 2. Execute each element of the array.
# 3. Verify an error code is returned.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-06-19)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && \
		destroy_pool $TESTPOOL

	partition_cleanup
}


set -A args "" "-?" "-t fakepool" "-f fakepool" "-ev fakepool" "fakepool" \
	"$TESTPOOL" "fakepool ${devarray[0]}" "fakepool ${devarray[1]}"

log_assert "Executing 'zpool remove' with bad options fails"
log_onexit cleanup

set_devs
setup_hotspares
 
typeset -i i=0

while [[ $i -lt ${#args[*]} ]]; do

	log_mustnot $ZPOOL remove ${args[$i]}

	(( i = i + 1 ))
done

log_pass "'zpool remove' command with bad options failed as expected."
