/* BFD back-end for IBM RS/6000 "XCOFF" files.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001
   Free Software Foundation, Inc.
   FIXME: Can someone provide a transliteration of this name into ASCII?
   Using the following chars caused a compiler warning on HIUX (so I replaced
   them with octal escapes), and isn't useful without an understanding of what
   character set it is.
   Written by Metin G. Ozisik, Mimi Ph\373\364ng-Th\345o V\365,
     and John Gilmore.
   Archive support from Damon A. Permezel.
   Contributed by IBM Corporation and Cygnus Support.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "bfd.h"
#include "sysdep.h"
#include "bfdlink.h"
#include "libbfd.h"
#include "coff/internal.h"
#include "coff/xcoff.h"
#include "coff/rs6000.h"
#include "libcoff.h"
#include "libxcoff.h"

extern boolean _bfd_xcoff_mkobject PARAMS ((bfd *));
extern boolean _bfd_xcoff_copy_private_bfd_data PARAMS ((bfd *, bfd *));
extern boolean _bfd_xcoff_is_local_label_name PARAMS ((bfd *, const char *));
extern reloc_howto_type *_bfd_xcoff_reloc_type_lookup
  PARAMS ((bfd *, bfd_reloc_code_real_type));
extern boolean _bfd_xcoff_slurp_armap PARAMS ((bfd *));
extern const bfd_target *_bfd_xcoff_archive_p PARAMS ((bfd *));
extern PTR _bfd_xcoff_read_ar_hdr PARAMS ((bfd *));
extern bfd *_bfd_xcoff_openr_next_archived_file PARAMS ((bfd *, bfd *));
extern int _bfd_xcoff_stat_arch_elt PARAMS ((bfd *, struct stat *));
extern boolean _bfd_xcoff_write_armap
  PARAMS ((bfd *, unsigned int, struct orl *, unsigned int, int));
extern boolean _bfd_xcoff_write_archive_contents PARAMS ((bfd *));
extern int _bfd_xcoff_sizeof_headers PARAMS ((bfd *, boolean));
extern void _bfd_xcoff_swap_sym_in PARAMS ((bfd *, PTR, PTR));
extern unsigned int _bfd_xcoff_swap_sym_out PARAMS ((bfd *, PTR, PTR));
extern void _bfd_xcoff_swap_aux_in PARAMS ((bfd *, PTR, int, int, int, int, PTR));
extern unsigned int _bfd_xcoff_swap_aux_out PARAMS ((bfd *, PTR, int, int, int, int, PTR));

/* Forward declare _bfd_xcoff_rtype2howto for coffcode.h macro.  */
void _bfd_xcoff_rtype2howto PARAMS ((arelent *, struct internal_reloc *));

/* coffcode.h needs these to be defined.  */
#define RS6000COFF_C 1

#define SELECT_RELOC(internal, howto)					\
  {									\
    internal.r_type = howto->type;					\
    internal.r_size =							\
      ((howto->complain_on_overflow == complain_overflow_signed		\
	? 0x80								\
	: 0)								\
       | (howto->bitsize - 1));						\
  }

#define COFF_DEFAULT_SECTION_ALIGNMENT_POWER (3)
#define COFF_LONG_FILENAMES
#define NO_COFF_SYMBOLS
#define RTYPE2HOWTO(cache_ptr, dst) _bfd_xcoff_rtype2howto (cache_ptr, dst)
#define coff_mkobject _bfd_xcoff_mkobject
#define coff_bfd_copy_private_bfd_data _bfd_xcoff_copy_private_bfd_data
#define coff_bfd_is_local_label_name _bfd_xcoff_is_local_label_name
#define coff_bfd_reloc_type_lookup _bfd_xcoff_reloc_type_lookup
#ifdef AIX_CORE
extern const bfd_target * rs6000coff_core_p ();
extern boolean rs6000coff_core_file_matches_executable_p ();
extern char *rs6000coff_core_file_failing_command PARAMS ((bfd *abfd));
extern int rs6000coff_core_file_failing_signal PARAMS ((bfd *abfd));
#define CORE_FILE_P rs6000coff_core_p
#define coff_core_file_failing_command \
  rs6000coff_core_file_failing_command
#define coff_core_file_failing_signal \
  rs6000coff_core_file_failing_signal
#define coff_core_file_matches_executable_p \
  rs6000coff_core_file_matches_executable_p
#else
#define CORE_FILE_P _bfd_dummy_target
#define coff_core_file_failing_command \
  _bfd_nocore_core_file_failing_command
#define coff_core_file_failing_signal \
  _bfd_nocore_core_file_failing_signal
#define coff_core_file_matches_executable_p \
  _bfd_nocore_core_file_matches_executable_p
#endif
#define coff_SWAP_sym_in _bfd_xcoff_swap_sym_in
#define coff_SWAP_sym_out _bfd_xcoff_swap_sym_out
#define coff_SWAP_aux_in _bfd_xcoff_swap_aux_in
#define coff_SWAP_aux_out _bfd_xcoff_swap_aux_out

#include "coffcode.h"

/* The main body of code is in coffcode.h.  */

static const char *normalize_filename PARAMS ((bfd *));
static boolean xcoff_write_armap_old
  PARAMS ((bfd *, unsigned int, struct orl *, unsigned int, int));
static boolean xcoff_write_armap_big
  PARAMS ((bfd *, unsigned int, struct orl *, unsigned int, int));
static boolean xcoff_write_archive_contents_old PARAMS ((bfd *));
static boolean xcoff_write_archive_contents_big PARAMS ((bfd *));
static void xcoff_swap_ldhdr_in
  PARAMS ((bfd *, const PTR, struct internal_ldhdr *));
static void xcoff_swap_ldhdr_out
  PARAMS ((bfd *, const struct internal_ldhdr *, PTR));
static void xcoff_swap_ldsym_in
  PARAMS ((bfd *, const PTR, struct internal_ldsym *));
static void xcoff_swap_ldsym_out
  PARAMS ((bfd *, const struct internal_ldsym *, PTR));
static void xcoff_swap_ldrel_in
  PARAMS ((bfd *, const PTR, struct internal_ldrel *));
static void xcoff_swap_ldrel_out
  PARAMS ((bfd *, const struct internal_ldrel *, PTR));
static boolean xcoff_ppc_relocate_section
  PARAMS ((bfd *, struct bfd_link_info *, bfd *, asection *, bfd_byte *,
	   struct internal_reloc *, struct internal_syment *, asection **));
static boolean _bfd_xcoff_put_ldsymbol_name
  PARAMS ((bfd *, struct xcoff_loader_info *, struct internal_ldsym *,
	   const char *));
static asection *xcoff_create_csect_from_smclas
  PARAMS ((bfd *, union internal_auxent *, const char *));
static boolean xcoff_is_lineno_count_overflow PARAMS ((bfd *, bfd_vma));
static boolean xcoff_is_reloc_count_overflow PARAMS ((bfd *, bfd_vma));
static bfd_vma xcoff_loader_symbol_offset
  PARAMS ((bfd *, struct internal_ldhdr *));
static bfd_vma xcoff_loader_reloc_offset
  PARAMS ((bfd *, struct internal_ldhdr *));
static boolean xcoff_generate_rtinit 
  PARAMS((bfd *, const char *, const char *));


/* We use our own tdata type.  Its first field is the COFF tdata type,
   so the COFF routines are compatible.  */

boolean
_bfd_xcoff_mkobject (abfd)
     bfd *abfd;
{
  coff_data_type *coff;
  bfd_size_type amt = sizeof (struct xcoff_tdata);

  abfd->tdata.xcoff_obj_data = (struct xcoff_tdata *) bfd_zalloc (abfd, amt);
  if (abfd->tdata.xcoff_obj_data == NULL)
    return false;
  coff = coff_data (abfd);
  coff->symbols = (coff_symbol_type *) NULL;
  coff->conversion_table = (unsigned int *) NULL;
  coff->raw_syments = (struct coff_ptr_struct *) NULL;
  coff->relocbase = 0;

  xcoff_data (abfd)->modtype = ('1' << 8) | 'L';

  /* We set cputype to -1 to indicate that it has not been
     initialized.  */
  xcoff_data (abfd)->cputype = -1;

  xcoff_data (abfd)->csects = NULL;
  xcoff_data (abfd)->debug_indices = NULL;

  /* text section alignment is different than the default */
  /* xcoff_data (abfd)->text_align_power = 5; */

  return true;
}

/* Copy XCOFF data from one BFD to another.  */

boolean
_bfd_xcoff_copy_private_bfd_data (ibfd, obfd)
     bfd *ibfd;
     bfd *obfd;
{
  struct xcoff_tdata *ix, *ox;
  asection *sec;

  if (ibfd->xvec != obfd->xvec)
    return true;
  ix = xcoff_data (ibfd);
  ox = xcoff_data (obfd);
  ox->full_aouthdr = ix->full_aouthdr;
  ox->toc = ix->toc;
  if (ix->sntoc == 0)
    ox->sntoc = 0;
  else
    {
      sec = coff_section_from_bfd_index (ibfd, ix->sntoc);
      if (sec == NULL)
	ox->sntoc = 0;
      else
	ox->sntoc = sec->output_section->target_index;
    }
  if (ix->snentry == 0)
    ox->snentry = 0;
  else
    {
      sec = coff_section_from_bfd_index (ibfd, ix->snentry);
      if (sec == NULL)
	ox->snentry = 0;
      else
	ox->snentry = sec->output_section->target_index;
    }
  ox->text_align_power = ix->text_align_power;
  ox->data_align_power = ix->data_align_power;
  ox->modtype = ix->modtype;
  ox->cputype = ix->cputype;
  ox->maxdata = ix->maxdata;
  ox->maxstack = ix->maxstack;
  return true;
}

/* I don't think XCOFF really has a notion of local labels based on
   name.  This will mean that ld -X doesn't actually strip anything.
   The AIX native linker does not have a -X option, and it ignores the
   -x option.  */

boolean
_bfd_xcoff_is_local_label_name (abfd, name)
     bfd *abfd ATTRIBUTE_UNUSED;
     const char *name ATTRIBUTE_UNUSED;
{
  return false;
}

void
_bfd_xcoff_swap_sym_in (abfd, ext1, in1)
     bfd            *abfd;
     PTR ext1;
     PTR in1;
{
  SYMENT *ext = (SYMENT *)ext1;
  struct internal_syment * in = (struct internal_syment *)in1;

  if (ext->e.e_name[0] != 0)
    {
      memcpy(in->_n._n_name, ext->e.e_name, SYMNMLEN);
    }
  else
    {
      in->_n._n_n._n_zeroes = 0;
      in->_n._n_n._n_offset = H_GET_32 (abfd, ext->e.e.e_offset);
    }

  in->n_value = H_GET_32 (abfd, ext->e_value);
  in->n_scnum = H_GET_16 (abfd, ext->e_scnum);
  in->n_type = H_GET_16 (abfd, ext->e_type);
  in->n_sclass = H_GET_8 (abfd, ext->e_sclass);
  in->n_numaux = H_GET_8 (abfd, ext->e_numaux);
}

unsigned int
_bfd_xcoff_swap_sym_out (abfd, inp, extp)
     bfd       *abfd;
     PTR	inp;
     PTR	extp;
{
  struct internal_syment *in = (struct internal_syment *)inp;
  SYMENT *ext =(SYMENT *)extp;

  if (in->_n._n_name[0] != 0)
    {
      memcpy(ext->e.e_name, in->_n._n_name, SYMNMLEN);
    }
  else
    {
      H_PUT_32 (abfd, 0, ext->e.e.e_zeroes);
      H_PUT_32 (abfd, in->_n._n_n._n_offset, ext->e.e.e_offset);
    }

  H_PUT_32 (abfd, in->n_value, ext->e_value);
  H_PUT_16 (abfd, in->n_scnum, ext->e_scnum);
  H_PUT_16 (abfd, in->n_type, ext->e_type);
  H_PUT_8 (abfd, in->n_sclass, ext->e_sclass);
  H_PUT_8 (abfd, in->n_numaux, ext->e_numaux);
  return bfd_coff_symesz (abfd);
}

void
_bfd_xcoff_swap_aux_in (abfd, ext1, type, class, indx, numaux, in1)
     bfd            *abfd;
     PTR 	      ext1;
     int             type;
     int             class;
     int	      indx;
     int	      numaux;
     PTR 	      in1;
{
  AUXENT * ext = (AUXENT *)ext1;
  union internal_auxent *in = (union internal_auxent *)in1;

  switch (class)
    {
    case C_FILE:
      if (ext->x_file.x_fname[0] == 0)
	{
	  in->x_file.x_n.x_zeroes = 0;
	  in->x_file.x_n.x_offset =
	    H_GET_32 (abfd, ext->x_file.x_n.x_offset);
	}
      else
	{
	  if (numaux > 1)
	    {
	      if (indx == 0)
		memcpy (in->x_file.x_fname, ext->x_file.x_fname,
			numaux * sizeof (AUXENT));
	    }
	  else
	    {
	      memcpy (in->x_file.x_fname, ext->x_file.x_fname, FILNMLEN);
	    }
	}
      goto end;

      /* RS/6000 "csect" auxents */
    case C_EXT:
    case C_HIDEXT:
      if (indx + 1 == numaux)
	{
	  in->x_csect.x_scnlen.l = H_GET_32 (abfd, ext->x_csect.x_scnlen);
	  in->x_csect.x_parmhash = H_GET_32 (abfd, ext->x_csect.x_parmhash);
	  in->x_csect.x_snhash   = H_GET_16 (abfd, ext->x_csect.x_snhash);
	  /* We don't have to hack bitfields in x_smtyp because it's
	     defined by shifts-and-ands, which are equivalent on all
	     byte orders.  */
	  in->x_csect.x_smtyp    = H_GET_8 (abfd, ext->x_csect.x_smtyp);
	  in->x_csect.x_smclas   = H_GET_8 (abfd, ext->x_csect.x_smclas);
	  in->x_csect.x_stab     = H_GET_32 (abfd, ext->x_csect.x_stab);
	  in->x_csect.x_snstab   = H_GET_16 (abfd, ext->x_csect.x_snstab);
	  goto end;
	}
      break;

    case C_STAT:
    case C_LEAFSTAT:
    case C_HIDDEN:
      if (type == T_NULL)
	{
	  in->x_scn.x_scnlen = H_GET_32 (abfd, ext->x_scn.x_scnlen);
	  in->x_scn.x_nreloc = H_GET_16 (abfd, ext->x_scn.x_nreloc);
	  in->x_scn.x_nlinno = H_GET_16 (abfd, ext->x_scn.x_nlinno);
	  /* PE defines some extra fields; we zero them out for
             safety.  */
	  in->x_scn.x_checksum = 0;
	  in->x_scn.x_associated = 0;
	  in->x_scn.x_comdat = 0;

	  goto end;
	}
      break;
    }

  in->x_sym.x_tagndx.l = H_GET_32 (abfd, ext->x_sym.x_tagndx);
  in->x_sym.x_tvndx = H_GET_16 (abfd, ext->x_sym.x_tvndx);

  if (class == C_BLOCK || class == C_FCN || ISFCN (type) || ISTAG (class))
    {
      in->x_sym.x_fcnary.x_fcn.x_lnnoptr =
	H_GET_32 (abfd, ext->x_sym.x_fcnary.x_fcn.x_lnnoptr);
      in->x_sym.x_fcnary.x_fcn.x_endndx.l =
	H_GET_32 (abfd, ext->x_sym.x_fcnary.x_fcn.x_endndx);
    }
  else
    {
      in->x_sym.x_fcnary.x_ary.x_dimen[0] =
	H_GET_16 (abfd, ext->x_sym.x_fcnary.x_ary.x_dimen[0]);
      in->x_sym.x_fcnary.x_ary.x_dimen[1] =
	H_GET_16 (abfd, ext->x_sym.x_fcnary.x_ary.x_dimen[1]);
      in->x_sym.x_fcnary.x_ary.x_dimen[2] =
	H_GET_16 (abfd, ext->x_sym.x_fcnary.x_ary.x_dimen[2]);
      in->x_sym.x_fcnary.x_ary.x_dimen[3] =
	H_GET_16 (abfd, ext->x_sym.x_fcnary.x_ary.x_dimen[3]);
    }

  if (ISFCN (type))
    {
      in->x_sym.x_misc.x_fsize = H_GET_32 (abfd, ext->x_sym.x_misc.x_fsize);
    }
  else
    {
      in->x_sym.x_misc.x_lnsz.x_lnno =
	H_GET_16 (abfd, ext->x_sym.x_misc.x_lnsz.x_lnno);
      in->x_sym.x_misc.x_lnsz.x_size =
	H_GET_16 (abfd, ext->x_sym.x_misc.x_lnsz.x_size);
    }

 end: ;
  /* The semicolon is because MSVC doesn't like labels at
     end of block.  */
}


unsigned int _bfd_xcoff_swap_aux_out PARAMS ((bfd *, PTR, int, int, int, int, PTR));

