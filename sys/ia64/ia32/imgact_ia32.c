/*-
 * Copyright (c) 2000 David O'Brien
 * Copyright (c) 1995-1996 S�ren Schmidt
 * Copyright (c) 1996 Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mman.h>
#include <sys/namei.h>
#include <sys/pioctl.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/resourcevar.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <ia64/ia32/imgact_ia32.h>
#include <i386/include/psl.h>
#include <i386/include/segments.h>
#include <i386/include/specialreg.h>
#include <machine/md_var.h>

#define OLD_EI_BRAND	8

__ElfType(Brandinfo);
__ElfType(Auxargs);

#define IA32_USRSTACK	(3L*1024*1024*1024)
#define IA32_PS_STRINGS	(IA32_USRSTACK - sizeof(struct ia32_ps_strings))

static int elf32_check_header(const Elf32_Ehdr *hdr);
static int elf32_freebsd_fixup(register_t **stack_base,
    struct image_params *imgp);
static int elf32_load_file(struct proc *p, const char *file, u_long *addr,
    u_long *entry);
static int elf32_load_section(struct proc *p,
    struct vmspace *vmspace, struct vnode *vp,
    vm_offset_t offset, caddr_t vmaddr, size_t memsz, size_t filsz,
    vm_prot_t prot);
static int exec_elf32_imgact(struct image_params *imgp);
static int elf32_coredump(struct thread *td, struct vnode *vp, off_t limit);
static register_t *elf32_copyout_strings(struct image_params *imgp);
static void elf32_setregs(struct thread *td, u_long entry, u_long stack,
    u_long ps_strings);

static int elf32_trace = 0;
SYSCTL_INT(_debug, OID_AUTO, elf32_trace, CTLFLAG_RW, &elf32_trace, 0, "");

extern struct sysent ia32_sysent[];

static char ia32_sigcode[] = {
	0xff, 0x54, 0x24, 0x10,		/* call *SIGF_HANDLER(%esp) */
	0x8d, 0x44, 0x24, 0x14,		/* lea SIGF_UC(%esp),%eax */
	0x50,				/* pushl %eax */
	0xf7, 0x40, 0x54, 0x00, 0x00, 0x02, 0x02, /* testl $PSL_VM,UC_EFLAGS(%eax) */
	0x75, 0x03,			/* jne 9f */
	0x8e, 0x68, 0x14,		/* movl UC_GS(%eax),%gs */
	0xb8, 0x57, 0x01, 0x00, 0x00,	/* 9: movl $SYS_sigreturn,%eax */
	0x50,				/* pushl %eax */
	0xcd, 0x80,			/* int $0x80 */
	0xeb, 0xfe,			/* 0: jmp 0b */
	0, 0, 0, 0
};
static int ia32_szsigcode = sizeof(ia32_sigcode) & ~3;

struct sysentvec elf32_freebsd_sysvec = {
        SYS_MAXSYSCALL,
        ia32_sysent,
        0,
        0,
        0,
        0,
        0,
        0,
        elf32_freebsd_fixup,
        sendsig,
        ia32_sigcode,
        &ia32_szsigcode,
        0,
	"FreeBSD ELF",
	elf32_coredump,
	NULL,
	MINSIGSTKSZ,
	elf32_copyout_strings,
	elf32_setregs
};

static Elf32_Brandinfo freebsd_brand_info = {
						ELFOSABI_FREEBSD,
						"FreeBSD",
						"",
						"/usr/libexec/ld-elf.so.1",
						&elf32_freebsd_sysvec
					  };
static Elf32_Brandinfo *elf32_brand_list[MAX_BRANDS] = {
							&freebsd_brand_info,
							NULL, NULL, NULL,
							NULL, NULL, NULL, NULL
						    };

#if 0

int
elf32_insert_brand_entry(Elf32_Brandinfo *entry)
{
	int i;

	for (i=1; i<MAX_BRANDS; i++) {
		if (elf32_brand_list[i] == NULL) {
			elf32_brand_list[i] = entry;
			break;
		}
	}
	if (i == MAX_BRANDS)
		return -1;
	return 0;
}

int
elf32_remove_brand_entry(Elf32_Brandinfo *entry)
{
	int i;

	for (i=1; i<MAX_BRANDS; i++) {
		if (elf32_brand_list[i] == entry) {
			elf32_brand_list[i] = NULL;
			break;
		}
	}
	if (i == MAX_BRANDS)
		return -1;
	return 0;
}

int
elf32_brand_inuse(Elf32_Brandinfo *entry)
{
	struct proc *p;
	int rval = FALSE;

	sx_slock(&allproc_lock);
	LIST_FOREACH(p, &allproc, p_list) {
		if (p->p_sysent == entry->sysvec) {
			rval = TRUE;
			break;
		}
	}
	sx_sunlock(&allproc_lock);

	return (rval);
}

#endif

static int
elf32_check_header(const Elf32_Ehdr *hdr)
{
	if (!IS_ELF(*hdr) ||
	    hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT)
		return ENOEXEC;

	if (!ELF_MACHINE_OK(hdr->e_machine))
		return ENOEXEC;

	if (hdr->e_version != ELF_TARG_VER)
		return ENOEXEC;
	
	return 0;
}

#define PAGE4K_SHIFT	12
#define PAGE4K_MASK	((1 << PAGE4K_SHIFT)-1)
#define round_page4k(x)	(((x) + PAGE4K_MASK) & ~(PAGE4K_MASK))
#define trunc_page4k(x)	((x) & ~(PAGE4K_MASK))

