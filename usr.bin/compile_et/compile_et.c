/*
 *
 * Copyright 1986, 1987, 1988
 * by MIT Student Information Processing Board.
 *
 * For copyright info, see "mit-sipb-copyright.h".
 *
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/param.h>
#include "mit-sipb-copyright.h"
#include "compiler.h"

#ifndef __STDC__
#define const
#endif

#ifndef lint
static const char copyright[] =
    "Copyright 1987,1988 by MIT Student Information Processing Board";

static const char rcsid_compile_et_c[] =
    "$Header: /home/ncvs/src/usr.bin/compile_et/compile_et.c,v 1.5 1998/12/15 12:20:27 des Exp $";
#endif

extern char *gensym();
extern char *current_token;
extern int table_number, current;
char buffer[BUFSIZ];
char *table_name = (char *)NULL;
FILE *hfile, *cfile;

/* lex stuff */
extern FILE *yyin;
extern int yylineno;

char * xmalloc (size) unsigned int size; {
    char * p = malloc (size);
    if (!p)
		err(1, NULL);
    return p;
}

static int check_arg (str_list, arg) char const *const *str_list, *arg; {
    while (*str_list)
	if (!strcmp(arg, *str_list++))
	    return 1;
    return 0;
}

static const char *const debug_args[] = {
    "d",
    "debug",
    0,
};

static const char *const lang_args[] = {
    "lang",
    "language",
    0,
};

static const char *const language_names[] = {
    "C",
    "K&R C",
    "C++",
    0,
};

static const char * const noargs_def[] = {
    "#ifdef __STDC__\n",
    "#define NOARGS void\n",
    "#else\n",
    "#define NOARGS\n",
    "#define const\n",
    "#endif\n\n",
    0,
};

static const char *const struct_def[] = {
    "struct error_table {\n",
    "    char const * const * msgs;\n",
    "    long base;\n",
    "    int n_msgs;\n",
    "};\n",
    "struct et_list {\n",
    "    struct et_list *next;\n",
    "    const struct error_table * table;\n",
    "};\n",
    "extern struct et_list *_et_list;\n",
    "\n", 0,
};

static const char warning[] =
    "/*\n * %s:\n * This file is automatically generated; please do not edit it.\n */\n";

/* pathnames */
char c_file[MAXPATHLEN];	/* output file */
char h_file[MAXPATHLEN];	/* output */

static void usage () {
    fprintf (stderr, "usage: compile_et ERROR_TABLE\n");
    exit (1);
}

static void dup_err (type, one, two) char const *type, *one, *two; {
    warnx("multiple %s specified: `%s' and `%s'", type, one, two);
    usage ();
}

int main (argc, argv) int argc; char **argv; {
    char *p, *ename;
    int len;
    char const * const *cpp;
    int got_language = 0;

    /* argument parsing */
    debug = 0;
    filename = 0;
    while (argv++, --argc) {
	char *arg = *argv;
	if (arg[0] != '-') {
	    if (filename)
		dup_err ("filenames", filename, arg);
	    filename = arg;
	}
	else {
	    arg++;
	    if (check_arg (debug_args, arg))
		debug++;
	    else if (check_arg (lang_args, arg)) {
		got_language++;
		arg = *++argv, argc--;
		if (!arg)
		    usage ();
		if (language)
		    dup_err ("languages", language_names[(int)language], arg);
#define check_lang(x,v) else if (!strcasecmp(arg,x)) language = v
		check_lang ("c", lang_C);
		check_lang ("ansi_c", lang_C);
		check_lang ("ansi-c", lang_C);
		check_lang ("krc", lang_KRC);
		check_lang ("kr_c", lang_KRC);
		check_lang ("kr-c", lang_KRC);
		check_lang ("k&r-c", lang_KRC);
		check_lang ("k&r_c", lang_KRC);
		check_lang ("c++", lang_CPP);
		check_lang ("cplusplus", lang_CPP);
		check_lang ("c-plus-plus", lang_CPP);
#undef check_lang
		else {
		    errx(1, "unknown language name `%s'\n\tpick one of: C K&R-C", arg);
		}
	    }
	    else {
			warnx("unknown control argument -`%s'", arg);
			usage ();
	    }
	}
    }
    if (!filename)
	usage ();
    if (!got_language)
	language = lang_KRC;
    else if (language == lang_CPP) {
		errx(1, "sorry, C++ support is not yet finished");
    }

    p = xmalloc (strlen (filename) + 5);
    strcpy (p, filename);
    filename = p;
    p = strrchr(filename, '/');
    if (p == (char *)NULL)
	p = filename;
    else
	p++;
    ename = p;
    len = strlen (ename);
    p += len - 3;
    if (strcmp (p, ".et"))
	p += 3;
    *p++ = '.';
    /* now p points to where "et" suffix should start */
    /* generate new filenames */
    strcpy (p, "c");
    strcpy (c_file, ename);
    *p = 'h';
    strcpy (h_file, ename);
    strcpy (p, "et");

    yyin = fopen(filename, "r");
    if (!yyin) {
	perror(filename);
	exit(1);
    }

    hfile = fopen(h_file, "w");
    if (hfile == (FILE *)NULL) {
	perror(h_file);
	exit(1);
    }
    fprintf (hfile, warning, h_file);

    cfile = fopen(c_file, "w");
    if (cfile == (FILE *)NULL) {
	perror(c_file);
	exit(1);
    }
    fprintf (cfile, warning, c_file);

    /* prologue */
    for (cpp = noargs_def; *cpp; cpp++) {
	fputs (*cpp, cfile);
	fputs (*cpp, hfile);
    }

    fputs("static const char * const text[] = {\n", cfile);

    /* parse it */
    yyparse();
    fclose(yyin);		/* bye bye input file */

    fputs ("    0\n};\n\n", cfile);
    for (cpp = struct_def; *cpp; cpp++)
	fputs (*cpp, cfile);
    fprintf(cfile,
	    "static const struct error_table et = { text, %dL, %d };\n\n",
	    table_number, current);
    fputs("static struct et_list link = { 0, 0 };\n\n",
	  cfile);
    fprintf(cfile, "void initialize_%s_error_table (NOARGS) {\n",
	    table_name);
    fputs("    if (!link.table) {\n", cfile);
    fputs("        link.next = _et_list;\n", cfile);
    fputs("        link.table = &et;\n", cfile);
    fputs("        _et_list = &link;\n", cfile);
    fputs("    }\n", cfile);
    fputs("}\n", cfile);
    fclose(cfile);

    fprintf (hfile, "extern void initialize_%s_error_table (NOARGS);\n",
	     table_name);
    fprintf (hfile, "#define ERROR_TABLE_BASE_%s (%dL)\n",
	     table_name, table_number);
    /* compatibility... */
    fprintf (hfile, "\n/* for compatibility with older versions... */\n");
    fprintf (hfile, "#define init_%s_err_tbl initialize_%s_error_table\n",
	     table_name, table_name);
    fprintf (hfile, "#define %s_err_base ERROR_TABLE_BASE_%s\n", table_name,
	     table_name);
    fclose(hfile);		/* bye bye include file */

    return 0;
}

int yyerror(s) char *s; {
    fputs(s, stderr);
    fprintf(stderr, "\nLine number %d; last token was '%s'\n",
	    yylineno, current_token);
    return 0;
}