unsigned int
_bfd_xcoff_swap_aux_out (abfd, inp, type, class, indx, numaux, extp)
     bfd * abfd;
     PTR   inp;
     int   type;
     int   class;
     int   indx ATTRIBUTE_UNUSED;
     int   numaux ATTRIBUTE_UNUSED;
     PTR   extp;
{
  union internal_auxent *in = (union internal_auxent *)inp;
  AUXENT *ext = (AUXENT *)extp;

  memset((PTR)ext, 0, bfd_coff_auxesz (abfd));
  switch (class)
    {
    case C_FILE:
      if (in->x_file.x_fname[0] == 0)
	{
	  H_PUT_32 (abfd, 0, ext->x_file.x_n.x_zeroes);
	  H_PUT_32 (abfd, in->x_file.x_n.x_offset, ext->x_file.x_n.x_offset);
	}
      else
	{
	  memcpy (ext->x_file.x_fname, in->x_file.x_fname, FILNMLEN);
	}
      goto end;

      /* RS/6000 "csect" auxents */
    case C_EXT:
    case C_HIDEXT:
      if (indx + 1 == numaux)
	{
	  H_PUT_32 (abfd, in->x_csect.x_scnlen.l, ext->x_csect.x_scnlen);
	  H_PUT_32 (abfd, in->x_csect.x_parmhash, ext->x_csect.x_parmhash);
	  H_PUT_16 (abfd, in->x_csect.x_snhash, ext->x_csect.x_snhash);
	  /* We don't have to hack bitfields in x_smtyp because it's
	     defined by shifts-and-ands, which are equivalent on all
	     byte orders.  */
	  H_PUT_8 (abfd, in->x_csect.x_smtyp, ext->x_csect.x_smtyp);
	  H_PUT_8 (abfd, in->x_csect.x_smclas, ext->x_csect.x_smclas);
	  H_PUT_32 (abfd, in->x_csect.x_stab, ext->x_csect.x_stab);
	  H_PUT_16 (abfd, in->x_csect.x_snstab, ext->x_csect.x_snstab);
	  goto end;
	}
      break;

    case C_STAT:
    case C_LEAFSTAT:
    case C_HIDDEN:
      if (type == T_NULL)
	{
	  H_PUT_32 (abfd, in->x_scn.x_scnlen, ext->x_scn.x_scnlen);
	  H_PUT_16 (abfd, in->x_scn.x_nreloc, ext->x_scn.x_nreloc);
	  H_PUT_16 (abfd, in->x_scn.x_nlinno, ext->x_scn.x_nlinno);
	  goto end;
	}
      break;
    }

  H_PUT_32 (abfd, in->x_sym.x_tagndx.l, ext->x_sym.x_tagndx);
  H_PUT_16 (abfd, in->x_sym.x_tvndx, ext->x_sym.x_tvndx);

  if (class == C_BLOCK || class == C_FCN || ISFCN (type) || ISTAG (class))
    {
      H_PUT_32 (abfd, in->x_sym.x_fcnary.x_fcn.x_lnnoptr,
		ext->x_sym.x_fcnary.x_fcn.x_lnnoptr);
      H_PUT_32 (abfd, in->x_sym.x_fcnary.x_fcn.x_endndx.l,
		ext->x_sym.x_fcnary.x_fcn.x_endndx);
    }
  else
    {
      H_PUT_16 (abfd, in->x_sym.x_fcnary.x_ary.x_dimen[0],
		ext->x_sym.x_fcnary.x_ary.x_dimen[0]);
      H_PUT_16 (abfd, in->x_sym.x_fcnary.x_ary.x_dimen[1],
		ext->x_sym.x_fcnary.x_ary.x_dimen[1]);
      H_PUT_16 (abfd, in->x_sym.x_fcnary.x_ary.x_dimen[2],
		ext->x_sym.x_fcnary.x_ary.x_dimen[2]);
      H_PUT_16 (abfd, in->x_sym.x_fcnary.x_ary.x_dimen[3],
		ext->x_sym.x_fcnary.x_ary.x_dimen[3]);
    }

  if (ISFCN (type))
    H_PUT_32 (abfd, in->x_sym.x_misc.x_fsize, ext->x_sym.x_misc.x_fsize);
  else
    {
      H_PUT_16 (abfd, in->x_sym.x_misc.x_lnsz.x_lnno,
		ext->x_sym.x_misc.x_lnsz.x_lnno);
      H_PUT_16 (abfd, in->x_sym.x_misc.x_lnsz.x_size,
		ext->x_sym.x_misc.x_lnsz.x_size);
    }

end:
  return bfd_coff_auxesz (abfd);
}



/* The XCOFF reloc table.  Actually, XCOFF relocations specify the
   bitsize and whether they are signed or not, along with a
   conventional type.  This table is for the types, which are used for
   different algorithms for putting in the reloc.  Many of these
   relocs need special_function entries, which I have not written.  */