static int
elf32_map_partial(vm_map_t map, vm_object_t object, vm_ooffset_t offset,
		  vm_offset_t start, vm_offset_t end, vm_prot_t prot,
		  vm_prot_t max)
{
	int error, rv;
	vm_offset_t off;
	vm_offset_t data_buf = 0;

	/*
	 * Create the page if it doesn't exist yet. Ignore errors.
	 */
	vm_map_lock(map);
	vm_map_insert(map, NULL, 0, trunc_page(start), round_page(end),
		      max, max, 0);
	vm_map_unlock(map);

	/*
	 * Find the page from the underlying object.
	 */
	if (object) {
		vm_object_reference(object);
		rv = vm_map_find(exec_map,
				 object,
				 trunc_page(offset),
				 &data_buf,
				 PAGE_SIZE,
				 TRUE,
				 VM_PROT_READ,
				 VM_PROT_ALL,
				 MAP_COPY_ON_WRITE | MAP_PREFAULT_PARTIAL);
		if (rv != KERN_SUCCESS) {
			vm_object_deallocate(object);
			return rv;
		}

		off = offset - trunc_page(offset);
		error = copyout((caddr_t)data_buf+off, (caddr_t)start, end - start);
		vm_map_remove(exec_map, data_buf, data_buf + PAGE_SIZE);
		if (error) {
			return KERN_FAILURE;
		}
	}

	return KERN_SUCCESS;
}

static int
elf32_map_insert(vm_map_t map, vm_object_t object, vm_ooffset_t offset,
		 vm_offset_t start, vm_offset_t end, vm_prot_t prot,
		 vm_prot_t max, int cow)
{
	int rv;

	if (start != trunc_page(start)) {
		rv = elf32_map_partial(map, object, offset,
				       start, round_page(start), prot, max);
		if (rv)
			return rv;
		start = round_page(start);
		offset = round_page(offset);
	}
	if (end != round_page(end)) {
		rv = elf32_map_partial(map, object, offset,
				       trunc_page(end), end, prot, max);
		if (rv)
			return rv;
		end = trunc_page(end);
	}
	if (end > start) {
		vm_map_lock(map);
		rv =  vm_map_insert(map, object, offset, start, end,
				    prot, max, cow);
		vm_map_unlock(map);
		return rv;
	} else {
		return KERN_SUCCESS;
	}
}

static int
elf32_load_section(struct proc *p, struct vmspace *vmspace, struct vnode *vp, vm_offset_t offset, caddr_t vmaddr, size_t memsz, size_t filsz, vm_prot_t prot)
{
	size_t map_len;
	vm_offset_t map_addr;
	int error, rv;
	size_t copy_len;
	vm_object_t object;
	vm_offset_t file_addr;
	vm_offset_t data_buf = 0;

	GIANT_REQUIRED;

	VOP_GETVOBJECT(vp, &object);
	error = 0;

	/*
	 * It's necessary to fail if the filsz + offset taken from the
	 * header is greater than the actual file pager object's size.
	 * If we were to allow this, then the vm_map_find() below would
	 * walk right off the end of the file object and into the ether.
	 *
	 * While I'm here, might as well check for something else that
	 * is invalid: filsz cannot be greater than memsz.
	 */
	if ((off_t)filsz + offset > object->un_pager.vnp.vnp_size ||
	    filsz > memsz) {
		uprintf("elf32_load_section: truncated ELF file\n");
		return (ENOEXEC);
	}

	map_addr = trunc_page4k((vm_offset_t)vmaddr);
	file_addr = trunc_page4k(offset);

	/*
	 * We have two choices.  We can either clear the data in the last page
	 * of an oversized mapping, or we can start the anon mapping a page
	 * early and copy the initialized data into that first page.  We
	 * choose the second..
	 */
	if (memsz > filsz)
		map_len = trunc_page4k(offset+filsz) - file_addr;
	else
		map_len = round_page4k(offset+filsz) - file_addr;

	if (map_len != 0) {
		vm_object_reference(object);
		rv = elf32_map_insert(&vmspace->vm_map,
				      object,
				      file_addr,	/* file offset */
				      map_addr,		/* virtual start */
				      map_addr + map_len,/* virtual end */
				      prot,
				      VM_PROT_ALL,
				      MAP_COPY_ON_WRITE | MAP_PREFAULT);
		if (rv != KERN_SUCCESS) {
			vm_object_deallocate(object);
			return EINVAL;
		}

		/* we can stop now if we've covered it all */
		if (memsz == filsz) {
			return 0;
		}
	}


	/*
	 * We have to get the remaining bit of the file into the first part
	 * of the oversized map segment.  This is normally because the .data
	 * segment in the file is extended to provide bss.  It's a neat idea
	 * to try and save a page, but it's a pain in the behind to implement.
	 */
	copy_len = (offset + filsz) - trunc_page4k(offset + filsz);
	map_addr = trunc_page4k((vm_offset_t)vmaddr + filsz);
	map_len = round_page4k((vm_offset_t)vmaddr + memsz) - map_addr;

	/* This had damn well better be true! */
        if (map_len != 0) {
		rv = elf32_map_insert(&vmspace->vm_map, NULL, 0,
					map_addr, map_addr + map_len,
					VM_PROT_ALL, VM_PROT_ALL, 0);
		if (rv != KERN_SUCCESS) {
			return EINVAL; 
		}
	}

	if (copy_len != 0) {
		vm_offset_t off;
		vm_object_reference(object);
		rv = vm_map_find(exec_map,
				 object, 
				 trunc_page(offset + filsz),
				 &data_buf,
				 PAGE_SIZE,
				 TRUE,
				 VM_PROT_READ,
				 VM_PROT_ALL,
				 MAP_COPY_ON_WRITE | MAP_PREFAULT_PARTIAL);
		if (rv != KERN_SUCCESS) {
			vm_object_deallocate(object);
			return EINVAL;
		}

		/* send the page fragment to user space */
		off = trunc_page4k(offset + filsz) - trunc_page(offset + filsz);
		error = copyout((caddr_t)data_buf+off, (caddr_t)map_addr, copy_len);
		vm_map_remove(exec_map, data_buf, data_buf + PAGE_SIZE);
		if (error) {
			return (error);
		}
	}

	/*
	 * set it to the specified protection
	 */
	vm_map_protect(&vmspace->vm_map, map_addr, map_addr + map_len,  prot,
		       FALSE);

	return error;
}

