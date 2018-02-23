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
# ident	"@(#)zfs_acl_compress_001_pos.ksh	1.2	07/01/09 SMI"
#

. $STF_SUITE/tests/acl/acl_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: zfs_acl_compress_001_pos
#
# DESCRIPTION:
#	The function verifies that compress will keep file attribute intact
#	after the file is compressed and uncompressed.
#
# STRATEGY:
#	1. In directory A, create several files and add attribute files for them
#	2. Save all files and their attribute files cksum value, then compress 
#	   all the files.
#	3. Move them to another directory B.
#	4. Uncompress them and calculate all the files and attribute files cksum
#	5. Verify all the cksum are identical
#	
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-06-01)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "both"

log_assert "Compress will keep file attribute intact after the file is " \
	"compressed and uncompressed"
log_onexit cleanup

test_requires ZFS_ACL ZFS_XATTR

set -A BEFORE_FCKSUM
set -A BEFORE_ACKSUM
set -A AFTER_FCKSUM
set -A AFTER_ACKSUM

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must create_files $TESTDIR

	log_must cksum_files $INI_DIR BEFORE_FCKSUM BEFORE_ACKSUM
	log_must usr_exec $COMPRESS $INI_DIR/*
	log_must usr_exec $MV $INI_DIR/* $TST_DIR
	log_must usr_exec $UNCOMPRESS $TST_DIR/*
	log_must cksum_files $TST_DIR AFTER_FCKSUM AFTER_ACKSUM

	log_must compare_cksum BEFORE_FCKSUM AFTER_FCKSUM
	log_must compare_cksum BEFORE_ACKSUM AFTER_ACKSUM

	log_must cleanup
done

log_pass "compress/uncompress test passed."
