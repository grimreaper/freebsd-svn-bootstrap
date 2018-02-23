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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_001_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_001_neg
#
# DESCRIPTION:
#
# zpool shows a usage message when run as a user
#
# STRATEGY:
# 1. Run the zpool command
# 2. Verify that a usage message is produced
#
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-27)
#
# __stc_assertion_end
#
################################################################################

function cleanup
{
	if [ -e $TMPDIR/zpool_001_neg.${TESTCASE_ID}.txt ]
	then
		$RM $TMPDIR/zpool_001_neg.${TESTCASE_ID}.txt
	fi
}

log_onexit cleanup
log_assert "zpool shows a usage message when run as a user"

run_unprivileged "$ZPOOL" > $TMPDIR/zpool_001_neg.${TESTCASE_ID}.txt 2>&1
log_must $GREP "usage: zpool command args" $TMPDIR/zpool_001_neg.${TESTCASE_ID}.txt

log_pass "zpool shows a usage message when run as a user"