/*
 * Load the file "file" into memory.  It may be either a shared object
 * or an executable.
 *
 * The "addr" reference parameter is in/out.  On entry, it specifies
 * the address where a shared object should be loaded.  If the file is
 * an executable, this value is ignored.  On exit, "addr" specifies
 * where the file was actually loaded.
 *
 * The "entry" reference parameter is out only.  On exit, it specifies
 * the entry point for the loaded file.
 */
static int
elf32_load_file(struct proc *p, const char *file, u_long *addr, u_long *entry)
{
	struct {
		struct nameidata nd;
		struct vattr attr;
		struct image_params image_params;
	} *tempdata;
	const Elf32_Ehdr *hdr = NULL;
	const Elf32_Phdr *phdr = NULL;
	struct nameidata *nd;
	struct vmspace *vmspace = p->p_vmspace;
	struct vattr *attr;
	struct image_params *imgp;
	vm_prot_t prot;
	u_long rbase;
	u_long base_addr = 0;
	int error, i, numsegs;

	if (curthread->td_proc != p)
		panic("elf32_load_file - thread");	/* XXXKSE DIAGNOSTIC */

	tempdata = malloc(sizeof(*tempdata), M_TEMP, M_WAITOK);
	nd = &tempdata->nd;
	attr = &tempdata->attr;
	imgp = &tempdata->image_params;

	/*
	 * Initialize part of the common data
	 */
	imgp->proc = p;
	imgp->uap = NULL;
	imgp->attr = attr;
	imgp->firstpage = NULL;
	imgp->image_header = (char *)kmem_alloc_wait(exec_map, PAGE_SIZE);

	if (imgp->image_header == NULL) {
		nd->ni_vp = NULL;
		error = ENOMEM;
		goto fail;
	}

	/* XXXKSE */
        NDINIT(nd, LOOKUP, LOCKLEAF|FOLLOW, UIO_SYSSPACE, file, curthread);   
			 
	if ((error = namei(nd)) != 0) {
		nd->ni_vp = NULL;
		goto fail;
	}
	NDFREE(nd, NDF_ONLY_PNBUF);
	imgp->vp = nd->ni_vp;

	/*
	 * Check permissions, modes, uid, etc on the file, and "open" it.
	 */
	error = exec_check_permissions(imgp);
	if (error) {
		VOP_UNLOCK(nd->ni_vp, 0, curthread); /* XXXKSE */
		goto fail;
	}

	error = exec_map_first_page(imgp);
	/*
	 * Also make certain that the interpreter stays the same, so set
	 * its VTEXT flag, too.
	 */
	if (error == 0)
		nd->ni_vp->v_flag |= VTEXT;
	VOP_UNLOCK(nd->ni_vp, 0, curthread); /* XXXKSE */
	if (error)
                goto fail;

	hdr = (const Elf32_Ehdr *)imgp->image_header;
	if ((error = elf32_check_header(hdr)) != 0)
		goto fail;
	if (hdr->e_type == ET_DYN)
		rbase = *addr;
	else if (hdr->e_type == ET_EXEC)
		rbase = 0;
	else {
		error = ENOEXEC;
		goto fail;
	}

	/* Only support headers that fit within first page for now */
	if ((hdr->e_phoff > PAGE_SIZE) ||
	    (hdr->e_phoff + hdr->e_phentsize * hdr->e_phnum) > PAGE_SIZE) {
		error = ENOEXEC;
		goto fail;
	}

	phdr = (const Elf32_Phdr *)(imgp->image_header + hdr->e_phoff);

	for (i = 0, numsegs = 0; i < hdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {	/* Loadable segment */
			prot = 0;
			if (phdr[i].p_flags & PF_X)
  				prot |= VM_PROT_EXECUTE;
			if (phdr[i].p_flags & PF_W)
  				prot |= VM_PROT_WRITE;
			if (phdr[i].p_flags & PF_R)
  				prot |= VM_PROT_READ;

			if ((error = elf32_load_section(p, vmspace, nd->ni_vp,
  						     phdr[i].p_offset,
  						     (caddr_t)(uintptr_t)phdr[i].p_vaddr +
							rbase,
  						     phdr[i].p_memsz,
  						     phdr[i].p_filesz, prot)) != 0)
				goto fail;
			/*
			 * Establish the base address if this is the
			 * first segment.
			 */
			if (numsegs == 0)
  				base_addr = trunc_page(phdr[i].p_vaddr + rbase);
			numsegs++;
		}
	}
	*addr = base_addr;
	*entry=(unsigned long)hdr->e_entry + rbase;

fail:
	if (imgp->firstpage)
		exec_unmap_first_page(imgp);
	if (imgp->image_header)
		kmem_free_wakeup(exec_map, (vm_offset_t)imgp->image_header,
			PAGE_SIZE);
	if (nd->ni_vp)
		vrele(nd->ni_vp);

	free(tempdata, M_TEMP);

	return error;
}

/*
 * non static, as it can be overridden by start_init()
 */
#ifdef __ia64__
int fallback_elf32_brand = ELFOSABI_FREEBSD;
#else
int fallback_elf32_brand = -1;
#endif
SYSCTL_INT(_kern, OID_AUTO, fallback_elf32_brand, CTLFLAG_RW,
		&fallback_elf32_brand, -1,
		"ELF brand of last resort");

