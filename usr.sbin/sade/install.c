/*
 * The new sysinstall program.
 *
 * This is probably the last program in the `sysinstall' line - the next
 * generation being essentially a complete rewrite.
 *
 * $Id: install.c,v 1.16 1995/05/11 09:01:32 jkh Exp $
 *
 * Copyright (c) 1995
 *	Jordan Hubbard.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer, 
 *    verbatim and that no modifications are made prior to this 
 *    point in the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jordan Hubbard
 *	for the FreeBSD Project.
 * 4. The name of Jordan Hubbard or the FreeBSD project may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JORDAN HUBBARD ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JORDAN HUBBARD OR HIS PETS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, LIFE OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "sysinstall.h"
#include <sys/disklabel.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <unistd.h>

Boolean SystemWasInstalled;

static void	make_filesystems(void);
static void	cpio_extract(void);
static void	install_configuration_files(void);
static void	do_final_setup(void);

int
installCommit(char *str)
{
    extern u_char boot1[], boot2[];
    extern u_char mbr[], bteasy17[];
    u_char *mbrContents;
    Device **devs;
    int i;

    if (!getenv(DISK_PARTITIONED)) {
	msgConfirm("You need to partition your disk before you can proceed with\nthe installation.");

	return 0;
    }
    if (!getenv(DISK_LABELLED)) {
	msgConfirm("You need to assign disk labels before you can proceed with\nthe installation.");
	return 0;
    }
    if (!Dists) {
	msgConfirm("You haven't told me what distributions to load yet!\nPlease select a distribution from the Distributions menu.");
	return 0;
    }
    if (mediaVerifyStatus()) {
	msgConfirm("Please correct installation media problems and try again!");
	return 0;
    }
    if (msgYesNo("Last Chance!  Are you SURE you want continue the\ninstallation?  If you're running this on an existing system, we STRONGLY\nencourage you to make proper backups before doing this.\nWe take no responsibility for lost disk contents!"))
	return 0;
    dialog_clear();
    mbrContents = NULL;
    if (!msgYesNo("Would you like to install a boot manager?\n\nThis will allow you to easily select between other operating systems\non the first disk, or boot from a disk other than the first."))
	mbrContents = bteasy17;
    else {
	dialog_clear();
	if (!msgYesNo("Would you like to remove an existing boot manager?"))
	    mbrContents = mbr;
    }
    for (i = 0; Devices[i]; i++) {
	if (Devices[i]->type != DEVICE_TYPE_DISK)
	    continue;
	if (mbrContents) {
	    Set_Boot_Mgr((Disk *)Devices[i]->private, mbrContents);
	    mbrContents = NULL;
	}
	Set_Boot_Blocks((Disk *)Devices[i]->private, boot1, boot2);
	msgNotify("Writing partition information to drive %s",
		  Devices[i]->name);
	Write_Disk((Disk *)Devices[i]->private);
    }
    make_filesystems();
    cpio_extract();
    install_configuration_files();
    do_final_setup();
    return 1;
}

/* Go newfs and/or mount all the filesystems we've been asked to */
static void
make_filesystems(void)
{
    int i;
    Disk *disk;
    Chunk *c1;

    command_clear();
    for (i = 0; Devices[i]; i++) {
	if (Devices[i]->type != DEVICE_TYPE_DISK)
	    continue;

	disk = (Disk *)Devices[i]->private;
	if (!disk->chunks)
	    msgFatal("No chunk list found for %s!", disk->name);
	c1 = disk->chunks->part;
	while (c1) {
	    if (c1->type == freebsd) {
		Chunk *c2 = c1->part;

		while (c2) {
		    if (c2->type == part && c2->subtype != FS_SWAP &&
			c2->private) {
			PartInfo *tmp = (PartInfo *)c2->private;

			if (tmp->newfs)
			    command_shell_add(tmp->mountpoint,
					      "%s %s", tmp->newfs_cmd,
					      c2->name);
			if (strcmp(tmp->mountpoint, "/"))
			    command_func_add(tmp->mountpoint, Mkdir, NULL);
			command_func_add(tmp->mountpoint, Mount, c2->name);
		    }
		    c2 = c2->next;
		}
	    }
	    c1 = c1->next;
	}
    }
    command_sort();
    command_execute();
}

static void
cpio_extract(void)
{
    int i, j, zpid, cpid, pfd[2];
    extern int wait(int *status);

    while (CpioFD == -1) {
	msgConfirm("Please Insert CPIO floppy in floppy drive 0");
	CpioFD = open("/dev/rfd0", O_RDONLY);
    }
    msgNotify("Extracting contents of CPIO floppy...");
    pipe(pfd);
    zpid = fork();
    if (!zpid) {
	close(0); dup(CpioFD); close(CpioFD);
	close(1); dup(pfd[1]); close(pfd[1]);
	close(pfd[0]);
	i = execl("/stand/gunzip", "/stand/gunzip", 0);
	msgDebug("/stand/gunzip command returns %d status\n", i);
	exit(i);
    }
    cpid = fork();
    if (!cpid) {
	close(0); dup(pfd[0]); close(pfd[0]);
	close(CpioFD);
	close(pfd[1]);
	close(1); open("/dev/null", O_WRONLY);
	i = execl("/stand/cpio", "/stand/cpio", "-iduvm", 0);
	msgDebug("/stand/cpio command returns %d status\n", i);
	exit(i);
    }
    close(pfd[0]);
    close(pfd[1]);
    close(CpioFD);
    i = wait(&j);
    if (i < 0 || j)
	msgFatal("Pid %d, status %d, cpio=%d, gunzip=%d.\nerror:%s",
		 i, j, cpid, zpid, strerror(errno));
    i = wait(&j);
    if (i < 0 || j)
	msgFatal("Pid %d, status %d, cpio=%d, gunzip=%d.\nerror:%s",
		 i, j, cpid, zpid, strerror(errno));
}

static void
install_configuration_files(void)
{
}

static void
do_final_setup(void)
{
}
