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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zfs_unshare_001_neg.ksh	1.2	08/02/27 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zfs_unshare_001_neg
#
# DESCRIPTION:
#
# zfs unshare returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to unshare a shared dataset
# 2. Verify the dataset is still shared
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

verify_runnable "global"

log_assert "zfs unshare returns an error when run as a user"

#  verify that the filesystem was shared initially
if not_shared $TESTDIR/shared 
then 
	log_fail "$TESTPOOL/$TESTFS/shared was not shared initially at all!"
fi

log_mustnot run_unprivileged "$ZFS unshare $TESTPOOL/$TESTFS/shared"

# now verify that the above command didn't do anything
if not_shared $TESTDIR/shared
then
	log_fail "$TESTPOOL/$TESTFS/shared was actually unshared!"
fi

log_pass "zfs unshare returns an error when run as a user"