static int
exec_elf32_imgact(struct image_params *imgp)
{
	const Elf32_Ehdr *hdr = (const Elf32_Ehdr *) imgp->image_header;
	const Elf32_Phdr *phdr;
	Elf32_Auxargs *elf32_auxargs = NULL;
	struct vmspace *vmspace;
	vm_prot_t prot;
	u_long text_size = 0, data_size = 0;
	u_long text_addr = 0, data_addr = 0;
	u_long addr, entry = 0, proghdr = 0;
	int error, i;
	const char *interp = NULL;
	Elf32_Brandinfo *brand_info;
	char *path;

	GIANT_REQUIRED;

	/*
	 * Do we have a valid ELF header ?
	 */
	if (elf32_check_header(hdr) != 0 || hdr->e_type != ET_EXEC)
		return -1;

	/*
	 * From here on down, we return an errno, not -1, as we've
	 * detected an ELF file.
	 */

	if ((hdr->e_phoff > PAGE_SIZE) ||
	    (hdr->e_phoff + hdr->e_phentsize * hdr->e_phnum) > PAGE_SIZE) {
		/* Only support headers in first page for now */
		return ENOEXEC;
	}
	phdr = (const Elf32_Phdr*)(imgp->image_header + hdr->e_phoff);
	
	/*
	 * From this point on, we may have resources that need to be freed.
	 */

	/*
	 * Yeah, I'm paranoid.  There is every reason in the world to get
	 * VTEXT now since from here on out, there are places we can have
	 * a context switch.  Better safe than sorry; I really don't want
	 * the file to change while it's being loaded.
	 */
	mtx_lock(&imgp->vp->v_interlock);
	imgp->vp->v_flag |= VTEXT;
	mtx_unlock(&imgp->vp->v_interlock);

	if ((error = exec_extract_strings(imgp)) != 0)
		goto fail;

	exec_new_vmspace(imgp, IA32_USRSTACK);

	vmspace = imgp->proc->p_vmspace;

	for (i = 0; i < hdr->e_phnum; i++) {
		switch(phdr[i].p_type) {

		case PT_LOAD:	/* Loadable segment */
			prot = 0;
			if (phdr[i].p_flags & PF_X)
  				prot |= VM_PROT_EXECUTE;
			if (phdr[i].p_flags & PF_W)
  				prot |= VM_PROT_WRITE;
			if (phdr[i].p_flags & PF_R)
  				prot |= VM_PROT_READ;

			if ((error = elf32_load_section(imgp->proc,
						     vmspace, imgp->vp,
  						     phdr[i].p_offset,
  						     (caddr_t)(uintptr_t)phdr[i].p_vaddr,
  						     phdr[i].p_memsz,
  						     phdr[i].p_filesz, prot)) != 0)
  				goto fail;

			/*
			 * Is this .text or .data ??
			 *
			 * We only handle one each of those yet XXX
			 */
			if (hdr->e_entry >= phdr[i].p_vaddr &&
			hdr->e_entry <(phdr[i].p_vaddr+phdr[i].p_memsz)) {
  				text_addr = trunc_page(phdr[i].p_vaddr);
  				text_size = round_page(phdr[i].p_memsz +
						       phdr[i].p_vaddr -
						       text_addr);
				entry = (u_long)hdr->e_entry;
			} else {
  				data_addr = trunc_page(phdr[i].p_vaddr);
  				data_size = round_page(phdr[i].p_memsz +
						       phdr[i].p_vaddr -
						       data_addr);
			}
			break;
	  	case PT_INTERP:	/* Path to interpreter */
			if (phdr[i].p_filesz > MAXPATHLEN ||
			    phdr[i].p_offset + phdr[i].p_filesz > PAGE_SIZE) {
				error = ENOEXEC;
				goto fail;
			}
			interp = imgp->image_header + phdr[i].p_offset;
			break;
		case PT_PHDR: 	/* Program header table info */
			proghdr = phdr[i].p_vaddr;
			break;
		default:
			break;
		}
	}

	vmspace->vm_tsize = text_size >> PAGE_SHIFT;
	vmspace->vm_taddr = (caddr_t)(uintptr_t)text_addr;
	vmspace->vm_dsize = data_size >> PAGE_SHIFT;
	vmspace->vm_daddr = (caddr_t)(uintptr_t)data_addr;

	addr = ELF_RTLD_ADDR(vmspace);

	imgp->entry_addr = entry;

	brand_info = NULL;

	/* We support three types of branding -- (1) the ELF EI_OSABI field
	 * that SCO added to the ELF spec, (2) FreeBSD 3.x's traditional string
	 * branding w/in the ELF header, and (3) path of the `interp_path'
	 * field.  We should also look for an ".note.ABI-tag" ELF section now
	 * in all Linux ELF binaries, FreeBSD 4.1+, and some NetBSD ones.
	 */

	/* If the executable has a brand, search for it in the brand list. */
	if (brand_info == NULL) {
		for (i = 0;  i < MAX_BRANDS;  i++) {
			Elf32_Brandinfo *bi = elf32_brand_list[i];

			if (bi != NULL && 
			    (hdr->e_ident[EI_OSABI] == bi->brand
			    || 0 == 
			    strncmp((const char *)&hdr->e_ident[OLD_EI_BRAND], 
			    bi->compat_3_brand, strlen(bi->compat_3_brand)))) {
				brand_info = bi;
				break;
			}
		}
	}

	/* Lacking a known brand, search for a recognized interpreter. */
	if (brand_info == NULL && interp != NULL) {
		for (i = 0;  i < MAX_BRANDS;  i++) {
			Elf32_Brandinfo *bi = elf32_brand_list[i];

			if (bi != NULL &&
			    strcmp(interp, bi->interp_path) == 0) {
				brand_info = bi;
				break;
			}
		}
	}

	/* Lacking a recognized interpreter, try the default brand */
	if (brand_info == NULL) {
		for (i = 0; i < MAX_BRANDS; i++) {
			Elf32_Brandinfo *bi = elf32_brand_list[i];

			if (bi != NULL && fallback_elf32_brand == bi->brand) {
				brand_info = bi;
				break;
			}
		}
	}

	if (brand_info == NULL) {
		uprintf("ELF binary type \"%u\" not known.\n",
		    hdr->e_ident[EI_OSABI]);
		error = ENOEXEC;
		goto fail;
	}

	imgp->proc->p_sysent = brand_info->sysvec;
	if (interp != NULL) {
		path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	        snprintf(path, MAXPATHLEN, "%s%s",
			 brand_info->emul_path, interp);
		if ((error = elf32_load_file(imgp->proc, path, &addr,
					   &imgp->entry_addr)) != 0) {
		        if ((error = elf32_load_file(imgp->proc, interp, &addr,
						   &imgp->entry_addr)) != 0) {
			        uprintf("ELF interpreter %s not found\n", path);
				free(path, M_TEMP);
				goto fail;
			}
                }
		free(path, M_TEMP);
	}

	/*
	 * Construct auxargs table (used by the fixup routine)
	 */
	elf32_auxargs = malloc(sizeof(Elf32_Auxargs), M_TEMP, M_WAITOK);
	elf32_auxargs->execfd = -1;
	elf32_auxargs->phdr = proghdr;
	elf32_auxargs->phent = hdr->e_phentsize;
	elf32_auxargs->phnum = hdr->e_phnum;
	elf32_auxargs->pagesz = PAGE_SIZE;
	elf32_auxargs->base = addr;
	elf32_auxargs->flags = 0;
	elf32_auxargs->entry = entry;
	elf32_auxargs->trace = elf32_trace;

	imgp->auxargs = elf32_auxargs;
	imgp->interpreted = 0;

fail:
	return error;
}