reloc_howto_type xcoff_howto_table[] =
{
  /* Standard 32 bit relocation.  */
  HOWTO (0,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_POS",               /* name */
	 true,	                /* partial_inplace */
	 0xffffffff,            /* src_mask */
	 0xffffffff,            /* dst_mask */
	 false),                /* pcrel_offset */

  /* 32 bit relocation, but store negative value.  */
  HOWTO (1,	                /* type */
	 0,	                /* rightshift */
	 -2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_NEG",               /* name */
	 true,	                /* partial_inplace */
	 0xffffffff,            /* src_mask */
	 0xffffffff,            /* dst_mask */
	 false),                /* pcrel_offset */

  /* 32 bit PC relative relocation.  */
  HOWTO (2,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 true,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_REL",               /* name */
	 true,	                /* partial_inplace */
	 0xffffffff,            /* src_mask */
	 0xffffffff,            /* dst_mask */
	 false),                /* pcrel_offset */

  /* 16 bit TOC relative relocation.  */
  HOWTO (3,	                /* type */
	 0,	                /* rightshift */
	 1,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_TOC",               /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* I don't really know what this is.  */
  HOWTO (4,	                /* type */
	 1,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RTB",               /* name */
	 true,	                /* partial_inplace */
	 0xffffffff,	        /* src_mask */
	 0xffffffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* External TOC relative symbol.  */
  HOWTO (5,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_GL",                /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Local TOC relative symbol.  */
  HOWTO (6,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_TCL",               /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  EMPTY_HOWTO (7),

  /* Non modifiable absolute branch.  */
  HOWTO (8,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 26,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_BA",                /* name */
	 true,	                /* partial_inplace */
	 0x3fffffc,	        /* src_mask */
	 0x3fffffc,        	/* dst_mask */
	 false),                /* pcrel_offset */

  EMPTY_HOWTO (9),

  /* Non modifiable relative branch.  */
  HOWTO (0xa,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 26,	                /* bitsize */
	 true,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_BR",                /* name */
	 true,	                /* partial_inplace */
	 0x3fffffc,	        /* src_mask */
	 0x3fffffc,        	/* dst_mask */
	 false),                /* pcrel_offset */

  EMPTY_HOWTO (0xb),

  /* Indirect load.  */
  HOWTO (0xc,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RL",                /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Load address.  */
  HOWTO (0xd,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RLA",               /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  EMPTY_HOWTO (0xe),

  /* Non-relocating reference.  */
  HOWTO (0xf,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_REF",               /* name */
	 false,	                /* partial_inplace */
	 0,		        /* src_mask */
	 0,     	   	/* dst_mask */
	 false),                /* pcrel_offset */

  EMPTY_HOWTO (0x10),
  EMPTY_HOWTO (0x11),

  /* TOC relative indirect load.  */
  HOWTO (0x12,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_TRL",               /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* TOC relative load address.  */
  HOWTO (0x13,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_TRLA",              /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable relative branch.  */
  HOWTO (0x14,	                /* type */
	 1,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RRTBI",             /* name */
	 true,	                /* partial_inplace */
	 0xffffffff,	        /* src_mask */
	 0xffffffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable absolute branch.  */
  HOWTO (0x15,	                /* type */
	 1,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RRTBA",             /* name */
	 true,	                /* partial_inplace */
	 0xffffffff,	        /* src_mask */
	 0xffffffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable call absolute indirect.  */
  HOWTO (0x16,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_CAI",               /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable call relative.  */
  HOWTO (0x17,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_CREL",              /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable branch absolute.  */
  HOWTO (0x18,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 26,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RBA",               /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable branch absolute.  */
  HOWTO (0x19,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 32,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RBAC",              /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable branch relative.  */
  HOWTO (0x1a,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 26,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RBR",               /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  /* Modifiable branch absolute.  */
  HOWTO (0x1b,	                /* type */
	 0,	                /* rightshift */
	 2,	                /* size (0 = byte, 1 = short, 2 = long) */
	 16,	                /* bitsize */
	 false,	                /* pc_relative */
	 0,	                /* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,		        /* special_function */
	 "R_RBRC",              /* name */
	 true,	                /* partial_inplace */
	 0xffff,	        /* src_mask */
	 0xffff,        	/* dst_mask */
	 false),                /* pcrel_offset */

  HOWTO (0,                     /* type */
         0,                     /* rightshift */
         4,                     /* size (0 = byte, 1 = short, 2 = long) */
         64,                    /* bitsize */
         false,                 /* pc_relative */
         0,                     /* bitpos */
         complain_overflow_bitfield, /* complain_on_overflow */
         0,                     /* special_function */
         "R_POS",               /* name */
         true,                  /* partial_inplace */
         MINUS_ONE,             /* src_mask */
	 MINUS_ONE,             /* dst_mask */
	 false)                 /* pcrel_offset */

};

void
_bfd_xcoff_rtype2howto (relent, internal)
     arelent *relent;
     struct internal_reloc *internal;
{
  relent->howto = xcoff_howto_table + internal->r_type;

  /* Check for relocs we don't know of.  */
  if (internal->r_type
      >= sizeof (xcoff_howto_table) / sizeof (xcoff_howto_table[0]))
    abort ();
  if (internal->r_type != relent->howto->type)
    abort ();

  /* The r_size field of an XCOFF reloc encodes the bitsize of the
     relocation, as well as indicating whether it is signed or not.
     Doublecheck that the relocation information gathered from the
     type matches this information.  The bitsize is not significant
     for R_REF relocs.  */
  if (relent->howto->dst_mask != 0
      && (relent->howto->bitsize
	  != ((unsigned int) internal->r_size & 0x3f) + 1))
    abort ();
#if 0
  if ((internal->r_size & 0x80) != 0
      ? (relent->howto->complain_on_overflow != complain_overflow_signed)
      : (relent->howto->complain_on_overflow != complain_overflow_bitfield))
    abort ();
#endif
}

reloc_howto_type *
_bfd_xcoff_reloc_type_lookup (abfd, code)
     bfd *abfd ATTRIBUTE_UNUSED;
     bfd_reloc_code_real_type code;
{
  switch (code)
    {
    case BFD_RELOC_PPC_B26:
      return &xcoff_howto_table[0xa];
    case BFD_RELOC_PPC_BA26:
      return &xcoff_howto_table[8];
    case BFD_RELOC_PPC_TOC16:
      return &xcoff_howto_table[3];
    case BFD_RELOC_32:
    case BFD_RELOC_CTOR:
      return &xcoff_howto_table[0];
    case BFD_RELOC_64:
      return &xcoff_howto_table[0x1c];
    default:
      return NULL;
    }
}


/* XCOFF archive support.  The original version of this code was by
   Damon A. Permezel.  It was enhanced to permit cross support, and
   writing archive files, by Ian Lance Taylor, Cygnus Support.

   XCOFF uses its own archive format.  Everything is hooked together
   with file offset links, so it is possible to rapidly update an
   archive in place.  Of course, we don't do that.  An XCOFF archive
   has a real file header, not just an ARMAG string.  The structure of
   the file header and of each archive header appear below.

   An XCOFF archive also has a member table, which is a list of
   elements in the archive (you can get that by looking through the
   linked list, but you have to read a lot more of the file).  The
   member table has a normal archive header with an empty name.  It is
   normally (and perhaps must be) the second to last entry in the
   archive.  The member table data is almost printable ASCII.  It
   starts with a 12 character decimal string which is the number of
   entries in the table.  For each entry it has a 12 character decimal
   string which is the offset in the archive of that member.  These
   entries are followed by a series of null terminated strings which
   are the member names for each entry.

   Finally, an XCOFF archive has a global symbol table, which is what
   we call the armap.  The global symbol table has a normal archive
   header with an empty name.  It is normally (and perhaps must be)
   the last entry in the archive.  The contents start with a four byte
   binary number which is the number of entries.  This is followed by
   a that many four byte binary numbers; each is the file offset of an
   entry in the archive.  These numbers are followed by a series of
   null terminated strings, which are symbol names.

   AIX 4.3 introduced a new archive format which can handle larger
   files and also 32- and 64-bit objects in the same archive.  The
   things said above remain true except that there is now more than
   one global symbol table.  The one is used to index 32-bit objects,
   the other for 64-bit objects.

   The new archives (recognizable by the new ARMAG string) has larger
   field lengths so that we cannot really share any code.  Also we have
   to take care that we are not generating the new form of archives
   on AIX 4.2 or earlier systems.  */

/* XCOFF archives use this as a magic string.  Note that both strings
   have the same length.  */



/* Read in the armap of an XCOFF archive.  */

boolean
_bfd_xcoff_slurp_armap (abfd)
     bfd *abfd;
{
  file_ptr off;
  size_t namlen;
  bfd_size_type sz;
  bfd_byte *contents, *cend;
  bfd_vma c, i;
  carsym *arsym;
  bfd_byte *p;

  if (xcoff_ardata (abfd) == NULL)
    {
      bfd_has_map (abfd) = false;
      return true;
    }

  if (! xcoff_big_format_p (abfd))
    {
      /* This is for the old format.  */
      struct xcoff_ar_hdr hdr;

      off = strtol (xcoff_ardata (abfd)->symoff, (char **) NULL, 10);
      if (off == 0)
	{
	  bfd_has_map (abfd) = false;
	  return true;
	}

      if (bfd_seek (abfd, off, SEEK_SET) != 0)
	return false;

      /* The symbol table starts with a normal archive header.  */
      if (bfd_bread ((PTR) &hdr, (bfd_size_type) SIZEOF_AR_HDR, abfd)
	  != SIZEOF_AR_HDR)
	return false;

      /* Skip the name (normally empty).  */
      namlen = strtol (hdr.namlen, (char **) NULL, 10);
      off = ((namlen + 1) & ~ (size_t) 1) + SXCOFFARFMAG;
      if (bfd_seek (abfd, off, SEEK_CUR) != 0)
	return false;

      sz = strtol (hdr.size, (char **) NULL, 10);

      /* Read in the entire symbol table.  */
      contents = (bfd_byte *) bfd_alloc (abfd, sz);
      if (contents == NULL)
	return false;
      if (bfd_bread ((PTR) contents, sz, abfd) != sz)
	return false;

      /* The symbol table starts with a four byte count.  */
      c = H_GET_32 (abfd, contents);

      if (c * 4 >= sz)
	{
	  bfd_set_error (bfd_error_bad_value);
	  return false;
	}

      bfd_ardata (abfd)->symdefs =
	((carsym *) bfd_alloc (abfd, c * sizeof (carsym)));
      if (bfd_ardata (abfd)->symdefs == NULL)
	return false;

      /* After the count comes a list of four byte file offsets.  */
      for (i = 0, arsym = bfd_ardata (abfd)->symdefs, p = contents + 4;
	   i < c;
	   ++i, ++arsym, p += 4)
	arsym->file_offset = H_GET_32 (abfd, p);
    }
  else
    {
      /* This is for the new format.  */
      struct xcoff_ar_hdr_big hdr;

      off = strtol (xcoff_ardata_big (abfd)->symoff, (char **) NULL, 10);
      if (off == 0)
	{
	  bfd_has_map (abfd) = false;
	  return true;
	}

      if (bfd_seek (abfd, off, SEEK_SET) != 0)
	return false;

      /* The symbol table starts with a normal archive header.  */
      if (bfd_bread ((PTR) &hdr, (bfd_size_type) SIZEOF_AR_HDR_BIG, abfd)
	  != SIZEOF_AR_HDR_BIG)
	return false;

      /* Skip the name (normally empty).  */
      namlen = strtol (hdr.namlen, (char **) NULL, 10);
      off = ((namlen + 1) & ~ (size_t) 1) + SXCOFFARFMAG;
      if (bfd_seek (abfd, off, SEEK_CUR) != 0)
	return false;

      /* XXX This actually has to be a call to strtoll (at least on 32-bit
	 machines) since the field width is 20 and there numbers with more
	 than 32 bits can be represented.  */
      sz = strtol (hdr.size, (char **) NULL, 10);

      /* Read in the entire symbol table.  */
      contents = (bfd_byte *) bfd_alloc (abfd, sz);
      if (contents == NULL)
	return false;
      if (bfd_bread ((PTR) contents, sz, abfd) != sz)
	return false;

      /* The symbol table starts with an eight byte count.  */
      c = H_GET_64 (abfd, contents);

      if (c * 8 >= sz)
	{
	  bfd_set_error (bfd_error_bad_value);
	  return false;
	}

      bfd_ardata (abfd)->symdefs =
	((carsym *) bfd_alloc (abfd, c * sizeof (carsym)));
      if (bfd_ardata (abfd)->symdefs == NULL)
	return false;

      /* After the count comes a list of eight byte file offsets.  */
      for (i = 0, arsym = bfd_ardata (abfd)->symdefs, p = contents + 8;
	   i < c;
	   ++i, ++arsym, p += 8)
	arsym->file_offset = H_GET_64 (abfd, p);
    }

  /* After the file offsets come null terminated symbol names.  */
  cend = contents + sz;
  for (i = 0, arsym = bfd_ardata (abfd)->symdefs;
       i < c;
       ++i, ++arsym, p += strlen ((char *) p) + 1)
    {
      if (p >= cend)
	{
	  bfd_set_error (bfd_error_bad_value);
	  return false;
	}
      arsym->name = (char *) p;
    }

  bfd_ardata (abfd)->symdef_count = c;
  bfd_has_map (abfd) = true;

  return true;
}

/* See if this is an XCOFF archive.  */

const bfd_target *
_bfd_xcoff_archive_p (abfd)
     bfd *abfd;
{
  char magic[SXCOFFARMAG];
  bfd_size_type amt;

  if (bfd_bread ((PTR) magic, (bfd_size_type) SXCOFFARMAG, abfd) != SXCOFFARMAG)
    {
      if (bfd_get_error () != bfd_error_system_call)
	bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  if (strncmp (magic, XCOFFARMAG, SXCOFFARMAG) != 0
      && strncmp (magic, XCOFFARMAGBIG, SXCOFFARMAG) != 0)
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  /* We are setting bfd_ardata(abfd) here, but since bfd_ardata
     involves a cast, we can't do it as the left operand of
     assignment.  */
  amt = sizeof (struct artdata);
  abfd->tdata.aout_ar_data = (struct artdata *) bfd_zalloc (abfd, amt);
  if (bfd_ardata (abfd) == (struct artdata *) NULL)
    return NULL;

  bfd_ardata (abfd)->cache = NULL;
  bfd_ardata (abfd)->archive_head = NULL;
  bfd_ardata (abfd)->symdefs = NULL;
  bfd_ardata (abfd)->extended_names = NULL;

  /* Now handle the two formats.  */
  if (magic[1] != 'b')
    {
      /* This is the old format.  */
      struct xcoff_ar_file_hdr hdr;

      /* Copy over the magic string.  */
      memcpy (hdr.magic, magic, SXCOFFARMAG);

      /* Now read the rest of the file header.  */
      if (bfd_bread ((PTR) &hdr.memoff,
		    (bfd_size_type) SIZEOF_AR_FILE_HDR - SXCOFFARMAG, abfd)
	  != SIZEOF_AR_FILE_HDR - SXCOFFARMAG)
	{
	  if (bfd_get_error () != bfd_error_system_call)
	    bfd_set_error (bfd_error_wrong_format);
	  return NULL;
	}

      bfd_ardata (abfd)->first_file_filepos = strtol (hdr.firstmemoff,
						      (char **) NULL, 10);

      amt = SIZEOF_AR_FILE_HDR;
      bfd_ardata (abfd)->tdata = bfd_zalloc (abfd, amt);
      if (bfd_ardata (abfd)->tdata == NULL)
	return NULL;

      memcpy (bfd_ardata (abfd)->tdata, &hdr, SIZEOF_AR_FILE_HDR);
    }
  else
    {
      /* This is the new format.  */
      struct xcoff_ar_file_hdr_big hdr;

      /* Copy over the magic string.  */
      memcpy (hdr.magic, magic, SXCOFFARMAG);

      /* Now read the rest of the file header.  */
      if (bfd_bread ((PTR) &hdr.memoff,
		    (bfd_size_type) SIZEOF_AR_FILE_HDR_BIG - SXCOFFARMAG, abfd)
	  != SIZEOF_AR_FILE_HDR_BIG - SXCOFFARMAG)
	{
	  if (bfd_get_error () != bfd_error_system_call)
	    bfd_set_error (bfd_error_wrong_format);
	  return NULL;
	}

      /* XXX This actually has to be a call to strtoll (at least on 32-bit
	 machines) since the field width is 20 and there numbers with more
	 than 32 bits can be represented.  */
      bfd_ardata (abfd)->first_file_filepos = strtol (hdr.firstmemoff,
						      (char **) NULL, 10);

      amt = SIZEOF_AR_FILE_HDR_BIG;
      bfd_ardata (abfd)->tdata = bfd_zalloc (abfd, amt);
      if (bfd_ardata (abfd)->tdata == NULL)
	return NULL;

      memcpy (bfd_ardata (abfd)->tdata, &hdr, SIZEOF_AR_FILE_HDR_BIG);
    }

  if (! _bfd_xcoff_slurp_armap (abfd))
    {
      bfd_release (abfd, bfd_ardata (abfd));
      abfd->tdata.aout_ar_data = (struct artdata *) NULL;
      return NULL;
    }

  return abfd->xvec;
}

/* Read the archive header in an XCOFF archive.  */

PTR
_bfd_xcoff_read_ar_hdr (abfd)
     bfd *abfd;
{
  bfd_size_type namlen;
  struct areltdata *ret;
  bfd_size_type amt = sizeof (struct areltdata);

  ret = (struct areltdata *) bfd_alloc (abfd, amt);
  if (ret == NULL)
    return NULL;

  if (! xcoff_big_format_p (abfd))
    {
      struct xcoff_ar_hdr hdr;
      struct xcoff_ar_hdr *hdrp;

      if (bfd_bread ((PTR) &hdr, (bfd_size_type) SIZEOF_AR_HDR, abfd)
	  != SIZEOF_AR_HDR)
	{
	  free (ret);
	  return NULL;
	}

      namlen = strtol (hdr.namlen, (char **) NULL, 10);
      amt = SIZEOF_AR_HDR + namlen + 1;
      hdrp = (struct xcoff_ar_hdr *) bfd_alloc (abfd, amt);
      if (hdrp == NULL)
	{
	  free (ret);
	  return NULL;
	}
      memcpy (hdrp, &hdr, SIZEOF_AR_HDR);
      if (bfd_bread ((char *) hdrp + SIZEOF_AR_HDR, namlen, abfd) != namlen)
	{
	  free (ret);
	  return NULL;
	}
      ((char *) hdrp)[SIZEOF_AR_HDR + namlen] = '\0';

      ret->arch_header = (char *) hdrp;
      ret->parsed_size = strtol (hdr.size, (char **) NULL, 10);
      ret->filename = (char *) hdrp + SIZEOF_AR_HDR;
    }
  else
    {
      struct xcoff_ar_hdr_big hdr;
      struct xcoff_ar_hdr_big *hdrp;

      if (bfd_bread ((PTR) &hdr, (bfd_size_type) SIZEOF_AR_HDR_BIG, abfd)
	  != SIZEOF_AR_HDR_BIG)
	{
	  free (ret);
	  return NULL;
	}

      namlen = strtol (hdr.namlen, (char **) NULL, 10);
      amt = SIZEOF_AR_HDR_BIG + namlen + 1;
      hdrp = (struct xcoff_ar_hdr_big *) bfd_alloc (abfd, amt);
      if (hdrp == NULL)
	{
	  free (ret);
	  return NULL;
	}
      memcpy (hdrp, &hdr, SIZEOF_AR_HDR_BIG);
      if (bfd_bread ((char *) hdrp + SIZEOF_AR_HDR_BIG, namlen, abfd) != namlen)
	{
	  free (ret);
	  return NULL;
	}
      ((char *) hdrp)[SIZEOF_AR_HDR_BIG + namlen] = '\0';

      ret->arch_header = (char *) hdrp;
      /* XXX This actually has to be a call to strtoll (at least on 32-bit
	 machines) since the field width is 20 and there numbers with more
	 than 32 bits can be represented.  */
      ret->parsed_size = strtol (hdr.size, (char **) NULL, 10);
      ret->filename = (char *) hdrp + SIZEOF_AR_HDR_BIG;
    }

  /* Skip over the XCOFFARFMAG at the end of the file name.  */
  if (bfd_seek (abfd, (file_ptr) ((namlen & 1) + SXCOFFARFMAG), SEEK_CUR) != 0)
    return NULL;

  return (PTR) ret;
}

/* Open the next element in an XCOFF archive.  */

bfd *
_bfd_xcoff_openr_next_archived_file (archive, last_file)
     bfd *archive;
     bfd *last_file;
{
  file_ptr filestart;

  if (xcoff_ardata (archive) == NULL)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return NULL;
    }

  if (! xcoff_big_format_p (archive))
    {
      if (last_file == NULL)
	filestart = bfd_ardata (archive)->first_file_filepos;
      else
	filestart = strtol (arch_xhdr (last_file)->nextoff, (char **) NULL,
			    10);

      if (filestart == 0
	  || filestart == strtol (xcoff_ardata (archive)->memoff,
				  (char **) NULL, 10)
	  || filestart == strtol (xcoff_ardata (archive)->symoff,
				  (char **) NULL, 10))
	{
	  bfd_set_error (bfd_error_no_more_archived_files);
	  return NULL;
	}
    }
  else
    {
      if (last_file == NULL)
	filestart = bfd_ardata (archive)->first_file_filepos;
      else
	/* XXX These actually have to be a calls to strtoll (at least
	   on 32-bit machines) since the fields's width is 20 and
	   there numbers with more than 32 bits can be represented.  */
	filestart = strtol (arch_xhdr_big (last_file)->nextoff, (char **) NULL,
			    10);

      /* XXX These actually have to be calls to strtoll (at least on 32-bit
	 machines) since the fields's width is 20 and there numbers with more
	 than 32 bits can be represented.  */
      if (filestart == 0
	  || filestart == strtol (xcoff_ardata_big (archive)->memoff,
				  (char **) NULL, 10)
	  || filestart == strtol (xcoff_ardata_big (archive)->symoff,
				  (char **) NULL, 10))
	{
	  bfd_set_error (bfd_error_no_more_archived_files);
	  return NULL;
	}
    }

  return _bfd_get_elt_at_filepos (archive, filestart);
}

/* Stat an element in an XCOFF archive.  */

int
_bfd_xcoff_stat_arch_elt (abfd, s)
     bfd *abfd;
     struct stat *s;
{
  if (abfd->arelt_data == NULL)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return -1;
    }

  if (! xcoff_big_format_p (abfd->my_archive))
    {
      struct xcoff_ar_hdr *hdrp = arch_xhdr (abfd);

      s->st_mtime = strtol (hdrp->date, (char **) NULL, 10);
      s->st_uid = strtol (hdrp->uid, (char **) NULL, 10);
      s->st_gid = strtol (hdrp->gid, (char **) NULL, 10);
      s->st_mode = strtol (hdrp->mode, (char **) NULL, 8);
      s->st_size = arch_eltdata (abfd)->parsed_size;
    }
  else
    {
      struct xcoff_ar_hdr_big *hdrp = arch_xhdr_big (abfd);

      s->st_mtime = strtol (hdrp->date, (char **) NULL, 10);
      s->st_uid = strtol (hdrp->uid, (char **) NULL, 10);
      s->st_gid = strtol (hdrp->gid, (char **) NULL, 10);
      s->st_mode = strtol (hdrp->mode, (char **) NULL, 8);
      s->st_size = arch_eltdata (abfd)->parsed_size;
    }

  return 0;
}

/* Normalize a file name for inclusion in an archive.  */

static const char *
normalize_filename (abfd)
     bfd *abfd;
{
  const char *file;
  const char *filename;

  file = bfd_get_filename (abfd);
  filename = strrchr (file, '/');
  if (filename != NULL)
    filename++;
  else
    filename = file;
  return filename;
}

/* Write out an XCOFF armap.  */

/*ARGSUSED*/
static boolean
xcoff_write_armap_old (abfd, elength, map, orl_count, stridx)
     bfd *abfd;
     unsigned int elength ATTRIBUTE_UNUSED;
     struct orl *map;
     unsigned int orl_count;
     int stridx;
{
  struct xcoff_ar_hdr hdr;
  char *p;
  unsigned char buf[4];
  bfd *sub;
  file_ptr fileoff;
  unsigned int i;

  memset (&hdr, 0, sizeof hdr);
  sprintf (hdr.size, "%ld", (long) (4 + orl_count * 4 + stridx));
  sprintf (hdr.nextoff, "%d", 0);
  memcpy (hdr.prevoff, xcoff_ardata (abfd)->memoff, XCOFFARMAG_ELEMENT_SIZE);
  sprintf (hdr.date, "%d", 0);
  sprintf (hdr.uid, "%d", 0);
  sprintf (hdr.gid, "%d", 0);
  sprintf (hdr.mode, "%d", 0);
  sprintf (hdr.namlen, "%d", 0);

  /* We need spaces, not null bytes, in the header.  */
  for (p = (char *) &hdr; p < (char *) &hdr + SIZEOF_AR_HDR; p++)
    if (*p == '\0')
      *p = ' ';

  if (bfd_bwrite ((PTR) &hdr, (bfd_size_type) SIZEOF_AR_HDR, abfd)
      != SIZEOF_AR_HDR
      || (bfd_bwrite (XCOFFARFMAG, (bfd_size_type) SXCOFFARFMAG, abfd)
	  != SXCOFFARFMAG))
    return false;

  H_PUT_32 (abfd, orl_count, buf);
  if (bfd_bwrite (buf, (bfd_size_type) 4, abfd) != 4)
    return false;

  sub = abfd->archive_head;
  fileoff = SIZEOF_AR_FILE_HDR;
  i = 0;
  while (sub != NULL && i < orl_count)
    {
      size_t namlen;

      while (map[i].u.abfd == sub)
	{
	  H_PUT_32 (abfd, fileoff, buf);
	  if (bfd_bwrite (buf, (bfd_size_type) 4, abfd) != 4)
	    return false;
	  ++i;
	}
      namlen = strlen (normalize_filename (sub));
      namlen = (namlen + 1) &~ (size_t) 1;
      fileoff += (SIZEOF_AR_HDR
		  + namlen
		  + SXCOFFARFMAG
		  + arelt_size (sub));
      fileoff = (fileoff + 1) &~ 1;
      sub = sub->next;
    }

  for (i = 0; i < orl_count; i++)
    {
      const char *name;
      size_t namlen;

      name = *map[i].name;
      namlen = strlen (name);
      if (bfd_bwrite (name, (bfd_size_type) (namlen + 1), abfd) != namlen + 1)
	return false;
    }

  if ((stridx & 1) != 0)
    {
      char b;

      b = '\0';
      if (bfd_bwrite (&b, (bfd_size_type) 1, abfd) != 1)
	return false;
    }

  return true;
}

static char buff20[XCOFFARMAGBIG_ELEMENT_SIZE + 1];
#define FMT20  "%-20lld"
#define FMT12  "%-12d"
#define FMT12_OCTAL  "%-12o"
#define FMT4  "%-4d"
#define PRINT20(d, v) \
  sprintf (buff20, FMT20, (long long)(v)), \
  memcpy ((void *) (d), buff20, 20)

#define PRINT12(d, v) \
  sprintf (buff20, FMT12, (int)(v)), \
  memcpy ((void *) (d), buff20, 12) 

#define PRINT12_OCTAL(d, v) \
  sprintf (buff20, FMT12_OCTAL, (unsigned int)(v)), \
  memcpy ((void *) (d), buff20, 12)

#define PRINT4(d, v) \
  sprintf (buff20, FMT4, (int)(v)), \
  memcpy ((void *) (d), buff20, 4) 

#define READ20(d, v) \
  buff20[20] = 0, \
  memcpy (buff20, (d), 20), \
  (v) = bfd_scan_vma (buff20, (const char **) NULL, 10)

static boolean
xcoff_write_armap_big (abfd, elength, map, orl_count, stridx)
     bfd *abfd;
     unsigned int elength ATTRIBUTE_UNUSED;
     struct orl *map;
     unsigned int orl_count;
     int stridx;
{
  struct xcoff_ar_file_hdr_big *fhdr;
  bfd_vma i, sym_32, sym_64, str_32, str_64;
  const bfd_arch_info_type *arch_info = NULL;
  bfd *current_bfd;
  size_t string_length;
  ufile_ptr nextoff, prevoff;
  
  /* First, we look through the symbols and work out which are
     from 32-bit objects and which from 64-bit ones.  */
  sym_32 = sym_64 = str_32 = str_64 = 0;

  current_bfd = abfd->archive_head;
  if (current_bfd != NULL)
    arch_info = bfd_get_arch_info (current_bfd);
    i = 0;
    while (current_bfd != NULL && i < orl_count)
    {
      while (map[i].u.abfd == current_bfd)
	{
	  string_length = strlen (*map[i].name) + 1;

	  if (arch_info->bits_per_address == 64)
	    {
	      sym_64++;
	      str_64 += string_length;
	    }
	  else
	    {
	      sym_32++;
	      str_32 += string_length;
	    }
	  i++;
	}
      current_bfd = current_bfd->next;
      if (current_bfd != NULL)
	arch_info = bfd_get_arch_info (current_bfd);
    }

  /* A quick sanity check... */
  BFD_ASSERT (sym_64 + sym_32 == orl_count);
  /* Explicit cast to int for compiler.  */
  BFD_ASSERT ((int)(str_64 + str_32) == stridx);

  fhdr = xcoff_ardata_big (abfd);

  /* xcoff_write_archive_contents_big passes nextoff in symoff. */
  READ20 (fhdr->memoff, prevoff);
  READ20 (fhdr->symoff, nextoff);

  BFD_ASSERT (nextoff == bfd_tell (abfd));

  /* Write out the symbol table.  
     Layout : 
     
     standard big archive header
     0x0000                   ar_size   [0x14]
     0x0014                   ar_nxtmem [0x14]
     0x0028                   ar_prvmem [0x14]
     0x003C                   ar_date   [0x0C]
     0x0048                   ar_uid    [0x0C]
     0x0054                   ar_gid    [0x0C]
     0x0060                   ar_mod    [0x0C]
     0x006C                   ar_namelen[0x04]
     0x0070                   ar_fmag   [SXCOFFARFMAG]
     
     Symbol table 
     0x0072                   num_syms  [0x08], binary
     0x0078                   offsets   [0x08 * num_syms], binary
     0x0086 + 0x08 * num_syms names     [??]
     ??                       pad to even bytes.
  */

  if (sym_32) 
    {
      struct xcoff_ar_hdr_big *hdr;
      bfd_byte *symbol_table;
      bfd_byte *st;
      file_ptr fileoff;

      bfd_vma symbol_table_size = 
	SIZEOF_AR_HDR_BIG
	+ SXCOFFARFMAG
	+ 8 
	+ 8 * sym_32 
	+ str_32 + (str_32 & 1);

      symbol_table = NULL;
      symbol_table = (bfd_byte *) bfd_malloc (symbol_table_size);
      if (symbol_table == NULL)
	return false;
      memset (symbol_table, 0, symbol_table_size);

      hdr = (struct xcoff_ar_hdr_big *) symbol_table;
	
      PRINT20 (hdr->size, 8 + 8 * sym_32 + str_32 + (str_32 & 1));
	
      if (sym_64)
	PRINT20 (hdr->nextoff, nextoff + symbol_table_size);
      else
	PRINT20 (hdr->nextoff, 0);

      PRINT20 (hdr->prevoff, prevoff);
      PRINT12 (hdr->date, 0);
      PRINT12 (hdr->uid, 0);
      PRINT12 (hdr->gid, 0);
      PRINT12 (hdr->mode, 0);
      PRINT4 (hdr->namlen, 0) ;

      st = symbol_table + SIZEOF_AR_HDR_BIG;
      memcpy (st, XCOFFARFMAG, SXCOFFARFMAG);
      st += SXCOFFARFMAG;

      bfd_h_put_64 (abfd, sym_32, st);
      st += 8;
      
      /* loop over the 32 bit offsets */
      current_bfd = abfd->archive_head;
      if (current_bfd != NULL)
	arch_info = bfd_get_arch_info (current_bfd);
      fileoff = SIZEOF_AR_FILE_HDR_BIG;
      i = 0;
      while (current_bfd != NULL && i < orl_count)
	{
	  while (map[i].u.abfd == current_bfd)
	    {
	      if (arch_info->bits_per_address == 32)
		{
		  bfd_h_put_64 (abfd, fileoff, st);
		  st += 8;
		}
	      i++;
	    }
	  string_length = strlen (normalize_filename (current_bfd));
	  string_length += string_length & 1;
	  fileoff += (SIZEOF_AR_HDR_BIG
		      + string_length
		      + SXCOFFARFMAG
		      + arelt_size (current_bfd));
	  fileoff += fileoff & 1;
	  current_bfd = current_bfd->next;
	  if (current_bfd != NULL)
	    arch_info = bfd_get_arch_info (current_bfd);
	}

      /* loop over the 32 bit symbol names */
      current_bfd = abfd->archive_head;
      if (current_bfd != NULL)
	arch_info = bfd_get_arch_info (current_bfd);
      i = 0;
      while (current_bfd != NULL && i < orl_count)
	{
	  while (map[i].u.abfd == current_bfd)
	    {
	      if (arch_info->bits_per_address == 32)
		{
		  string_length = sprintf (st, "%s", *map[i].name);
		  st += string_length + 1;
		}
	      i++;
	    }
	  current_bfd = current_bfd->next;
	  if (current_bfd != NULL)
	    arch_info = bfd_get_arch_info (current_bfd);
	}

      bfd_bwrite (symbol_table, symbol_table_size, abfd);

      free (symbol_table);
      symbol_table = NULL;

      prevoff = nextoff;
      nextoff = nextoff + symbol_table_size;
    }
  else 
    PRINT20 (fhdr->symoff, 0);
  
  if (sym_64) 
    {
      struct xcoff_ar_hdr_big *hdr;
      bfd_byte *symbol_table;
      bfd_byte *st;
      file_ptr fileoff;

      bfd_vma symbol_table_size = 
	SIZEOF_AR_HDR_BIG
	+ SXCOFFARFMAG
	+ 8 
	+ 8 * sym_64 
	+ str_64 + (str_64 & 1);

      symbol_table = NULL;
      symbol_table = (bfd_byte *) bfd_malloc (symbol_table_size);
      if (symbol_table == NULL)
	return false;
      memset (symbol_table, 0, symbol_table_size);

      hdr = (struct xcoff_ar_hdr_big *) symbol_table;

      PRINT20 (hdr->size, 8 + 8 * sym_64 + str_64 + (str_64 & 1));
      PRINT20 (hdr->nextoff, 0);
      PRINT20 (hdr->prevoff, prevoff);
      PRINT12 (hdr->date, 0);
      PRINT12 (hdr->uid, 0);
      PRINT12 (hdr->gid, 0);
      PRINT12 (hdr->mode, 0);
      PRINT4 (hdr->namlen, 0);

      st = symbol_table + SIZEOF_AR_HDR_BIG;
      memcpy (st, XCOFFARFMAG, SXCOFFARFMAG);
      st += SXCOFFARFMAG;

      bfd_h_put_64 (abfd, sym_64, st);
      st += 8;
      
      /* loop over the 64 bit offsets */
      current_bfd = abfd->archive_head;
      if (current_bfd != NULL)
	arch_info = bfd_get_arch_info (current_bfd);
      fileoff = SIZEOF_AR_FILE_HDR_BIG;
      i = 0;
      while (current_bfd != NULL && i < orl_count)
	{
	  while (map[i].u.abfd == current_bfd)
	    {
	      if (arch_info->bits_per_address == 64)
		{
		  bfd_h_put_64 (abfd, fileoff, st);
		  st += 8;
		}
	      i++;
	    }
	  string_length = strlen (normalize_filename (current_bfd));
	  string_length += string_length & 1;
	  fileoff += (SIZEOF_AR_HDR_BIG
		      + string_length
		      + SXCOFFARFMAG
		      + arelt_size (current_bfd));
	  fileoff += fileoff & 1;
	  current_bfd = current_bfd->next;
	  if (current_bfd != NULL)
	    arch_info = bfd_get_arch_info (current_bfd);
	}

      /* loop over the 64 bit symbol names */
      current_bfd = abfd->archive_head;
      if (current_bfd != NULL)
	arch_info = bfd_get_arch_info (current_bfd);
      i = 0;
      while (current_bfd != NULL && i < orl_count)
	{
	  while (map[i].u.abfd == current_bfd)
	    {
	      if (arch_info->bits_per_address == 64)
		{
		  string_length = sprintf (st, "%s", *map[i].name);
		  st += string_length + 1;
		}
	      i++;
	    }
	  current_bfd = current_bfd->next;
	  if (current_bfd != NULL)
	    arch_info = bfd_get_arch_info (current_bfd);
	}

      bfd_bwrite (symbol_table, symbol_table_size, abfd);

      free (symbol_table);
      symbol_table = NULL;

      PRINT20 (fhdr->symoff64, nextoff);
    }
  else 
    PRINT20 (fhdr->symoff64, 0);
  
  return true;
}

boolean
_bfd_xcoff_write_armap (abfd, elength, map, orl_count, stridx)
     bfd *abfd;
     unsigned int elength ATTRIBUTE_UNUSED;
     struct orl *map;
     unsigned int orl_count;
     int stridx;
{
  if (! xcoff_big_format_p (abfd))
    return xcoff_write_armap_old (abfd, elength, map, orl_count, stridx);
  else
    return xcoff_write_armap_big (abfd, elength, map, orl_count, stridx);
}

/* Write out an XCOFF archive.  We always write an entire archive,
   rather than fussing with the freelist and so forth.  */

static boolean
xcoff_write_archive_contents_old (abfd)
     bfd *abfd;
{
  struct xcoff_ar_file_hdr fhdr;
  bfd_size_type count;
  bfd_size_type total_namlen;
  file_ptr *offsets;
  boolean makemap;
  boolean hasobjects;
  ufile_ptr prevoff, nextoff;
  bfd *sub;
  size_t i;
  struct xcoff_ar_hdr ahdr;
  bfd_size_type size;
  char *p;
  char decbuf[XCOFFARMAG_ELEMENT_SIZE + 1];

  memset (&fhdr, 0, sizeof fhdr);
  strncpy (fhdr.magic, XCOFFARMAG, SXCOFFARMAG);
  sprintf (fhdr.firstmemoff, "%d", SIZEOF_AR_FILE_HDR);
  sprintf (fhdr.freeoff, "%d", 0);

  count = 0;
  total_namlen = 0;
  for (sub = abfd->archive_head; sub != NULL; sub = sub->next)
    {
      ++count;
      total_namlen += strlen (normalize_filename (sub)) + 1;
    }
  offsets = (file_ptr *) bfd_alloc (abfd, count * sizeof (file_ptr));
  if (offsets == NULL)
    return false;

  if (bfd_seek (abfd, (file_ptr) SIZEOF_AR_FILE_HDR, SEEK_SET) != 0)
    return false;

  makemap = bfd_has_map (abfd);
  hasobjects = false;
  prevoff = 0;
  nextoff = SIZEOF_AR_FILE_HDR;
  for (sub = abfd->archive_head, i = 0; sub != NULL; sub = sub->next, i++)
    {
      const char *name;
      bfd_size_type namlen;
      struct xcoff_ar_hdr *ahdrp;
      bfd_size_type remaining;

      if (makemap && ! hasobjects)
	{
	  if (bfd_check_format (sub, bfd_object))
	    hasobjects = true;
	}

      name = normalize_filename (sub);
      namlen = strlen (name);

      if (sub->arelt_data != NULL)
	ahdrp = arch_xhdr (sub);
      else
	ahdrp = NULL;

      if (ahdrp == NULL)
	{
	  struct stat s;

	  memset (&ahdr, 0, sizeof ahdr);
	  ahdrp = &ahdr;
	  if (stat (bfd_get_filename (sub), &s) != 0)
	    {
	      bfd_set_error (bfd_error_system_call);
	      return false;
	    }

	  sprintf (ahdrp->size, "%ld", (long) s.st_size);
	  sprintf (ahdrp->date, "%ld", (long) s.st_mtime);
	  sprintf (ahdrp->uid, "%ld", (long) s.st_uid);
	  sprintf (ahdrp->gid, "%ld", (long) s.st_gid);
	  sprintf (ahdrp->mode, "%o", (unsigned int) s.st_mode);

	  if (sub->arelt_data == NULL)
	    {
	      size = sizeof (struct areltdata);
	      sub->arelt_data = bfd_alloc (sub, size);
	      if (sub->arelt_data == NULL)
		return false;
	    }

	  arch_eltdata (sub)->parsed_size = s.st_size;
	}

      sprintf (ahdrp->prevoff, "%ld", (long) prevoff);
      sprintf (ahdrp->namlen, "%ld", (long) namlen);

      /* If the length of the name is odd, we write out the null byte
         after the name as well.  */
      namlen = (namlen + 1) &~ (bfd_size_type) 1;

      remaining = arelt_size (sub);
      size = (SIZEOF_AR_HDR
	      + namlen
	      + SXCOFFARFMAG
	      + remaining);

      BFD_ASSERT (nextoff == bfd_tell (abfd));

      offsets[i] = nextoff;

      prevoff = nextoff;
      nextoff += size + (size & 1);

      sprintf (ahdrp->nextoff, "%ld", (long) nextoff);

      /* We need spaces, not null bytes, in the header.  */
      for (p = (char *) ahdrp; p < (char *) ahdrp + SIZEOF_AR_HDR; p++)
	if (*p == '\0')
	  *p = ' ';

      if ((bfd_bwrite ((PTR) ahdrp, (bfd_size_type) SIZEOF_AR_HDR, abfd)
	   != SIZEOF_AR_HDR)
	  || (bfd_bwrite ((PTR) name, namlen, abfd) != namlen)
	  || (bfd_bwrite ((PTR) XCOFFARFMAG, (bfd_size_type) SXCOFFARFMAG, abfd)
	      != SXCOFFARFMAG))
	return false;

      if (bfd_seek (sub, (file_ptr) 0, SEEK_SET) != 0)
	return false;
      while (remaining != 0)
	{
	  bfd_size_type amt;
	  bfd_byte buffer[DEFAULT_BUFFERSIZE];

	  amt = sizeof buffer;
	  if (amt > remaining)
	    amt = remaining;
	  if (bfd_bread (buffer, amt, sub) != amt
	      || bfd_bwrite (buffer, amt, abfd) != amt)
	    return false;
	  remaining -= amt;
	}

      if ((size & 1) != 0)
	{
	  bfd_byte b;

	  b = '\0';
	  if (bfd_bwrite (&b, (bfd_size_type) 1, abfd) != 1)
	    return false;
	}
    }

  sprintf (fhdr.lastmemoff, "%ld", (long) prevoff);

  /* Write out the member table.  */

  BFD_ASSERT (nextoff == bfd_tell (abfd));
  sprintf (fhdr.memoff, "%ld", (long) nextoff);

  memset (&ahdr, 0, sizeof ahdr);
  sprintf (ahdr.size, "%ld", (long) (XCOFFARMAG_ELEMENT_SIZE + 
				     count * XCOFFARMAG_ELEMENT_SIZE + 
				     total_namlen));
  sprintf (ahdr.prevoff, "%ld", (long) prevoff);
  sprintf (ahdr.date, "%d", 0);
  sprintf (ahdr.uid, "%d", 0);
  sprintf (ahdr.gid, "%d", 0);
  sprintf (ahdr.mode, "%d", 0);
  sprintf (ahdr.namlen, "%d", 0);

  size = (SIZEOF_AR_HDR
	  + XCOFFARMAG_ELEMENT_SIZE
	  + count * XCOFFARMAG_ELEMENT_SIZE
	  + total_namlen
	  + SXCOFFARFMAG);

  prevoff = nextoff;
  nextoff += size + (size & 1);

  if (makemap && hasobjects)
    sprintf (ahdr.nextoff, "%ld", (long) nextoff);
  else
    sprintf (ahdr.nextoff, "%d", 0);

  /* We need spaces, not null bytes, in the header.  */
  for (p = (char *) &ahdr; p < (char *) &ahdr + SIZEOF_AR_HDR; p++)
    if (*p == '\0')
      *p = ' ';

  if ((bfd_bwrite ((PTR) &ahdr, (bfd_size_type) SIZEOF_AR_HDR, abfd)
       != SIZEOF_AR_HDR)
      || (bfd_bwrite ((PTR) XCOFFARFMAG, (bfd_size_type) SXCOFFARFMAG, abfd)
	  != SXCOFFARFMAG))
    return false;

  sprintf (decbuf, "%-12ld", (long) count);
  if (bfd_bwrite ((PTR) decbuf, (bfd_size_type) XCOFFARMAG_ELEMENT_SIZE, abfd)
      != XCOFFARMAG_ELEMENT_SIZE)
    return false;
  for (i = 0; i < (size_t) count; i++)
    {
      sprintf (decbuf, "%-12ld", (long) offsets[i]);
      if (bfd_bwrite ((PTR) decbuf, (bfd_size_type) XCOFFARMAG_ELEMENT_SIZE, 
		      abfd) != XCOFFARMAG_ELEMENT_SIZE)
	return false;
    }
  for (sub = abfd->archive_head; sub != NULL; sub = sub->next)
    {
      const char *name;
      bfd_size_type namlen;

      name = normalize_filename (sub);
      namlen = strlen (name);
      if (bfd_bwrite ((PTR) name, namlen + 1, abfd) != namlen + 1)
	return false;
    }
  if ((size & 1) != 0)
    {
      bfd_byte b;

      b = '\0';
      if (bfd_bwrite ((PTR) &b, (bfd_size_type) 1, abfd) != 1)
	return false;
    }

  /* Write out the armap, if appropriate.  */
  if (! makemap || ! hasobjects)
    sprintf (fhdr.symoff, "%d", 0);
  else
    {
      BFD_ASSERT (nextoff == bfd_tell (abfd));
      sprintf (fhdr.symoff, "%ld", (long) nextoff);
      bfd_ardata (abfd)->tdata = (PTR) &fhdr;
      if (! _bfd_compute_and_write_armap (abfd, 0))
	return false;
    }

  /* Write out the archive file header.  */

  /* We need spaces, not null bytes, in the header.  */
  for (p = (char *) &fhdr; p < (char *) &fhdr + SIZEOF_AR_FILE_HDR; p++)
    if (*p == '\0')
      *p = ' ';

  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0
      || (bfd_bwrite ((PTR) &fhdr, (bfd_size_type) SIZEOF_AR_FILE_HDR, abfd)
	  != SIZEOF_AR_FILE_HDR))
    return false;

  return true;
}

static boolean
xcoff_write_archive_contents_big (abfd)
     bfd *abfd;
{
  struct xcoff_ar_file_hdr_big fhdr;
  bfd_size_type count;
  bfd_size_type total_namlen;
  file_ptr *offsets;
  boolean makemap;
  boolean hasobjects;
  ufile_ptr prevoff, nextoff;
  bfd *current_bfd;
  size_t i;
  struct xcoff_ar_hdr_big *hdr, ahdr;
  bfd_size_type size;
  bfd_byte *member_table, *mt;
  bfd_vma member_table_size;

  memcpy (fhdr.magic, XCOFFARMAGBIG, SXCOFFARMAG);
  PRINT20 (fhdr.firstmemoff, SIZEOF_AR_FILE_HDR_BIG);
  PRINT20 (fhdr.freeoff, 0);

  /* Calculate count and total_namlen */
  for (current_bfd = abfd->archive_head, count = 0, total_namlen = 0; 
       current_bfd != NULL; 
       current_bfd = current_bfd->next, count++)
    total_namlen += strlen (normalize_filename (current_bfd)) + 1;

  offsets = NULL;
  if (count)
    {
      offsets = (file_ptr *) bfd_malloc (count * sizeof (file_ptr));
      if (offsets == NULL)
	return false;
    }
  if (bfd_seek (abfd, (file_ptr) SIZEOF_AR_FILE_HDR_BIG, SEEK_SET) != 0)
    return false;

  makemap = bfd_has_map (abfd);
  hasobjects = false;
  prevoff = 0;
  nextoff = SIZEOF_AR_FILE_HDR_BIG;
  for (current_bfd = abfd->archive_head, i = 0; 
       current_bfd != NULL; 
       current_bfd = current_bfd->next, i++)
    {
      const char *name;
      bfd_size_type namlen;
      struct xcoff_ar_hdr_big *ahdrp;
      bfd_size_type remaining;

      if (makemap && ! hasobjects)
	{
	  if (bfd_check_format (current_bfd, bfd_object))
	    hasobjects = true;
	}

      name = normalize_filename (current_bfd);
      namlen = strlen (name);

      if (current_bfd->arelt_data != NULL)
	ahdrp = arch_xhdr_big (current_bfd);
      else
	ahdrp = NULL;

      if (ahdrp == NULL)
	{
	  struct stat s;

	  ahdrp = &ahdr;
	  /* XXX This should actually be a call to stat64 (at least on
	     32-bit machines).  
	     XXX This call will fail if the original object is not found.  */
	  if (stat (bfd_get_filename (current_bfd), &s) != 0)
	    {
	      bfd_set_error (bfd_error_system_call);
	      return false;
	    }

	  PRINT20 (ahdrp->size, s.st_size);
	  PRINT12 (ahdrp->date, s.st_mtime);
	  PRINT12 (ahdrp->uid,  s.st_uid);
	  PRINT12 (ahdrp->gid,  s.st_gid);
	  PRINT12_OCTAL (ahdrp->mode, s.st_mode);

	  if (current_bfd->arelt_data == NULL)
	    {
	      size = sizeof (struct areltdata);
	      current_bfd->arelt_data = bfd_alloc (current_bfd, size);
	      if (current_bfd->arelt_data == NULL)
		return false;
	    }

	  arch_eltdata (current_bfd)->parsed_size = s.st_size;
	}

      PRINT20 (ahdrp->prevoff, prevoff);
      PRINT4 (ahdrp->namlen, namlen);

      /* If the length of the name is odd, we write out the null byte
         after the name as well.  */
      namlen = (namlen + 1) &~ (bfd_size_type) 1;

      remaining = arelt_size (current_bfd);
      size = (SIZEOF_AR_HDR_BIG
	      + namlen
	      + SXCOFFARFMAG
	      + remaining);

      BFD_ASSERT (nextoff == bfd_tell (abfd));

      offsets[i] = nextoff;

      prevoff = nextoff;
      nextoff += size + (size & 1);

      PRINT20 (ahdrp->nextoff, nextoff);

      if ((bfd_bwrite ((PTR) ahdrp, (bfd_size_type) SIZEOF_AR_HDR_BIG, abfd)
	   != SIZEOF_AR_HDR_BIG)
	  || bfd_bwrite ((PTR) name, (bfd_size_type) namlen, abfd) != namlen
	  || (bfd_bwrite ((PTR) XCOFFARFMAG, (bfd_size_type) SXCOFFARFMAG, 
			  abfd) != SXCOFFARFMAG))
	return false;

      if (bfd_seek (current_bfd, (file_ptr) 0, SEEK_SET) != 0)
	return false;
      while (remaining != 0)
	{
	  bfd_size_type amt;
	  bfd_byte buffer[DEFAULT_BUFFERSIZE];

	  amt = sizeof buffer;
	  if (amt > remaining)
	    amt = remaining;
	  if (bfd_bread (buffer, amt, current_bfd) != amt
	      || bfd_bwrite (buffer, amt, abfd) != amt)
	    return false;
	  remaining -= amt;
	}

      if ((size & 1) != 0)
	{
	  bfd_byte b;

	  b = '\0';
	  if (bfd_bwrite (&b, (bfd_size_type) 1, abfd) != 1)
	    return false;
	}
    }

  PRINT20 (fhdr.lastmemoff, prevoff);

  /* Write out the member table.  
     Layout : 

     standard big archive header
     0x0000                   ar_size   [0x14]
     0x0014                   ar_nxtmem [0x14]
     0x0028                   ar_prvmem [0x14]
     0x003C                   ar_date   [0x0C]
     0x0048                   ar_uid    [0x0C]
     0x0054                   ar_gid    [0x0C]
     0x0060                   ar_mod    [0x0C]
     0x006C                   ar_namelen[0x04]
     0x0070                   ar_fmag   [0x02]

     Member table 
     0x0072                   count     [0x14]
     0x0086                   offsets   [0x14 * counts]
     0x0086 + 0x14 * counts   names     [??]
     ??                       pad to even bytes.
   */

  BFD_ASSERT (nextoff == bfd_tell (abfd));

  member_table_size = (SIZEOF_AR_HDR_BIG
		       + SXCOFFARFMAG
		       + XCOFFARMAGBIG_ELEMENT_SIZE
		       + count * XCOFFARMAGBIG_ELEMENT_SIZE
		       + total_namlen);

  member_table_size += member_table_size & 1;
  member_table = NULL;
  member_table = (bfd_byte *) bfd_malloc (member_table_size);
  if (member_table == NULL)
    return false;
  memset (member_table, 0, member_table_size);

  hdr = (struct xcoff_ar_hdr_big *) member_table;

  PRINT20 (hdr->size, (XCOFFARMAGBIG_ELEMENT_SIZE + 
		       count * XCOFFARMAGBIG_ELEMENT_SIZE + 
		       total_namlen + (total_namlen & 1)));
  if (makemap && hasobjects) 
    PRINT20 (hdr->nextoff, nextoff + member_table_size);
  else
    PRINT20 (hdr->nextoff, 0);
  PRINT20 (hdr->prevoff, prevoff);
  PRINT12 (hdr->date, 0);
  PRINT12 (hdr->uid, 0);
  PRINT12 (hdr->gid, 0);
  PRINT12 (hdr->mode, 0);
  PRINT4 (hdr->namlen, 0);
  
  mt = member_table + SIZEOF_AR_HDR_BIG;
  memcpy (mt, XCOFFARFMAG, SXCOFFARFMAG);
  mt += SXCOFFARFMAG;

  PRINT20 (mt, count);
  mt += XCOFFARMAGBIG_ELEMENT_SIZE;
  for (i = 0; i < (size_t) count; i++)
    {
      PRINT20 (mt, offsets[i]);
      mt += XCOFFARMAGBIG_ELEMENT_SIZE;
    }

  if (count) 
    {
      free (offsets);
      offsets = NULL;
    }

  for (current_bfd = abfd->archive_head; current_bfd != NULL; 
       current_bfd = current_bfd->next)
    {
      const char *name;
      size_t namlen;

      name = normalize_filename (current_bfd);
      namlen = sprintf(mt, "%s", name);
      mt += namlen + 1;
    }
  
  if (bfd_bwrite (member_table, member_table_size, abfd) != member_table_size)
    return false;

  free (member_table);
  member_table = NULL;

  PRINT20 (fhdr.memoff, nextoff);

  prevoff = nextoff;
  nextoff += member_table_size;

  /* Write out the armap, if appropriate.  */

  if (! makemap || ! hasobjects) 
    PRINT20 (fhdr.symoff, 0);
  else
    {
      BFD_ASSERT (nextoff == bfd_tell (abfd));

      /* Save nextoff in fhdr.symoff so the armap routine can use it.  */
      PRINT20 (fhdr.symoff, nextoff);
      
      bfd_ardata (abfd)->tdata = (PTR) &fhdr;
      if (! _bfd_compute_and_write_armap (abfd, 0))
	return false;
    }

  /* Write out the archive file header.  */

  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0
      || (bfd_bwrite ((PTR) &fhdr, (bfd_size_type) SIZEOF_AR_FILE_HDR_BIG, 
		      abfd) != SIZEOF_AR_FILE_HDR_BIG))
    return false;
  
  return true;
}

boolean
_bfd_xcoff_write_archive_contents (abfd)
     bfd *abfd;
{
  if (! xcoff_big_format_p (abfd))
    return xcoff_write_archive_contents_old (abfd);
  else
    return xcoff_write_archive_contents_big (abfd);
}

/* We can't use the usual coff_sizeof_headers routine, because AIX
   always uses an a.out header.  */

int
_bfd_xcoff_sizeof_headers (abfd, reloc)
     bfd *abfd;
     boolean reloc ATTRIBUTE_UNUSED;
{
  int size;

  size = FILHSZ;
  if (xcoff_data (abfd)->full_aouthdr)
    size += AOUTSZ;
  else
    size += SMALL_AOUTSZ;
  size += abfd->section_count * SCNHSZ;
  return size;
}

/* Routines to swap information in the XCOFF .loader section.  If we
   ever need to write an XCOFF loader, this stuff will need to be
   moved to another file shared by the linker (which XCOFF calls the
   ``binder'') and the loader.  */

/* Swap in the ldhdr structure.  */

static void
xcoff_swap_ldhdr_in (abfd, s, dst)
     bfd *abfd;
     const PTR s;
     struct internal_ldhdr *dst;
{
  const struct external_ldhdr *src = (const struct external_ldhdr *) s;

  dst->l_version = bfd_get_32 (abfd, src->l_version);
  dst->l_nsyms = bfd_get_32 (abfd, src->l_nsyms);
  dst->l_nreloc = bfd_get_32 (abfd, src->l_nreloc);
  dst->l_istlen = bfd_get_32 (abfd, src->l_istlen);
  dst->l_nimpid = bfd_get_32 (abfd, src->l_nimpid);
  dst->l_impoff = bfd_get_32 (abfd, src->l_impoff);
  dst->l_stlen = bfd_get_32 (abfd, src->l_stlen);
  dst->l_stoff = bfd_get_32 (abfd, src->l_stoff);
}

/* Swap out the ldhdr structure.  */

static void
xcoff_swap_ldhdr_out (abfd, src, d)
     bfd *abfd;
     const struct internal_ldhdr *src;
     PTR d;
{
  struct external_ldhdr *dst = (struct external_ldhdr *) d;

  bfd_put_32 (abfd, (bfd_vma) src->l_version, dst->l_version);
  bfd_put_32 (abfd, src->l_nsyms, dst->l_nsyms);
  bfd_put_32 (abfd, src->l_nreloc, dst->l_nreloc);
  bfd_put_32 (abfd, src->l_istlen, dst->l_istlen);
  bfd_put_32 (abfd, src->l_nimpid, dst->l_nimpid);
  bfd_put_32 (abfd, src->l_impoff, dst->l_impoff);
  bfd_put_32 (abfd, src->l_stlen, dst->l_stlen);
  bfd_put_32 (abfd, src->l_stoff, dst->l_stoff);
}

/* Swap in the ldsym structure.  */

static void
xcoff_swap_ldsym_in (abfd, s, dst)
     bfd *abfd;
     const PTR s;
     struct internal_ldsym *dst;
{
  const struct external_ldsym *src = (const struct external_ldsym *) s;

  if (bfd_get_32 (abfd, src->_l._l_l._l_zeroes) != 0) {
    memcpy (dst->_l._l_name, src->_l._l_name, SYMNMLEN);
  } else {
    dst->_l._l_l._l_zeroes = 0;
    dst->_l._l_l._l_offset = bfd_get_32 (abfd, src->_l._l_l._l_offset);
  }
  dst->l_value = bfd_get_32 (abfd, src->l_value);
  dst->l_scnum = bfd_get_16 (abfd, src->l_scnum);
  dst->l_smtype = bfd_get_8 (abfd, src->l_smtype);
  dst->l_smclas = bfd_get_8 (abfd, src->l_smclas);
  dst->l_ifile = bfd_get_32 (abfd, src->l_ifile);
  dst->l_parm = bfd_get_32 (abfd, src->l_parm);
}

/* Swap out the ldsym structure.  */

static void
xcoff_swap_ldsym_out (abfd, src, d)
     bfd *abfd;
     const struct internal_ldsym *src;
     PTR d;
{
  struct external_ldsym *dst = (struct external_ldsym *) d;

  if (src->_l._l_l._l_zeroes != 0)
    memcpy (dst->_l._l_name, src->_l._l_name, SYMNMLEN);
  else
    {
      bfd_put_32 (abfd, (bfd_vma) 0, dst->_l._l_l._l_zeroes);
      bfd_put_32 (abfd, (bfd_vma) src->_l._l_l._l_offset,
		  dst->_l._l_l._l_offset);
    }
  bfd_put_32 (abfd, src->l_value, dst->l_value);
  bfd_put_16 (abfd, (bfd_vma) src->l_scnum, dst->l_scnum);
  bfd_put_8 (abfd, src->l_smtype, dst->l_smtype);
  bfd_put_8 (abfd, src->l_smclas, dst->l_smclas);
  bfd_put_32 (abfd, src->l_ifile, dst->l_ifile);
  bfd_put_32 (abfd, src->l_parm, dst->l_parm);
}

/* Swap in the ldrel structure.  */

static void
xcoff_swap_ldrel_in (abfd, s, dst)
     bfd *abfd;
     const PTR s;
     struct internal_ldrel *dst;
{
  const struct external_ldrel *src = (const struct external_ldrel *) s;

  dst->l_vaddr = bfd_get_32 (abfd, src->l_vaddr);
  dst->l_symndx = bfd_get_32 (abfd, src->l_symndx);
  dst->l_rtype = bfd_get_16 (abfd, src->l_rtype);
  dst->l_rsecnm = bfd_get_16 (abfd, src->l_rsecnm);
}

/* Swap out the ldrel structure.  */

static void
xcoff_swap_ldrel_out (abfd, src, d)
     bfd *abfd;
     const struct internal_ldrel *src;
     PTR d;
{
  struct external_ldrel *dst = (struct external_ldrel *) d;

  bfd_put_32 (abfd, src->l_vaddr, dst->l_vaddr);
  bfd_put_32 (abfd, src->l_symndx, dst->l_symndx);
  bfd_put_16 (abfd, (bfd_vma) src->l_rtype, dst->l_rtype);
  bfd_put_16 (abfd, (bfd_vma) src->l_rsecnm, dst->l_rsecnm);
}



/* This is the relocation function for the RS/6000/POWER/PowerPC.
   This is currently the only processor which uses XCOFF; I hope that
   will never change.  */

static boolean
xcoff_ppc_relocate_section (output_bfd, info, input_bfd,
			    input_section, contents, relocs, syms,
			    sections)
     bfd *output_bfd;
     struct bfd_link_info *info;
     bfd *input_bfd;
     asection *input_section;
     bfd_byte *contents;
     struct internal_reloc *relocs;
     struct internal_syment *syms;
     asection **sections;
{
  struct internal_reloc *rel;
  struct internal_reloc *relend;

  rel = relocs;
  relend = rel + input_section->reloc_count;

  for (; rel < relend; rel++)
    {
      long symndx;
      struct xcoff_link_hash_entry *h;
      struct internal_syment *sym;
      bfd_vma addend;
      bfd_vma val;
      struct reloc_howto_struct howto;
      bfd_reloc_status_type rstat;

      /* Relocation type R_REF is a special relocation type which is
         merely used to prevent garbage collection from occurring for
         the csect including the symbol which it references.  */
      if (rel->r_type == R_REF)
	continue;

      symndx = rel->r_symndx;

      if (symndx == -1)
	{
	  h = NULL;
	  sym = NULL;
	  addend = 0;
	}
      else
	{
	  h = obj_xcoff_sym_hashes (input_bfd)[symndx];
	  sym = syms + symndx;
	  addend = - sym->n_value;

	}

      /* We build the howto information on the fly.  */

      howto.type = rel->r_type;
      howto.rightshift = 0;
      howto.size = 2;
      howto.bitsize = (rel->r_size & 0x1f) + 1;
      howto.pc_relative = false;
      howto.bitpos = 0;
      if ((rel->r_size & 0x80) != 0)
	howto.complain_on_overflow = complain_overflow_signed;
      else
	howto.complain_on_overflow = complain_overflow_bitfield;
      howto.special_function = NULL;
      howto.name = "internal";
      howto.partial_inplace = true;
      if (howto.bitsize == 32)
	howto.src_mask = howto.dst_mask = 0xffffffff;
      else
	{
	  howto.src_mask = howto.dst_mask = (1 << howto.bitsize) - 1;
	  if (howto.bitsize == 16)
	    howto.size = 1;
	}
      howto.pcrel_offset = false;

      val = 0;

      if (h == NULL)
	{
	  asection *sec;

	  if (symndx == -1)
	    {
	      sec = bfd_abs_section_ptr;
	      val = 0;
	    }
	  else
	    {
	      sec = sections[symndx];
	      /* Hack to make sure we use the right TOC anchor value
                 if this reloc is against the TOC anchor.  */

	      if (sec->name[3] == '0'
			  && strcmp (sec->name, ".tc0") == 0)
		{
		  val = xcoff_data (output_bfd)->toc;
		}
	      else
		{
		  val = (sec->output_section->vma
			 + sec->output_offset
			 + sym->n_value
			 - sec->vma);
		}
	    }
	}
      else
	{
	  if (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
	    {
	      asection *sec;

	      sec = h->root.u.def.section;
	      val = (h->root.u.def.value
		     + sec->output_section->vma
		     + sec->output_offset);
	    }
	  else if (h->root.type == bfd_link_hash_common)
	    {
	      asection *sec;

	      sec = h->root.u.c.p->section;
	      val = (sec->output_section->vma
		     + sec->output_offset);
	    }
	  else if ((h->flags & XCOFF_DEF_DYNAMIC) != 0
		   || (h->flags & XCOFF_IMPORT) != 0)
	    {
	      /* Every symbol in a shared object is defined somewhere.  */
	      val = 0;
	    }
	  else if (! info->relocateable)
	    {
	      if (! ((*info->callbacks->undefined_symbol)
		     (info, h->root.root.string, input_bfd, input_section,
		      rel->r_vaddr - input_section->vma, true)))
		return false;

	      /* Don't try to process the reloc.  It can't help, and
                 it may generate another error.  */
	      continue;
	    }
	}

      /* I took the relocation type definitions from two documents:
	 the PowerPC AIX Version 4 Application Binary Interface, First
	 Edition (April 1992), and the PowerOpen ABI, Big-Endian
	 32-Bit Hardware Implementation (June 30, 1994).  Differences
	 between the documents are noted below.  */

      switch (rel->r_type)
	{
	case R_RTB:
	case R_RRTBI:
	case R_RRTBA:
	  /* These relocs are defined by the PowerPC ABI to be
             relative branches which use half of the difference
             between the symbol and the program counter.  I can't
             quite figure out when this is useful.  These relocs are
             not defined by the PowerOpen ABI.  */
	default:
	  (*_bfd_error_handler)
	    (_("%s: unsupported relocation type 0x%02x"),
	     bfd_archive_filename (input_bfd), (unsigned int) rel->r_type);
	  bfd_set_error (bfd_error_bad_value);
	  return false;
	case R_POS:
	  /* Simple positive relocation.  */
	  break;
	case R_NEG:
	  /* Simple negative relocation.  */
	  val = - val;
	  break;
	case R_REL:
	  /* Simple PC relative relocation.  */
	  howto.pc_relative = true;
	  break;
	case R_TOC:
	  /* TOC relative relocation.  The value in the instruction in
             the input file is the offset from the input file TOC to
             the desired location.  We want the offset from the final
             TOC to the desired location.  We have:
	         isym = iTOC + in
		 iinsn = in + o
		 osym = oTOC + on
		 oinsn = on + o
	     so we must change insn by on - in.
	     */
	case R_GL:
	  /* Global linkage relocation.  The value of this relocation
             is the address of the entry in the TOC section.  */
	case R_TCL:
	  /* Local object TOC address.  I can't figure out the
             difference between this and case R_GL.  */
	case R_TRL:
	  /* TOC relative relocation.  A TOC relative load instruction
             which may be changed to a load address instruction.
             FIXME: We don't currently implement this optimization.  */
	case R_TRLA:
	  /* TOC relative relocation.  This is a TOC relative load
             address instruction which may be changed to a load
             instruction.  FIXME: I don't know if this is the correct
             implementation.  */
	  if (h != NULL && h->smclas != XMC_TD)
	    {
	      if (h->toc_section == NULL)
		{
		  (*_bfd_error_handler)
		    (_("%s: TOC reloc at 0x%x to symbol `%s' with no TOC entry"),
		     bfd_archive_filename (input_bfd), rel->r_vaddr,
		     h->root.root.string);
		  bfd_set_error (bfd_error_bad_value);
		  return false;
		}

	      BFD_ASSERT ((h->flags & XCOFF_SET_TOC) == 0);
	      val = (h->toc_section->output_section->vma
		     + h->toc_section->output_offset);
	    }

	  val = ((val - xcoff_data (output_bfd)->toc)
		 - (sym->n_value - xcoff_data (input_bfd)->toc));
	  addend = 0;
	  break;
	case R_BA:
	  /* Absolute branch.  We don't want to mess with the lower
             two bits of the instruction.  */
	case R_CAI:
	  /* The PowerPC ABI defines this as an absolute call which
             may be modified to become a relative call.  The PowerOpen
             ABI does not define this relocation type.  */
	case R_RBA:
	  /* Absolute branch which may be modified to become a
             relative branch.  */
	case R_RBAC:
	  /* The PowerPC ABI defines this as an absolute branch to a
             fixed address which may be modified to an absolute branch
             to a symbol.  The PowerOpen ABI does not define this
             relocation type.  */
	case R_RBRC:
	  /* The PowerPC ABI defines this as an absolute branch to a
             fixed address which may be modified to a relative branch.
             The PowerOpen ABI does not define this relocation type.  */
	  howto.src_mask &= ~3;
	  howto.dst_mask = howto.src_mask;
	  break;
	case R_BR:
	  /* Relative branch.  We don't want to mess with the lower
             two bits of the instruction.  */
	case R_CREL:
	  /* The PowerPC ABI defines this as a relative call which may
             be modified to become an absolute call.  The PowerOpen
             ABI does not define this relocation type.  */
	case R_RBR:
	  /* A relative branch which may be modified to become an
             absolute branch.  FIXME: We don't implement this,
             although we should for symbols of storage mapping class
             XMC_XO.  */
	  howto.pc_relative = true;
	  howto.src_mask &= ~3;
	  howto.dst_mask = howto.src_mask;
	  break;
	case R_RL:
	  /* The PowerPC AIX ABI describes this as a load which may be
             changed to a load address.  The PowerOpen ABI says this
             is the same as case R_POS.  */
	  break;
	case R_RLA:
	  /* The PowerPC AIX ABI describes this as a load address
             which may be changed to a load.  The PowerOpen ABI says
             this is the same as R_POS.  */
	  break;
	}

      /* If we see an R_BR or R_RBR reloc which is jumping to global
         linkage code, and it is followed by an appropriate cror nop
         instruction, we replace the cror with lwz r2,20(r1).  This
         restores the TOC after the glink code.  Contrariwise, if the
         call is followed by a lwz r2,20(r1), but the call is not
         going to global linkage code, we can replace the load with a
         cror.  */
      if ((rel->r_type == R_BR || rel->r_type == R_RBR)
	  && h != NULL
	  && h->root.type == bfd_link_hash_defined
	  && (rel->r_vaddr - input_section->vma + 8
	      <= input_section->_cooked_size))
	{
	  bfd_byte *pnext;
	  unsigned long next;

	  pnext = contents + (rel->r_vaddr - input_section->vma) + 4;
	  next = bfd_get_32 (input_bfd, pnext);

	  /* The _ptrgl function is magic.  It is used by the AIX
             compiler to call a function through a pointer.  */
	  if (h->smclas == XMC_GL
	      || strcmp (h->root.root.string, "._ptrgl") == 0)
	    {
	      if (next == 0x4def7b82		/* cror 15,15,15 */
		  || next == 0x4ffffb82		/* cror 31,31,31 */
		  || next == 0x60000000)	/* ori r0,r0,0 */
		bfd_put_32 (input_bfd,
			    (bfd_vma) 0x80410014, /* lwz r1,20(r1) */
			    pnext);
	    }
	  else
	    {
	      if (next == 0x80410014)		/* lwz r1,20(r1) */
		bfd_put_32 (input_bfd,
			    (bfd_vma) 0x60000000, /* ori r0,r0,0 */
			    pnext);
	    }
	}

      /* A PC relative reloc includes the section address.  */
      if (howto.pc_relative)
	addend += input_section->vma;

      rstat = _bfd_final_link_relocate (&howto, input_bfd, input_section,
					contents,
					rel->r_vaddr - input_section->vma,
					val, addend);

      switch (rstat)
	{
	default:
	  abort ();
	case bfd_reloc_ok:
	  break;
	case bfd_reloc_overflow:
	  {
	    const char *name;
	    char buf[SYMNMLEN + 1];
	    char howto_name[10];

	    if (symndx == -1)
	      name = "*ABS*";
	    else if (h != NULL)
	      name = h->root.root.string;
	    else
	      {
		name = _bfd_coff_internal_syment_name (input_bfd, sym, buf);

		if (name == NULL)
		  return false;
	      }
	    sprintf (howto_name, "0x%02x", rel->r_type);

	    if (! ((*info->callbacks->reloc_overflow)
		   (info, name, howto_name, (bfd_vma) 0, input_bfd,
		    input_section, rel->r_vaddr - input_section->vma)))
	      return false;
	  }
	}
    }

  return true;
}

static boolean
_bfd_xcoff_put_ldsymbol_name (abfd, ldinfo, ldsym, name)
     bfd *abfd ATTRIBUTE_UNUSED;
	 struct xcoff_loader_info *ldinfo;
	 struct internal_ldsym *ldsym;
	 const char *name;
{
  size_t len;
  len = strlen (name);

  if (len <= SYMNMLEN)
    strncpy (ldsym->_l._l_name, name, SYMNMLEN);
  else
    {
      if (ldinfo->string_size + len + 3 > ldinfo->string_alc)
	{
	  bfd_size_type newalc;
	  bfd_byte *newstrings;

	  newalc = ldinfo->string_alc * 2;
	  if (newalc == 0)
	    newalc = 32;
	  while (ldinfo->string_size + len + 3 > newalc)
	    newalc *= 2;

	  newstrings = ((bfd_byte *)
			bfd_realloc ((PTR) ldinfo->strings, newalc));
	  if (newstrings == NULL)
	    {
	      ldinfo->failed = true;
	      return false;
	    }
	  ldinfo->string_alc = newalc;
	  ldinfo->strings = newstrings;
	}

      bfd_put_16 (ldinfo->output_bfd, (bfd_vma) (len + 1),
		  ldinfo->strings + ldinfo->string_size);
      strcpy (ldinfo->strings + ldinfo->string_size + 2, name);
      ldsym->_l._l_l._l_zeroes = 0;
      ldsym->_l._l_l._l_offset = ldinfo->string_size + 2;
      ldinfo->string_size += len + 3;
    }

  return true;
}

static boolean
_bfd_xcoff_put_symbol_name (bfd *abfd, struct bfd_strtab_hash *strtab,
			    struct internal_syment *sym,
			    const char *name)
{
  if (strlen (name) <= SYMNMLEN)
    {
      strncpy (sym->_n._n_name, name, SYMNMLEN);
    }
  else
    {
      boolean hash;
      bfd_size_type indx;

      hash = true;
      if ((abfd->flags & BFD_TRADITIONAL_FORMAT) != 0)
	hash = false;
      indx = _bfd_stringtab_add (strtab, name, hash, false);
      if (indx == (bfd_size_type) -1)
	return false;
      sym->_n._n_n._n_zeroes = 0;
      sym->_n._n_n._n_offset = STRING_SIZE_SIZE + indx;
    }
  return true;
}

static asection *
xcoff_create_csect_from_smclas (abfd, aux, symbol_name)
     bfd *abfd;
     union internal_auxent *aux;
     const char *symbol_name;
{
  asection *return_value = NULL;

  /* .sv64 = x_smclas == 17
     This is an invalid csect for 32 bit apps.  */
  static const char *names[19] =
  {
    ".pr", ".ro", ".db", ".tc", ".ua", ".rw", ".gl", ".xo",
    ".sv", ".bs", ".ds", ".uc", ".ti", ".tb", NULL, ".tc0",
    ".td", NULL, ".sv3264"
  };

  if ((19 >= aux->x_csect.x_smclas) &&
      (NULL != names[aux->x_csect.x_smclas]))
    {
      return_value = bfd_make_section_anyway
	(abfd, names[aux->x_csect.x_smclas]);
    }
  else
    {
      (*_bfd_error_handler)
	(_("%s: symbol `%s' has unrecognized smclas %d"),
	 bfd_archive_filename (abfd), symbol_name, aux->x_csect.x_smclas);
      bfd_set_error (bfd_error_bad_value);
    }

  return return_value;
}

static boolean
xcoff_is_lineno_count_overflow (abfd, value)
    bfd *abfd ATTRIBUTE_UNUSED;
	bfd_vma value;
{
  if (0xffff <= value)
    return true;

  return false;
}

static boolean
xcoff_is_reloc_count_overflow (abfd, value)
    bfd *abfd ATTRIBUTE_UNUSED;
	bfd_vma value;
{
  if (0xffff <= value)
    return true;

  return false;
}

static bfd_vma
xcoff_loader_symbol_offset (abfd, ldhdr)
    bfd *abfd;
    struct internal_ldhdr *ldhdr ATTRIBUTE_UNUSED;
{
  return bfd_xcoff_ldhdrsz(abfd);
}

static bfd_vma
xcoff_loader_reloc_offset (abfd, ldhdr)
    bfd *abfd;
    struct internal_ldhdr *ldhdr;
{
  return bfd_xcoff_ldhdrsz(abfd) +
    (ldhdr->l_nsyms * bfd_xcoff_ldsymsz(abfd));
}

static boolean 
xcoff_generate_rtinit  (abfd, init, fini)
     bfd *abfd;
     const char *init;
     const char *fini;
{
  bfd_byte filehdr_ext[FILHSZ];
  bfd_byte scnhdr_ext[SCNHSZ];
  bfd_byte syment_ext[SYMESZ * 8];
  bfd_byte reloc_ext[RELSZ * 2];
  bfd_byte *data_buffer;
  bfd_size_type data_buffer_size;
  bfd_byte *string_table = NULL, *st_tmp = NULL;
  bfd_size_type string_table_size;
  bfd_vma val;
  size_t initsz, finisz;
  struct internal_filehdr filehdr;
  struct internal_scnhdr scnhdr;
  struct internal_syment syment;
  union internal_auxent auxent;
  struct internal_reloc reloc;
  
  char *data_name = ".data";
  char *rtinit_name = "__rtinit";
  
  if (! bfd_xcoff_rtinit_size (abfd) 
      || (init == NULL && fini == NULL))
    return false;

  initsz = (init == NULL ? 0 : 1 + strlen (init));
  finisz = (fini == NULL ? 0 : 1 + strlen (fini));

  /* file header */
  memset (filehdr_ext, 0, FILHSZ);
  memset (&filehdr, 0, sizeof (struct internal_filehdr));
  filehdr.f_magic = bfd_xcoff_magic_number (abfd);
  filehdr.f_nscns = 1; 
  filehdr.f_timdat = 0;
  filehdr.f_nsyms = 0;  /* at least 6, no more than 8 */
  filehdr.f_symptr = 0; /* set below */
  filehdr.f_opthdr = 0;
  filehdr.f_flags = 0;

  /* section header */
  memset (scnhdr_ext, 0, SCNHSZ);
  memset (&scnhdr, 0, sizeof (struct internal_scnhdr));
  memcpy (scnhdr.s_name, data_name, strlen (data_name));
  scnhdr.s_paddr = 0;
  scnhdr.s_vaddr = 0;
  scnhdr.s_size = 0;    /* set below */
  scnhdr.s_scnptr = FILHSZ + SCNHSZ;
  scnhdr.s_relptr = 0;  /* set below */
  scnhdr.s_lnnoptr = 0;
  scnhdr.s_nreloc = 0;  /* either 1 or 2 */
  scnhdr.s_nlnno = 0;
  scnhdr.s_flags = STYP_DATA;

  /* .data 
     0x0000           0x00000000 : rtl
     0x0004           0x00000010 : offset to init, or 0
     0x0008           0x00000028 : offset to fini, or 0
     0x000C           0x0000000C : size of descriptor 
     0x0010           0x00000000 : init, needs a reloc
     0x0014           0x00000040 : offset to init name
     0x0018           0x00000000 : flags, padded to a word
     0x001C           0x00000000 : empty init
     0x0020           0x00000000 : 
     0x0024           0x00000000 : 
     0x0028           0x00000000 : fini, needs a reloc
     0x002C           0x00000??? : offset to fini name
     0x0030           0x00000000 : flags, padded to a word
     0x0034           0x00000000 : empty fini
     0x0038           0x00000000 : 
     0x003C           0x00000000 : 
     0x0040           init name
     0x0040 + initsz  fini name */

  data_buffer_size = 0x0040 + initsz + finisz;
  data_buffer_size += (data_buffer_size & 7) ? 8 - (data_buffer_size & 7) : 0;
  data_buffer = NULL;
  data_buffer = (bfd_byte *) bfd_malloc (data_buffer_size);
  if (data_buffer == NULL)
    return false;
  
  memset (data_buffer, 0, data_buffer_size);

  if (initsz) 
    {
      val = 0x10;
      bfd_h_put_32 (abfd, val, &data_buffer[0x04]);
      val = 0x40;
      bfd_h_put_32 (abfd, val, &data_buffer[0x14]);
      memcpy (&data_buffer[val], init, initsz);
    }

  if (finisz) 
    {
      val = 0x28;
      bfd_h_put_32 (abfd, val, &data_buffer[0x08]);
      val = 0x40 + initsz;
      bfd_h_put_32 (abfd, val, &data_buffer[0x2C]);
      memcpy (&data_buffer[val], fini, finisz);
    }

  val = 0x0C;
  bfd_h_put_32 (abfd, val, &data_buffer[0x0C]);

  scnhdr.s_size = data_buffer_size;

  /* string table */
  string_table_size = 0;
  if (initsz > 9) 
    string_table_size += initsz;
  if (finisz > 9)
    string_table_size += finisz;
  if (string_table_size)
    {
      string_table_size += 4;
      string_table = (bfd_byte *)bfd_malloc (string_table_size);
      memset (string_table, 0, string_table_size);
      val = string_table_size;
      bfd_h_put_32 (abfd, val, &string_table[0]);
      st_tmp = string_table + 4;
    }
  
  /* symbols 
     0. .data csect
     2. __rtinit
     4. init function 
     6. fini function */
  memset (syment_ext, 0, 8 * SYMESZ);
  memset (reloc_ext, 0, 2 * RELSZ);

  /* .data csect */
  memset (&syment, 0, sizeof (struct internal_syment));
  memset (&auxent, 0, sizeof (union internal_auxent));
  memcpy (syment._n._n_name, data_name, strlen (data_name));
  syment.n_scnum = 1;
  syment.n_sclass = C_HIDEXT;
  syment.n_numaux = 1;
  auxent.x_csect.x_scnlen.l = data_buffer_size;
  auxent.x_csect.x_smtyp = 3 << 3 | XTY_SD;
  auxent.x_csect.x_smclas = XMC_RW;
  bfd_coff_swap_sym_out (abfd, &syment, 
			 &syment_ext[filehdr.f_nsyms * SYMESZ]);
  bfd_coff_swap_aux_out (abfd, &auxent, syment.n_type, syment.n_sclass, 0, 
			 syment.n_numaux, 
			 &syment_ext[(filehdr.f_nsyms + 1) * SYMESZ]);
  filehdr.f_nsyms += 2;

  /* __rtinit */
  memset (&syment, 0, sizeof (struct internal_syment));
  memset (&auxent, 0, sizeof (union internal_auxent));
  memcpy (syment._n._n_name, rtinit_name, strlen (rtinit_name));
  syment.n_scnum = 1;
  syment.n_sclass = C_EXT;
  syment.n_numaux = 1;
  auxent.x_csect.x_smtyp = XTY_LD;
  auxent.x_csect.x_smclas = XMC_RW;
  bfd_coff_swap_sym_out (abfd, &syment, 
			 &syment_ext[filehdr.f_nsyms * SYMESZ]);
  bfd_coff_swap_aux_out (abfd, &auxent, syment.n_type, syment.n_sclass, 0, 
			 syment.n_numaux, 
			 &syment_ext[(filehdr.f_nsyms + 1) * SYMESZ]);
  filehdr.f_nsyms += 2;

  /* init */
  if (initsz) 
    {
      memset (&syment, 0, sizeof (struct internal_syment));
      memset (&auxent, 0, sizeof (union internal_auxent));

      if (initsz > 9) 
	{
	  syment._n._n_n._n_offset = st_tmp - string_table;
	  memcpy (st_tmp, init, initsz);
	  st_tmp += initsz;
	}
      else
	memcpy (syment._n._n_name, init, initsz - 1);

      syment.n_sclass = C_EXT;
      syment.n_numaux = 1;
      bfd_coff_swap_sym_out (abfd, &syment, 
			     &syment_ext[filehdr.f_nsyms * SYMESZ]);
      bfd_coff_swap_aux_out (abfd, &auxent, syment.n_type, syment.n_sclass, 0, 
			     syment.n_numaux, 
			     &syment_ext[(filehdr.f_nsyms + 1) * SYMESZ]);

      /* reloc */
      memset (&reloc, 0, sizeof (struct internal_reloc));
      reloc.r_vaddr = 0x0010;
      reloc.r_symndx = filehdr.f_nsyms;
      reloc.r_type = R_POS;
      reloc.r_size = 31;
      bfd_coff_swap_reloc_out (abfd, &reloc, &reloc_ext[0]);

      filehdr.f_nsyms += 2;
      scnhdr.s_nreloc += 1;
    }
  
  /* fini */
  if (finisz) 
    {
      memset (&syment, 0, sizeof (struct internal_syment));
      memset (&auxent, 0, sizeof (union internal_auxent));

      if (finisz > 9) 
	{
	  syment._n._n_n._n_offset = st_tmp - string_table;
	  memcpy (st_tmp, fini, finisz);
	  st_tmp += finisz;
	}
      else
	memcpy (syment._n._n_name, fini, finisz - 1);

      syment.n_sclass = C_EXT;
      syment.n_numaux = 1;
      bfd_coff_swap_sym_out (abfd, &syment, 
			     &syment_ext[filehdr.f_nsyms * SYMESZ]);
      bfd_coff_swap_aux_out (abfd, &auxent, syment.n_type, syment.n_sclass, 0, 
			     syment.n_numaux, 
			     &syment_ext[(filehdr.f_nsyms + 1) * SYMESZ]);

      /* reloc */
      memset (&reloc, 0, sizeof (struct internal_reloc));
      reloc.r_vaddr = 0x0028;
      reloc.r_symndx = filehdr.f_nsyms;
      reloc.r_type = R_POS;
      reloc.r_size = 31;
      bfd_coff_swap_reloc_out (abfd, &reloc, 
			       &reloc_ext[scnhdr.s_nreloc * RELSZ]);

      filehdr.f_nsyms += 2;
      scnhdr.s_nreloc += 1;
    }

  scnhdr.s_relptr = scnhdr.s_scnptr + data_buffer_size;
  filehdr.f_symptr = scnhdr.s_relptr + scnhdr.s_nreloc * RELSZ;

  bfd_coff_swap_filehdr_out (abfd, &filehdr, filehdr_ext);
  bfd_bwrite (filehdr_ext, FILHSZ, abfd);
  bfd_coff_swap_scnhdr_out (abfd, &scnhdr, scnhdr_ext);
  bfd_bwrite (scnhdr_ext, SCNHSZ, abfd);
  bfd_bwrite (data_buffer, data_buffer_size, abfd);
  bfd_bwrite (reloc_ext, scnhdr.s_nreloc * RELSZ, abfd);
  bfd_bwrite (syment_ext, filehdr.f_nsyms * SYMESZ, abfd);
  bfd_bwrite (string_table, string_table_size, abfd);

  free (data_buffer);
  data_buffer = NULL;

  return true;
}


static reloc_howto_type xcoff_dynamic_reloc =
HOWTO (0,	                /* type */
       0,	                /* rightshift */
       2,	                /* size (0 = byte, 1 = short, 2 = long) */
       32,	                /* bitsize */
       false,	                /* pc_relative */
       0,	                /* bitpos */
       complain_overflow_bitfield, /* complain_on_overflow */
       0,		        /* special_function */
       "R_POS",               /* name */
       true,	                /* partial_inplace */
       0xffffffff,            /* src_mask */
       0xffffffff,            /* dst_mask */
       false);                /* pcrel_offset */

/*  glink

   The first word of global linkage code must be modified by filling in
   the correct TOC offset.  */

static unsigned long xcoff_glink_code[9] =
  {
    0x81820000,	/* lwz r12,0(r2) */
    0x90410014,	/* stw r2,20(r1) */
    0x800c0000,	/* lwz r0,0(r12) */
    0x804c0004,	/* lwz r2,4(r12) */
    0x7c0903a6,	/* mtctr r0 */
    0x4e800420,	/* bctr */
    0x00000000,	/* start of traceback table */
    0x000c8000,	/* traceback table */
    0x00000000,	/* traceback table */
  };


static const struct xcoff_backend_data_rec bfd_xcoff_backend_data =
  {
    { /* COFF backend, defined in libcoff.h.  */
      _bfd_xcoff_swap_aux_in,           /* _bfd_coff_swap_aux_in */
      _bfd_xcoff_swap_sym_in,           /* _bfd_coff_swap_sym_in */
      coff_swap_lineno_in,              /* _bfd_coff_swap_lineno_in */
      _bfd_xcoff_swap_aux_out,          /* _bfd_swap_aux_out */
      _bfd_xcoff_swap_sym_out,          /* _bfd_swap_sym_out */
      coff_swap_lineno_out,             /* _bfd_swap_lineno_out */
      coff_swap_reloc_out,              /* _bfd_swap_reloc_out */
      coff_swap_filehdr_out,            /* _bfd_swap_filehdr_out */
      coff_swap_aouthdr_out,            /* _bfd_swap_aouthdr_out */
      coff_swap_scnhdr_out,             /* _bfd_swap_scnhdr_out */
      FILHSZ,                           /* _bfd_filhsz */
      AOUTSZ,                           /* _bfd_aoutsz */
      SCNHSZ,                           /* _bfd_scnhsz */
      SYMESZ,                           /* _bfd_symesz */
      AUXESZ,                           /* _bfd_auxesz */
      RELSZ,                            /* _bfd_relsz */
      LINESZ,                           /* _bfd_linesz */
      FILNMLEN,                         /* _bfd_filnmlen */
      true,                             /* _bfd_coff_long_filenames */
      false,                            /* _bfd_coff_long_section_names */
      (3),                              /* _bfd_coff_default_section_alignment_power */
      false,                            /* _bfd_coff_force_symnames_in_strings */
      2,                                /* _bfd_coff_debug_string_prefix_length */
      coff_swap_filehdr_in,             /* _bfd_coff_swap_filehdr_in */
      coff_swap_aouthdr_in,             /* _bfd_swap_aouthdr_in */
      coff_swap_scnhdr_in,              /* _bfd_swap_scnhdr_in */
      coff_swap_reloc_in,               /* _bfd_reloc_in */
      coff_bad_format_hook,             /* _bfd_bad_format_hook */
      coff_set_arch_mach_hook,          /* _bfd_set_arch_mach_hook */
      coff_mkobject_hook,               /* _bfd_mkobject_hook */
      styp_to_sec_flags,                /* _bfd_syp_to_sec_flags */
      coff_set_alignment_hook,          /* _bfd_set_alignment_hook */
      coff_slurp_symbol_table,          /* _bfd_coff_slurp_symbol_table */
      symname_in_debug_hook,            /* _coff_symname_in_debug_hook */
      coff_pointerize_aux_hook,         /* _bfd_coff_pointerize_aux_hook */
      coff_print_aux,                   /* bfd_coff_print_aux */
      dummy_reloc16_extra_cases,        /* _bfd_coff_reloc16_extra_cases */
      dummy_reloc16_estimate,           /* _bfd_coff_reloc16_estimate */
      NULL,                             /* bfd_coff_sym_is_global */
      coff_compute_section_file_positions, /* _bfd_coff_compute_section_file_positions */
      NULL,                             /* _bfd_coff_start_final_link */
      xcoff_ppc_relocate_section,       /* _bfd_coff_relocate_section */
      coff_rtype_to_howto,              /* _bfd_coff_rtype_to_howto */
      NULL,                             /* _bfd_coff_addust_symndx */
      _bfd_generic_link_add_one_symbol, /* _bfd_coff_add_one_symbol */
      coff_link_output_has_begun,       /* _bfd_coff_link_output_has_begun */
      coff_final_link_postscript        /* _bfd_coff_final_link_postscript */
    },

    0x01DF,                             /* magic number */
    bfd_arch_rs6000,                    /* architecture */
    bfd_mach_rs6k,                      /* machine */

    /* Function pointers to xcoff specific swap routines.  */
    xcoff_swap_ldhdr_in,                /* _xcoff_swap_ldhdr_in */
    xcoff_swap_ldhdr_out,               /* _xcoff_swap_ldhdr_out */
    xcoff_swap_ldsym_in,                /* _xcoff_swap_ldsym_in */
    xcoff_swap_ldsym_out,               /* _xcoff_swap_ldsym_out */
    xcoff_swap_ldrel_in,                /* _xcoff_swap_ldrel_in */
    xcoff_swap_ldrel_out,               /* _xcoff_swap_ldrel_out */

    /* Sizes.  */
    LDHDRSZ,                            /* _xcoff_ldhdrsz */
    LDSYMSZ,                            /* _xcoff_ldsymsz */
    LDRELSZ,                            /* _xcoff_ldrelsz */
    12,                                 /* _xcoff_function_descriptor_size */
    SMALL_AOUTSZ,                       /* _xcoff_small_aout_header_size */

  /* Versions. */
    1,                                    /* _xcoff_ldhdr_version */

    /* Xcoff vs xcoff64 putting symbol names.  */
    _bfd_xcoff_put_symbol_name,          /* _xcoff_put_symbol_name */
    _bfd_xcoff_put_ldsymbol_name,          /* _xcoff_put_ldsymbol_name */

    & xcoff_dynamic_reloc,                  /* dynamic reloc howto */

    xcoff_create_csect_from_smclas,      /* _xcoff_create_csect_from_smclas */

    /* Lineno and reloc count overflow.  */
    xcoff_is_lineno_count_overflow,
    xcoff_is_reloc_count_overflow,

    xcoff_loader_symbol_offset,
    xcoff_loader_reloc_offset,

    /* glink.  */
    & xcoff_glink_code[0],
    (36),           /* _xcoff_glink_size */

    /* rtinit */
    64,           /* _xcoff_rtinit_size */
    xcoff_generate_rtinit,  /* _xcoff_generate_rtinit */
};

/* The transfer vector that leads the outside world to all of the above. */
const bfd_target rs6000coff_vec =
{
  "aixcoff-rs6000",
  bfd_target_xcoff_flavour,
  BFD_ENDIAN_BIG,		/* data byte order is big */
  BFD_ENDIAN_BIG,		/* header byte order is big */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG | DYNAMIC |
   HAS_SYMS | HAS_LOCALS | WP_TEXT),

  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
  0,				/* leading char */
  '/',				/* ar_pad_char */
  15,				/* ar_max_namelen??? FIXMEmgo */

                      /* data */
  bfd_getb64,         /* bfd_getx64 */
  bfd_getb_signed_64, /* bfd_getx_signed_64 */
  bfd_putb64,         /* bfd_putx64 */
  bfd_getb32,         /* bfd_getx32 */
  bfd_getb_signed_32, /* bfd_getx_signed_32 */
  bfd_putb32,         /* bfd_putx32 */
  bfd_getb16,         /* bfd_getx16 */
  bfd_getb_signed_16, /* bfd_getx_signed_16 */
  bfd_putb16,         /* bfd_putx16 */

                      /* hdrs */
  bfd_getb64,         /* bfd_h_getx64 */
  bfd_getb_signed_64, /* bfd_h_getx_signed_64 */
  bfd_putb64,         /* bfd_h_putx64 */
  bfd_getb32,         /* bfd_h_getx32 */
  bfd_getb_signed_32, /* bfd_h_getx_signed_32 */
  bfd_putb32,         /* bfd_h_putx32 */
  bfd_getb16,         /* bfd_h_getx16 */
  bfd_getb_signed_16, /* bfd_h_getx_signed_16 */
  bfd_putb16,         /* bfd_h_putx16 */

  { /* bfd_check_format */
    _bfd_dummy_target,
    coff_object_p,
    _bfd_xcoff_archive_p,
    CORE_FILE_P
  },

  { /* bfd_set_format */
    bfd_false,
    coff_mkobject,
    _bfd_generic_mkarchive,
    bfd_false
  },

  {/* bfd_write_contents */
    bfd_false,
    coff_write_object_contents,
    _bfd_xcoff_write_archive_contents,
    bfd_false
  },

  /* Generic */
  bfd_true,                          /* _close_and_cleanup */
  bfd_true,                          /* _bfd_free_cached_info */
  coff_new_section_hook,             /* _new_section_hook */
  _bfd_generic_get_section_contents, /* _bfd_get_section_contents */
                                     /* _bfd_get_section_contents_in_window */
  _bfd_generic_get_section_contents_in_window,

  /* Copy */
  _bfd_xcoff_copy_private_bfd_data, /* _bfd_copy_private_bfd */
                                    /* _bfd_merge_private_bfd_data */
  ((boolean (*) (bfd *, bfd *)) bfd_true),
                                    /* _bfd_copy_pivate_section_data */
  ((boolean (*) (bfd *, asection *, bfd *, asection *)) bfd_true),
                                    /* _bfd_copy_private_symbol_data */
  ((boolean (*) (bfd *, asymbol *, bfd *, asymbol *)) bfd_true),
  ((boolean (*) (bfd *, flagword)) bfd_true), /* _bfd_set_private_flags */
  ((boolean (*) (bfd *, void * )) bfd_true),  /* _bfd_print_private_bfd_data */

  /* Core */
  coff_core_file_failing_command,    /* _core_file_failing_command */
  coff_core_file_failing_signal,     /* _core_file_failing_signal */
                                          /* _core_file_matches_executable_p */
  coff_core_file_matches_executable_p,

  /* Archive */
  _bfd_xcoff_slurp_armap,                  /* _slurp_armap */
                                           /* XCOFF archives do not have
					      anything which corresponds to
					      an extended name table.  */
  bfd_false,                               /* _slurp_extended_name_table */
                                           /* _construct_extended_name_table */
  ((boolean (*) (bfd *, char **, bfd_size_type *, const char **)) bfd_false),
  bfd_dont_truncate_arname,                /* _truncate_arname */
  _bfd_xcoff_write_armap,                  /* _write_armap */
  _bfd_xcoff_read_ar_hdr,                  /* _read_ar_hdr */
  _bfd_xcoff_openr_next_archived_file,     /* _openr_next_archived_file */
  _bfd_generic_get_elt_at_index,           /* _get_elt_at_index */
  _bfd_xcoff_stat_arch_elt,                /* _generic_stat_arch_elt */
                                           /* XCOFF archives do not have
					      a timestamp.  */
  bfd_true,                                /* _update_armap_timestamp */

  /* Symbols */
  coff_get_symtab_upper_bound,             /* _get_symtab_upper_bound */
  coff_get_symtab,                         /* _get_symtab */
  coff_make_empty_symbol,                  /* _make_empty_symbol */
  coff_print_symbol,                       /* _print_symbol */
  coff_get_symbol_info,                    /* _get_symbol_info */
  _bfd_xcoff_is_local_label_name,          /* _bfd_is_local_label_name */
  coff_get_lineno,                         /* _get_lineno */
  coff_find_nearest_line,                  /* _find_nearest_line */
  coff_bfd_make_debug_symbol,              /* _bfd_make_debug_symbol */
  _bfd_generic_read_minisymbols,           /* _read_minisymbols */
  _bfd_generic_minisymbol_to_symbol,       /* _minsymbol_to_symbol */

  /* Reloc */
  coff_get_reloc_upper_bound,              /* _get_reloc_upper_bound */
  coff_canonicalize_reloc,                 /* _cononicalize_reloc */
  _bfd_xcoff_reloc_type_lookup,            /* _bfd_reloc_type_lookup */

  /* Write */
  coff_set_arch_mach,                      /* _set_arch_mach */
  coff_set_section_contents,               /* _set_section_contents */

  /* Link */
  _bfd_xcoff_sizeof_headers,               /* _sizeof_headers */
                                      /* _bfd_get_relocated_section_contents */
  bfd_generic_get_relocated_section_contents,
  bfd_generic_relax_section,               /* _bfd_relax_section */
  _bfd_xcoff_bfd_link_hash_table_create,   /* _bfd_link_hash_table_create */
  _bfd_xcoff_bfd_link_add_symbols,         /* _bfd_link_add_symbols */
  _bfd_xcoff_bfd_final_link,               /* _bfd_filnal_link */
  _bfd_generic_link_split_section,         /* _bfd_link_split_section */
  bfd_generic_gc_sections,                 /* _bfd_gc_sections */
  bfd_generic_merge_sections,              /* _bfd_merge_sections */

  /* Dynamic */
                                          /* _get_dynamic_symtab_upper_bound */
  _bfd_xcoff_get_dynamic_symtab_upper_bound,
  _bfd_xcoff_canonicalize_dynamic_symtab,  /* _cononicalize_dynamic_symtab */
  _bfd_xcoff_get_dynamic_reloc_upper_bound,/* _get_dynamic_reloc_upper_bound */
  _bfd_xcoff_canonicalize_dynamic_reloc,   /* _cononicalize_dynamic_reloc */

  /* Opposite endian version, none exists */
  NULL,

  /* back end data */
  (void *) &bfd_xcoff_backend_data,
};

/*
 * xcoff-powermac target
 * Old target.
 * Only difference between this target and the rs6000 target is the
 * the default architecture and machine type used in coffcode.h
 *
 * PowerPC Macs use the same magic numbers as RS/6000
 * (because that's how they were bootstrapped originally),
 * but they are always PowerPC architecture.
 */
static const struct xcoff_backend_data_rec bfd_pmac_xcoff_backend_data =
{
  { /* COFF backend, defined in libcoff.h */
    _bfd_xcoff_swap_aux_in,           /* _bfd_coff_swap_aux_in */
    _bfd_xcoff_swap_sym_in,           /* _bfd_coff_swap_sym_in */
    coff_swap_lineno_in,              /* _bfd_coff_swap_lineno_in */
    _bfd_xcoff_swap_aux_out,          /* _bfd_swap_aux_out */
    _bfd_xcoff_swap_sym_out,          /* _bfd_swap_sym_out */
    coff_swap_lineno_out,             /* _bfd_swap_lineno_out */
    coff_swap_reloc_out,              /* _bfd_swap_reloc_out */
    coff_swap_filehdr_out,            /* _bfd_swap_filehdr_out */
    coff_swap_aouthdr_out,            /* _bfd_swap_aouthdr_out */
    coff_swap_scnhdr_out,             /* _bfd_swap_scnhdr_out */
    FILHSZ,                           /* _bfd_filhsz */
    AOUTSZ,                           /* _bfd_aoutsz */
    SCNHSZ,                           /* _bfd_scnhsz */
    SYMESZ,                           /* _bfd_symesz */
    AUXESZ,                           /* _bfd_auxesz */
    RELSZ,                            /* _bfd_relsz */
    LINESZ,                           /* _bfd_linesz */
    FILNMLEN,                         /* _bfd_filnmlen */
    true,                             /* _bfd_coff_long_filenames */
    false,                            /* _bfd_coff_long_section_names */
    (3),                        /* _bfd_coff_default_section_alignment_power */
    false,                            /* _bfd_coff_force_symnames_in_strings */
    2,                               /* _bfd_coff_debug_string_prefix_length */
    coff_swap_filehdr_in,             /* _bfd_coff_swap_filehdr_in */
    coff_swap_aouthdr_in,             /* _bfd_swap_aouthdr_in */
    coff_swap_scnhdr_in,              /* _bfd_swap_scnhdr_in */
    coff_swap_reloc_in,               /* _bfd_reloc_in */
    coff_bad_format_hook,             /* _bfd_bad_format_hook */
    coff_set_arch_mach_hook,          /* _bfd_set_arch_mach_hook */
    coff_mkobject_hook,               /* _bfd_mkobject_hook */
    styp_to_sec_flags,                /* _bfd_syp_to_sec_flags */
    coff_set_alignment_hook,          /* _bfd_set_alignment_hook */
    coff_slurp_symbol_table,          /* _bfd_coff_slurp_symbol_table */
    symname_in_debug_hook,            /* _coff_symname_in_debug_hook */
    coff_pointerize_aux_hook,         /* _bfd_coff_pointerize_aux_hook */
    coff_print_aux,                   /* bfd_coff_print_aux */
    dummy_reloc16_extra_cases,        /* _bfd_coff_reloc16_extra_cases */
    dummy_reloc16_estimate,           /* _bfd_coff_reloc16_estimate */
    NULL,                             /* bfd_coff_sym_is_global */
                                 /* _bfd_coff_compute_section_file_positions */
    coff_compute_section_file_positions,
    NULL,                             /* _bfd_coff_start_final_link */
    xcoff_ppc_relocate_section,       /* _bfd_coff_relocate_section */
    coff_rtype_to_howto,              /* _bfd_coff_rtype_to_howto */
    NULL,                             /* _bfd_coff_addust_symndx */
    _bfd_generic_link_add_one_symbol, /* _bfd_coff_add_one_symbol */
    coff_link_output_has_begun,       /* _bfd_coff_link_output_has_begun */
    coff_final_link_postscript        /* _bfd_coff_final_link_postscript */
  },

  0x01DF,                             /* magic number */
  bfd_arch_powerpc,                   /* architecture */
  bfd_mach_ppc,                       /* machine */

  /* function pointers to xcoff specific swap routines */
  xcoff_swap_ldhdr_in,                /* _xcoff_swap_ldhdr_in */
  xcoff_swap_ldhdr_out,               /* _xcoff_swap_ldhdr_out */
  xcoff_swap_ldsym_in,                /* _xcoff_swap_ldsym_in */
  xcoff_swap_ldsym_out,               /* _xcoff_swap_ldsym_out */
  xcoff_swap_ldrel_in,                /* _xcoff_swap_ldrel_in */
  xcoff_swap_ldrel_out,               /* _xcoff_swap_ldrel_out */

  /* sizes */
  LDHDRSZ,                            /* _xcoff_ldhdrsz */
  LDSYMSZ,                            /* _xcoff_ldsymsz */
  LDRELSZ,                            /* _xcoff_ldrelsz */
  12,                                 /* _xcoff_function_descriptor_size */
  SMALL_AOUTSZ,                       /* _xcoff_small_aout_header_size */

  /* versions */
  1,                                    /* _xcoff_ldhdr_version */

  /* xcoff vs xcoff64 putting symbol names */
  _bfd_xcoff_put_symbol_name,          /* _xcoff_put_symbol_name */
  _bfd_xcoff_put_ldsymbol_name,          /* _xcoff_put_ldsymbol_name */

  &xcoff_dynamic_reloc,                  /* dynamic reloc howto */

  xcoff_create_csect_from_smclas,      /* _xcoff_create_csect_from_smclas */

  /* lineno and reloc count overflow */
  xcoff_is_lineno_count_overflow,
  xcoff_is_reloc_count_overflow,

  xcoff_loader_symbol_offset,
  xcoff_loader_reloc_offset,

  /* glink */
  &xcoff_glink_code[0],
  (36),           /* _xcoff_glink_size */

  /* rtinit */
  0,           /* _xcoff_rtinit_size */
  xcoff_generate_rtinit,  /* _xcoff_generate_rtinit */

};

/* The transfer vector that leads the outside world to all of the above. */
const bfd_target pmac_xcoff_vec =
{
  "xcoff-powermac",
  bfd_target_xcoff_flavour,
  BFD_ENDIAN_BIG,		/* data byte order is big */
  BFD_ENDIAN_BIG,		/* header byte order is big */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG | DYNAMIC |
   HAS_SYMS | HAS_LOCALS | WP_TEXT),

  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
  0,				/* leading char */
  '/',				/* ar_pad_char */
  15,				/* ar_max_namelen??? FIXMEmgo */

                      /* data */
  bfd_getb64,         /* bfd_getx64 */
  bfd_getb_signed_64, /* bfd_getx_signed_64 */
  bfd_putb64,         /* bfd_putx64 */
  bfd_getb32,         /* bfd_getx32 */
  bfd_getb_signed_32, /* bfd_getx_signed_32 */
  bfd_putb32,         /* bfd_putx32 */
  bfd_getb16,         /* bfd_getx16 */
  bfd_getb_signed_16, /* bfd_getx_signed_16 */
  bfd_putb16,         /* bfd_putx16 */

                      /* hdrs */
  bfd_getb64,         /* bfd_h_getx64 */
  bfd_getb_signed_64, /* bfd_h_getx_signed_64 */
  bfd_putb64,         /* bfd_h_putx64 */
  bfd_getb32,         /* bfd_h_getx32 */
  bfd_getb_signed_32, /* bfd_h_getx_signed_32 */
  bfd_putb32,         /* bfd_h_putx32 */
  bfd_getb16,         /* bfd_h_getx16 */
  bfd_getb_signed_16, /* bfd_h_getx_signed_16 */
  bfd_putb16,         /* bfd_h_putx16 */

  { /* bfd_check_format */
    _bfd_dummy_target,
    coff_object_p,
    _bfd_xcoff_archive_p,
    CORE_FILE_P
  },

  { /* bfd_set_format */
    bfd_false,
    coff_mkobject,
    _bfd_generic_mkarchive,
    bfd_false
  },

  {/* bfd_write_contents */
    bfd_false,
    coff_write_object_contents,
    _bfd_xcoff_write_archive_contents,
    bfd_false
  },

  /* Generic */
  bfd_true,                          /* _close_and_cleanup */
  bfd_true,                          /* _bfd_free_cached_info */
  coff_new_section_hook,             /* _new_section_hook */
  _bfd_generic_get_section_contents, /* _bfd_get_section_contents */
                                     /* _bfd_get_section_contents_in_window */
  _bfd_generic_get_section_contents_in_window,

  /* Copy */
  _bfd_xcoff_copy_private_bfd_data, /* _bfd_copy_private_bfd */
                                    /* _bfd_merge_private_bfd_data */
  ((boolean (*) (bfd *, bfd *)) bfd_true),
                                    /* _bfd_copy_pivate_section_data */
  ((boolean (*) (bfd *, asection *, bfd *, asection *)) bfd_true),
                                    /* _bfd_copy_private_symbol_data */
  ((boolean (*) (bfd *, asymbol *, bfd *, asymbol *)) bfd_true),
  ((boolean (*) (bfd *, flagword)) bfd_true), /* _bfd_set_private_flags */
  ((boolean (*) (bfd *, void * )) bfd_true),  /* _bfd_print_private_bfd_data */

  /* Core */
  coff_core_file_failing_command,    /* _core_file_failing_command */
  coff_core_file_failing_signal,     /* _core_file_failing_signal */
                                          /* _core_file_matches_executable_p */
  coff_core_file_matches_executable_p,

  /* Archive */
  _bfd_xcoff_slurp_armap,                  /* _slurp_armap */
                                           /* XCOFF archives do not have
					      anything which corresponds to
					      an extended name table.  */
  bfd_false,                               /* _slurp_extended_name_table */
                                           /* _construct_extended_name_table */
  ((boolean (*) (bfd *, char **, bfd_size_type *, const char **)) bfd_false),
  bfd_dont_truncate_arname,                /* _truncate_arname */
  _bfd_xcoff_write_armap,                  /* _write_armap */
  _bfd_xcoff_read_ar_hdr,                  /* _read_ar_hdr */
  _bfd_xcoff_openr_next_archived_file,     /* _openr_next_archived_file */
  _bfd_generic_get_elt_at_index,           /* _get_elt_at_index */
  _bfd_xcoff_stat_arch_elt,                /* _generic_stat_arch_elt */
                                           /* XCOFF archives do not have
					      a timestamp.  */
  bfd_true,                                /* _update_armap_timestamp */

  /* Symbols */
  coff_get_symtab_upper_bound,             /* _get_symtab_upper_bound */
  coff_get_symtab,                         /* _get_symtab */
  coff_make_empty_symbol,                  /* _make_empty_symbol */
  coff_print_symbol,                       /* _print_symbol */
  coff_get_symbol_info,                    /* _get_symbol_info */
  _bfd_xcoff_is_local_label_name,          /* _bfd_is_local_label_name */
  coff_get_lineno,                         /* _get_lineno */
  coff_find_nearest_line,                  /* _find_nearest_line */
  coff_bfd_make_debug_symbol,              /* _bfd_make_debug_symbol */
  _bfd_generic_read_minisymbols,           /* _read_minisymbols */
  _bfd_generic_minisymbol_to_symbol,       /* _minsymbol_to_symbol */

  /* Reloc */
  coff_get_reloc_upper_bound,              /* _get_reloc_upper_bound */
  coff_canonicalize_reloc,                 /* _cononicalize_reloc */
  _bfd_xcoff_reloc_type_lookup,            /* _bfd_reloc_type_lookup */

  /* Write */
  coff_set_arch_mach,                      /* _set_arch_mach */
  coff_set_section_contents,               /* _set_section_contents */

  /* Link */
  _bfd_xcoff_sizeof_headers,               /* _sizeof_headers */
                                      /* _bfd_get_relocated_section_contents */
  bfd_generic_get_relocated_section_contents,
  bfd_generic_relax_section,               /* _bfd_relax_section */
  _bfd_xcoff_bfd_link_hash_table_create,   /* _bfd_link_hash_table_create */
  _bfd_xcoff_bfd_link_add_symbols,         /* _bfd_link_add_symbols */
  _bfd_xcoff_bfd_final_link,               /* _bfd_filnal_link */
  _bfd_generic_link_split_section,         /* _bfd_link_split_section */
  bfd_generic_gc_sections,                 /* _bfd_gc_sections */
  bfd_generic_merge_sections,               /* _bfd_merge_sections */

  /* Dynamic */
                                          /* _get_dynamic_symtab_upper_bound */
  _bfd_xcoff_get_dynamic_symtab_upper_bound,
  _bfd_xcoff_canonicalize_dynamic_symtab,  /* _cononicalize_dynamic_symtab */
  _bfd_xcoff_get_dynamic_reloc_upper_bound,/* _get_dynamic_reloc_upper_bound */
  _bfd_xcoff_canonicalize_dynamic_reloc,   /* _cononicalize_dynamic_reloc */

  /* Opposite endian version, none exists */
  NULL,

  /* back end data */
  (void *) &bfd_pmac_xcoff_backend_data,
};