#define AUXARGS32_ENTRY(pos, id, val) { \
	suword32(pos++, id); \
	suword32(pos++, val); \
}

static int
elf32_freebsd_fixup(register_t **stack_base, struct image_params *imgp)
{
	Elf32_Auxargs *args = (Elf32_Auxargs *)imgp->auxargs;
	u_int32_t *pos;

	pos = *(u_int32_t **)stack_base + (imgp->argc + imgp->envc + 2);

	if (args->trace) {
		AUXARGS32_ENTRY(pos, AT_DEBUG, 1);
	}
	if (args->execfd != -1) {
		AUXARGS32_ENTRY(pos, AT_EXECFD, args->execfd);
	}
	AUXARGS32_ENTRY(pos, AT_PHDR, args->phdr);
	AUXARGS32_ENTRY(pos, AT_PHENT, args->phent);
	AUXARGS32_ENTRY(pos, AT_PHNUM, args->phnum);
	AUXARGS32_ENTRY(pos, AT_PAGESZ, args->pagesz);
	AUXARGS32_ENTRY(pos, AT_FLAGS, args->flags);
	AUXARGS32_ENTRY(pos, AT_ENTRY, args->entry);
	AUXARGS32_ENTRY(pos, AT_BASE, args->base);
	AUXARGS32_ENTRY(pos, AT_NULL, 0);

	free(imgp->auxargs, M_TEMP);
	imgp->auxargs = NULL;

	(*(u_int32_t **)stack_base)--;
	suword32(*stack_base, (long) imgp->argc);
	return 0;
} 

/*
 * Code for generating ELF core dumps.
 */

typedef void (*segment_callback)(vm_map_entry_t, void *);

/* Closure for cb_put_phdr(). */
struct phdr_closure {
	Elf32_Phdr *phdr;		/* Program header to fill in */
	Elf32_Off offset;		/* Offset of segment in core file */
};

/* Closure for cb_size_segment(). */
struct sseg_closure {
	int count;		/* Count of writable segments. */
	size_t size;		/* Total size of all writable segments. */
};

static void cb_put_phdr(vm_map_entry_t, void *);
static void cb_size_segment(vm_map_entry_t, void *);
static void each_writable_segment(struct proc *, segment_callback, void *);
static int elf32_corehdr(struct thread *, struct vnode *, struct ucred *,
    int, void *, size_t);
static void elf32_puthdr(struct proc *, void *, size_t *,
    const prstatus_t *, const prfpregset_t *, const prpsinfo_t *, int);
static void elf32_putnote(void *, size_t *, const char *, int,
    const void *, size_t);

extern int osreldate;

static int
elf32_coredump(td, vp, limit)
	struct thread *td;
	register struct vnode *vp;
	off_t limit;
{
	register struct proc *p = td->td_proc;
	register struct ucred *cred = td->td_ucred;
	int error = 0;
	struct sseg_closure seginfo;
	void *hdr;
	size_t hdrsize;

	/* Size the program segments. */
	seginfo.count = 0;
	seginfo.size = 0;
	each_writable_segment(p, cb_size_segment, &seginfo);

	/*
	 * Calculate the size of the core file header area by making
	 * a dry run of generating it.  Nothing is written, but the
	 * size is calculated.
	 */
	hdrsize = 0;
	elf32_puthdr((struct proc *)NULL, (void *)NULL, &hdrsize,
	    (const prstatus_t *)NULL, (const prfpregset_t *)NULL,
	    (const prpsinfo_t *)NULL, seginfo.count);

	if (hdrsize + seginfo.size >= limit)
		return (EFAULT);

	/*
	 * Allocate memory for building the header, fill it up,
	 * and write it out.
	 */
	hdr = malloc(hdrsize, M_TEMP, M_WAITOK);
	if (hdr == NULL) {
		return EINVAL;
	}
	error = elf32_corehdr(td, vp, cred, seginfo.count, hdr, hdrsize);

	/* Write the contents of all of the writable segments. */
	if (error == 0) {
		Elf32_Phdr *php;
		off_t offset;
		int i;

		php = (Elf32_Phdr *)((char *)hdr + sizeof(Elf32_Ehdr)) + 1;
		offset = hdrsize;
		for (i = 0;  i < seginfo.count;  i++) {
			error = vn_rdwr_inchunks(UIO_WRITE, vp, 
			    (caddr_t)(uintptr_t)php->p_vaddr,
			    php->p_filesz, offset, UIO_USERSPACE,
			    IO_UNIT | IO_DIRECT, cred, (int *)NULL, curthread); /* XXXKSE */
			if (error != 0)
				break;
			offset += php->p_filesz;
			php++;
		}
	}
	free(hdr, M_TEMP);
	
	return error;
}

/*
 * A callback for each_writable_segment() to write out the segment's
 * program header entry.
 */
static void
cb_put_phdr(entry, closure)
	vm_map_entry_t entry;
	void *closure;
{
	struct phdr_closure *phc = (struct phdr_closure *)closure;
	Elf32_Phdr *phdr = phc->phdr;

	phc->offset = round_page(phc->offset);

	phdr->p_type = PT_LOAD;
	phdr->p_offset = phc->offset;
	phdr->p_vaddr = entry->start;
	phdr->p_paddr = 0;
	phdr->p_filesz = phdr->p_memsz = entry->end - entry->start;
	phdr->p_align = PAGE_SIZE;
	phdr->p_flags = 0;
	if (entry->protection & VM_PROT_READ)
		phdr->p_flags |= PF_R;
	if (entry->protection & VM_PROT_WRITE)
		phdr->p_flags |= PF_W;
	if (entry->protection & VM_PROT_EXECUTE)
		phdr->p_flags |= PF_X;

	phc->offset += phdr->p_filesz;
	phc->phdr++;
}

/*
 * A callback for each_writable_segment() to gather information about
 * the number of segments and their total size.
 */
static void
cb_size_segment(entry, closure)
	vm_map_entry_t entry;
	void *closure;
{
	struct sseg_closure *ssc = (struct sseg_closure *)closure;

	ssc->count++;
	ssc->size += entry->end - entry->start;
}

/*
 * For each writable segment in the process's memory map, call the given
 * function with a pointer to the map entry and some arbitrary
 * caller-supplied data.
 */
static void
each_writable_segment(p, func, closure)
	struct proc *p;
	segment_callback func;
	void *closure;
{
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;

	for (entry = map->header.next;  entry != &map->header;
	    entry = entry->next) {
		vm_object_t obj;

		if ((entry->eflags & MAP_ENTRY_IS_SUB_MAP) ||
		    (entry->protection & (VM_PROT_READ|VM_PROT_WRITE)) !=
		    (VM_PROT_READ|VM_PROT_WRITE))
			continue;

		/*
		** Dont include memory segment in the coredump if
		** MAP_NOCORE is set in mmap(2) or MADV_NOCORE in
		** madvise(2).
		*/
		if (entry->eflags & MAP_ENTRY_NOCOREDUMP)
			continue;

		if ((obj = entry->object.vm_object) == NULL)
			continue;

		/* Find the deepest backing object. */
		while (obj->backing_object != NULL)
			obj = obj->backing_object;

		/* Ignore memory-mapped devices and such things. */
		if (obj->type != OBJT_DEFAULT &&
		    obj->type != OBJT_SWAP &&
		    obj->type != OBJT_VNODE)
			continue;

		(*func)(entry, closure);
	}
}

/*
 * Write the core file header to the file, including padding up to
 * the page boundary.
 */
static int
elf32_corehdr(td, vp, cred, numsegs, hdr, hdrsize)
	struct thread *td;
	struct vnode *vp;
	struct ucred *cred;
	int numsegs;
	size_t hdrsize;
	void *hdr;
{
	struct {
		prstatus_t status;
		prfpregset_t fpregset;
		prpsinfo_t psinfo;
	} *tempdata;
	struct proc *p = td->td_proc;
	size_t off;
	prstatus_t *status;
	prfpregset_t *fpregset;
	prpsinfo_t *psinfo;

	tempdata = malloc(sizeof(*tempdata), M_TEMP, M_ZERO | M_WAITOK);
	status = &tempdata->status;
	fpregset = &tempdata->fpregset;
	psinfo = &tempdata->psinfo;

	/* Gather the information for the header. */
	status->pr_version = PRSTATUS_VERSION;
	status->pr_statussz = sizeof(prstatus_t);
	status->pr_gregsetsz = sizeof(gregset_t);
	status->pr_fpregsetsz = sizeof(fpregset_t);
	status->pr_osreldate = osreldate;
	status->pr_cursig = p->p_sig;
	status->pr_pid = p->p_pid;
	fill_regs(td, &status->pr_reg);

	fill_fpregs(td, fpregset);

	psinfo->pr_version = PRPSINFO_VERSION;
	psinfo->pr_psinfosz = sizeof(prpsinfo_t);
	strncpy(psinfo->pr_fname, p->p_comm, sizeof(psinfo->pr_fname) - 1);

	/* XXX - We don't fill in the command line arguments properly yet. */
	strncpy(psinfo->pr_psargs, p->p_comm, PRARGSZ);

	/* Fill in the header. */
	bzero(hdr, hdrsize);
	off = 0;
	elf32_puthdr(p, hdr, &off, status, fpregset, psinfo, numsegs);

	free(tempdata, M_TEMP);

	/* Write it to the core file. */
	return vn_rdwr_inchunks(UIO_WRITE, vp, hdr, hdrsize, (off_t)0,
	    UIO_SYSSPACE, IO_UNIT | IO_DIRECT, cred, NULL, td); /* XXXKSE */
}

static void
elf32_puthdr(struct proc *p, void *dst, size_t *off, const prstatus_t *status,
    const prfpregset_t *fpregset, const prpsinfo_t *psinfo, int numsegs)
{
	size_t ehoff;
	size_t phoff;
	size_t noteoff;
	size_t notesz;

	ehoff = *off;
	*off += sizeof(Elf32_Ehdr);

	phoff = *off;
	*off += (numsegs + 1) * sizeof(Elf32_Phdr);

	noteoff = *off;
	elf32_putnote(dst, off, "FreeBSD", NT_PRSTATUS, status,
	    sizeof *status);
	elf32_putnote(dst, off, "FreeBSD", NT_FPREGSET, fpregset,
	    sizeof *fpregset);
	elf32_putnote(dst, off, "FreeBSD", NT_PRPSINFO, psinfo,
	    sizeof *psinfo);
	notesz = *off - noteoff;

	/* Align up to a page boundary for the program segments. */
	*off = round_page(*off);

	if (dst != NULL) {
		Elf32_Ehdr *ehdr;
		Elf32_Phdr *phdr;
		struct phdr_closure phc;

		/*
		 * Fill in the ELF header.
		 */
		ehdr = (Elf32_Ehdr *)((char *)dst + ehoff);
		ehdr->e_ident[EI_MAG0] = ELFMAG0;
		ehdr->e_ident[EI_MAG1] = ELFMAG1;
		ehdr->e_ident[EI_MAG2] = ELFMAG2;
		ehdr->e_ident[EI_MAG3] = ELFMAG3;
		ehdr->e_ident[EI_CLASS] = ELF_CLASS;
		ehdr->e_ident[EI_DATA] = ELF_DATA;
		ehdr->e_ident[EI_VERSION] = EV_CURRENT;
		ehdr->e_ident[EI_OSABI] = ELFOSABI_FREEBSD;
		ehdr->e_ident[EI_ABIVERSION] = 0;
		ehdr->e_ident[EI_PAD] = 0;
		ehdr->e_type = ET_CORE;
		ehdr->e_machine = ELF_ARCH;
		ehdr->e_version = EV_CURRENT;
		ehdr->e_entry = 0;
		ehdr->e_phoff = phoff;
		ehdr->e_flags = 0;
		ehdr->e_ehsize = sizeof(Elf32_Ehdr);
		ehdr->e_phentsize = sizeof(Elf32_Phdr);
		ehdr->e_phnum = numsegs + 1;
		ehdr->e_shentsize = sizeof(Elf32_Shdr);
		ehdr->e_shnum = 0;
		ehdr->e_shstrndx = SHN_UNDEF;

		/*
		 * Fill in the program header entries.
		 */
		phdr = (Elf32_Phdr *)((char *)dst + phoff);

		/* The note segement. */
		phdr->p_type = PT_NOTE;
		phdr->p_offset = noteoff;
		phdr->p_vaddr = 0;
		phdr->p_paddr = 0;
		phdr->p_filesz = notesz;
		phdr->p_memsz = 0;
		phdr->p_flags = 0;
		phdr->p_align = 0;
		phdr++;

		/* All the writable segments from the program. */
		phc.phdr = phdr;
		phc.offset = *off;
		each_writable_segment(p, cb_put_phdr, &phc);
	}
}

static void
elf32_putnote(void *dst, size_t *off, const char *name, int type,
    const void *desc, size_t descsz)
{
	Elf_Note note;

	note.n_namesz = strlen(name) + 1;
	note.n_descsz = descsz;
	note.n_type = type;
	if (dst != NULL)
		bcopy(&note, (char *)dst + *off, sizeof note);
	*off += sizeof note;
	if (dst != NULL)
		bcopy(name, (char *)dst + *off, note.n_namesz);
	*off += roundup2(note.n_namesz, sizeof(Elf32_Size));
	if (dst != NULL)
		bcopy(desc, (char *)dst + *off, note.n_descsz);
	*off += roundup2(note.n_descsz, sizeof(Elf32_Size));
}

struct ia32_ps_strings {
	u_int32_t ps_argvstr;	/* first of 0 or more argument strings */
	int	ps_nargvstr;	/* the number of argument strings */
	u_int32_t ps_envstr;	/* first of 0 or more environment strings */
	int	ps_nenvstr;	/* the number of environment strings */
};

static register_t *
elf32_copyout_strings(struct image_params *imgp)
{
	int argc, envc;
	u_int32_t *vectp;
	char *stringp, *destp;
	u_int32_t *stack_base;
	struct ia32_ps_strings *arginfo;
	int szsigcode;

	/*
	 * Calculate string base and vector table pointers.
	 * Also deal with signal trampoline code for this exec type.
	 */
	arginfo = (struct ia32_ps_strings *)IA32_PS_STRINGS;
	szsigcode = *(imgp->proc->p_sysent->sv_szsigcode);
	destp =	(caddr_t)arginfo - szsigcode - SPARE_USRSPACE -
		roundup((ARG_MAX - imgp->stringspace), sizeof(char *));

	/*
	 * install sigcode
	 */
	if (szsigcode)
		copyout(imgp->proc->p_sysent->sv_sigcode,
			((caddr_t)arginfo - szsigcode), szsigcode);

	/*
	 * If we have a valid auxargs ptr, prepare some room
	 * on the stack.
	 */
	if (imgp->auxargs) {
		/*
		 * 'AT_COUNT*2' is size for the ELF Auxargs data. This is for
		 * lower compatibility.
		 */
		imgp->auxarg_size = (imgp->auxarg_size) ? imgp->auxarg_size
			: (AT_COUNT * 2);
		/*
		 * The '+ 2' is for the null pointers at the end of each of
		 * the arg and env vector sets,and imgp->auxarg_size is room
		 * for argument of Runtime loader.
		 */
		vectp = (u_int32_t *) (destp - (imgp->argc + imgp->envc + 2 +
				       imgp->auxarg_size) * sizeof(u_int32_t));

	} else 
		/*
		 * The '+ 2' is for the null pointers at the end of each of
		 * the arg and env vector sets
		 */
		vectp = (u_int32_t *)
			(destp - (imgp->argc + imgp->envc + 2) * sizeof(u_int32_t));

	/*
	 * vectp also becomes our initial stack base
	 */
	stack_base = vectp;

	stringp = imgp->stringbase;
	argc = imgp->argc;
	envc = imgp->envc;

	/*
	 * Copy out strings - arguments and environment.
	 */
	copyout(stringp, destp, ARG_MAX - imgp->stringspace);

	/*
	 * Fill in "ps_strings" struct for ps, w, etc.
	 */
	suword32(&arginfo->ps_argvstr, (u_int32_t)(intptr_t)vectp);
	suword32(&arginfo->ps_nargvstr, argc);

	/*
	 * Fill in argument portion of vector table.
	 */
	for (; argc > 0; --argc) {
		suword32(vectp++, (u_int32_t)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* a null vector table pointer separates the argp's from the envp's */
	suword32(vectp++, 0);

	suword32(&arginfo->ps_envstr, (u_int32_t)(intptr_t)vectp);
	suword32(&arginfo->ps_nenvstr, envc);

	/*
	 * Fill in environment portion of vector table.
	 */
	for (; envc > 0; --envc) {
		suword32(vectp++, (u_int32_t)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* end of vector table is a null pointer */
	suword32(vectp, 0);

	return ((register_t *)stack_base);
}

static void
elf32_setregs(struct thread *td, u_long entry, u_long stack,
    u_long ps_strings)
{
	struct trapframe *frame = td->td_frame;
	vm_offset_t gdt, ldt;
	u_int64_t codesel, datasel, ldtsel;
	u_int64_t codeseg, dataseg, gdtseg, ldtseg;
	struct segment_descriptor desc;
	struct vmspace *vmspace = td->td_proc->p_vmspace;

	/*
	 * Make sure that we restore the entire trapframe after an
	 * execve.
	 */
	frame->tf_flags &= ~FRAME_SYSCALL;

	bzero(frame->tf_r, sizeof(frame->tf_r));
	bzero(frame->tf_f, sizeof(frame->tf_f));

	frame->tf_cr_iip = entry;
	frame->tf_cr_ipsr = (IA64_PSR_IC
			     | IA64_PSR_I
			     | IA64_PSR_IT
			     | IA64_PSR_DT
			     | IA64_PSR_RT
			     | IA64_PSR_IS
			     | IA64_PSR_BN
			     | IA64_PSR_CPL_USER);
	frame->tf_r[FRAME_R12] = stack;

	codesel = LSEL(LUCODE_SEL, SEL_UPL);
	datasel = LSEL(LUDATA_SEL, SEL_UPL);
	ldtsel = GSEL(GLDT_SEL, SEL_UPL);

#if 1
	frame->tf_r[FRAME_R16] = (datasel << 48) | (datasel << 32)
		| (datasel << 16) | datasel;
	frame->tf_r[FRAME_R17] = (ldtsel << 32) | (datasel << 16) | codesel;
#else
	frame->tf_r[FRAME_R16] = datasel;
	frame->tf_r[FRAME_R17] = codesel;
	frame->tf_r[FRAME_R18] = datasel;
	frame->tf_r[FRAME_R19] = datasel;
	frame->tf_r[FRAME_R20] = datasel;
	frame->tf_r[FRAME_R21] = datasel;
	frame->tf_r[FRAME_R22] = ldtsel;
#endif

	/*
	 * Build the GDT and LDT.
	 */
	gdt = IA32_USRSTACK;
	vm_map_find(&vmspace->vm_map, 0, 0,
		    &gdt, PAGE_SIZE, 0,
		    VM_PROT_ALL, VM_PROT_ALL, 0);
	ldt = gdt + 4096;
	
	desc.sd_lolimit = 8*NLDT-1;
	desc.sd_lobase = ldt & 0xffffff;
	desc.sd_type = SDT_SYSLDT;
	desc.sd_dpl = SEL_UPL;
	desc.sd_p = 1;
	desc.sd_hilimit = 0;
	desc.sd_def32 = 0;
	desc.sd_gran = 0;
	desc.sd_hibase = ldt >> 24;
	copyout(&desc, (caddr_t) gdt + 8*GLDT_SEL, sizeof(desc));

	desc.sd_lolimit = ((IA32_USRSTACK >> 12) - 1) & 0xffff;
	desc.sd_lobase = 0;
	desc.sd_type = SDT_MEMERA;
	desc.sd_dpl = SEL_UPL;
	desc.sd_p = 1;
	desc.sd_hilimit = ((IA32_USRSTACK >> 12) - 1) >> 16;
	desc.sd_def32 = 1;
	desc.sd_gran = 1;
	desc.sd_hibase = 0;
	copyout(&desc, (caddr_t) ldt + 8*LUCODE_SEL, sizeof(desc));
	desc.sd_type = SDT_MEMRWA;
	copyout(&desc, (caddr_t) ldt + 8*LUDATA_SEL, sizeof(desc));
	
	codeseg = 0		/* base */
		+ (((IA32_USRSTACK >> 12) - 1) << 32) /* limit */
		+ ((long)SDT_MEMERA << 52)
		+ ((long)SEL_UPL << 57)
		+ (1L << 59) /* present */
		+ (1L << 62) /* 32 bits */
		+ (1L << 63); /* page granularity */
	dataseg = 0		/* base */
		+ (((IA32_USRSTACK >> 12) - 1) << 32) /* limit */
		+ ((long)SDT_MEMRWA << 52)
		+ ((long)SEL_UPL << 57)
		+ (1L << 59) /* present */
		+ (1L << 62) /* 32 bits */
		+ (1L << 63); /* page granularity */
	ia64_set_csd(codeseg);
	ia64_set_ssd(dataseg);
	frame->tf_r[FRAME_R24] = dataseg; /* ESD */
	frame->tf_r[FRAME_R27] = dataseg; /* DSD */
	frame->tf_r[FRAME_R28] = dataseg; /* FSD */
	frame->tf_r[FRAME_R29] = dataseg; /* GSD */

	gdtseg = gdt		/* base */
		+ ((8L*NGDT - 1) << 32) /* limit */
		+ ((long)SDT_SYSNULL << 52)
		+ ((long)SEL_UPL << 57)
		+ (1L << 59) /* present */
		+ (0L << 62) /* 16 bits */
		+ (0L << 63); /* byte granularity */
	ldtseg = ldt		/* base */
		+ ((8L*NLDT - 1) << 32) /* limit */
		+ ((long)SDT_SYSLDT << 52)
		+ ((long)SEL_UPL << 57)
		+ (1L << 59) /* present */
		+ (0L << 62) /* 16 bits */
		+ (0L << 63); /* byte granularity */
	frame->tf_r[FRAME_R30] = ldtseg; /* LDTD */
	frame->tf_r[FRAME_R31] = gdtseg; /* GDTD */

	ia64_set_eflag(PSL_USER);

	/* PS_STRINGS value for BSD/OS binaries.  It is 0 for non-BSD/OS. */
	frame->tf_r[FRAME_R11] = ps_strings;

	/*
	 * Set ia32 control registers.
	 */
	ia64_set_cflg((CR0_PE | CR0_PG)
		      | ((long)(CR4_XMM | CR4_FXSR) << 32));

	/*
	 * XXX - Linux emulator
	 * Make sure sure edx is 0x0 on entry. Linux binaries depend
	 * on it.
	 */
	td->td_retval[1] = 0;
}

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw elf32_execsw = {exec_elf32_imgact, "ELF32"};
EXEC_SET(elf32, elf32_execsw);
