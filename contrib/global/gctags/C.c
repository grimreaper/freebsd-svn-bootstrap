/*
 * Copyright (c) 1987, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)C.c	8.4 (Berkeley) 4/2/94";
#endif /* LIBC_SCCS and not lint */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ctags.h"

static int	func_entry __P((void));
static void	hash_entry __P((void));
static void	skip_string __P((int));
static int	str_entry __P((int));
#ifdef GLOBAL
static int	cmp __P((const void *, const void *));
static int	isstatement __P((char *));
static void	define_line __P((void));
#endif

#ifdef YACC
extern int	yaccfile;		/* true when *.y file */
#endif
/*
 * c_entries --
 *	read .c and .h files and call appropriate routines
 */
void
c_entries()
{
	int	c;			/* current character */
	int	level;			/* brace level */
	int	token;			/* if reading a token */
	int	t_def;			/* if reading a typedef */
	int	t_level;		/* typedef's brace level */
	char	*sp;			/* buffer pointer */
	char	tok[MAXTOKEN];		/* token buffer */
#ifdef YACC
	/*
	 * yacc file format is like the following.
	 *
	 * declarations
	 * %%
	 * rules
	 * %%
	 * programs
	 *
	 */
#define DECLARATIONS	0
#define RULES		1
#define PROGRAMS	2
	int	yaccstatus = (yaccfile) ? DECLARATIONS : PROGRAMS;
	int	inyacc     = (yaccfile) ? YES : NO;	/* NO while C source */
#endif

	lineftell = ftell(inf);
	sp = tok; token = t_def = NO; t_level = -1; level = 0; lineno = 1;
	while (GETC(!=, EOF)) {
		switch (c) {
		/*
		 * Here's where it DOESN'T handle: {
		 *	foo(a)
		 *	{
		 *	#ifdef notdef
		 *		}
		 *	#endif
		 *		if (a)
		 *			puts("hello, world");
		 *	}
		 */
		case '{':
#ifdef YACC
			if (yaccstatus == RULES && level == 0)
				inyacc = NO;
#endif
			++level;
			goto endtok;
		case '}':
			/*
			 * if level goes below zero, try and fix
			 * it, even though we've already messed up
			 */
			if (--level < 0)
				level = 0;
#ifdef GLOBAL
			/*
			 * -e flag force a function to end when a '}' appear
			 * at column 0. If -e flag not specified, all functions
			 * after funcA() would be lost.
			 *
			 * funcA() {
			 * #ifdef A
			 *	if (a) {
			 *		...
			 * #else
			 *	if (nota) {
			 *		...
			 * #endif
			 *	}
			 * }
			 */
			if (eflag && ftell(inf) == lineftell+1) {
				level = 0;
			}
#endif
#if YACC
			if (yaccstatus == RULES && level == 0)
				inyacc = YES;
#endif
			goto endtok;

		case '\n':
			SETLINE;
			/*
			 * the above 3 cases are similar in that they
			 * are special characters that also end tokens.
			 */
	endtok:		if (sp > tok) {
				*sp = EOS;
				token = YES;
				sp = tok;
			}
			else
				token = NO;
			continue;

		/*
		 * We ignore quoted strings and character constants
		 * completely.
		 */
		case '"':
		case '\'':
			(void)skip_string(c);
			break;

		/*
		 * comments can be fun; note the state is unchanged after
		 * return, in case we found:
		 *	"foo() XX comment XX { int bar; }"
		 */
		case '/':
			if (GETC(==, '*')) {
				skip_comment();
				continue;
			}
			(void)ungetc(c, inf);
			c = '/';
			goto storec;

		/* hash marks flag #define's. */
		case '#':
			if (sp == tok) {
				hash_entry();
				break;
			}
			goto storec;

		/*
		 * if we have a current token, parenthesis on
		 * level zero indicates a function.
#ifdef GLOBAL
		 * in the case of rflag == 1, if we have a current token,
		 * parenthesis on level > zero indicates a function reference.
#endif
#ifdef YACC
		 * inyacc == NO while C source.
#endif
		 */
		case '(':
#ifdef GLOBAL
			if (sflag)
				break;
#endif
#ifdef YACC
			if (inyacc == NO)
#endif
#ifdef GLOBAL
			if (!rflag && !level && token)
#else
			if (!level && token)
#endif
			{
				int	curline;

				if (sp != tok)
					*sp = EOS;
				/*
				 * grab the line immediately, we may
				 * already be wrong, for example,
				 *	foo\n
				 *	(arg1,
				 */
				getline();
				curline = lineno;
#ifdef GLOBAL
				/* to make sure. */
				if (!isstatement(tok))
#endif
				if (func_entry()) {
					++level;
					pfnote(tok, curline);
				}
				break;
			}
#ifdef GLOBAL
			else if (rflag && level && token) {
				if (sp != tok)
					*sp = EOS;
				if (!isstatement(tok) && lookup(tok)) {
					getline();
					pfnote(tok, lineno);
				}
				break;
			}
#endif
			goto storec;

		/*
		 * semi-colons indicate the end of a typedef; if we find a
		 * typedef we search for the next semi-colon of the same
		 * level as the typedef.  Ignoring "structs", they are
		 * tricky, since you can find:
		 *
		 *	"typedef long time_t;"
		 *	"typedef unsigned int u_int;"
		 *	"typedef unsigned int u_int [10];"
		 *
		 * If looking at a typedef, we save a copy of the last token
		 * found.  Then, when we find the ';' we take the current
		 * token if it starts with a valid token name, else we take
		 * the one we saved.  There's probably some reasonable
		 * alternative to this...
		 */
		case ';':
			if (t_def && level == t_level) {
				t_def = NO;
				getline();
				if (sp != tok)
					*sp = EOS;
				pfnote(tok, lineno);
				break;
			}
			goto storec;

#if YACC
		case '%':
			if (yaccstatus == DECLARATIONS || yaccstatus == RULES) {
				if (GETC(==, '%')) {
					level = 0;
					if (yaccstatus == DECLARATIONS) {
						if (!rflag) {
							getline();
							pfnote("yyparse", lineno);
						}
						yaccstatus = RULES;
					} else if (yaccstatus == RULES) {
						yaccstatus = PROGRAMS;
					}
					inyacc = (yaccstatus == PROGRAMS) ? NO : YES;
				} else if (c == '{') {
					level = 0;
					inyacc = NO;
				} else if (c == '}') {
					level = 0;
					inyacc = YES;
				} else {
					(void)ungetc(c, inf);
				}
				break;
			}
			/* else fall throuth */
#endif
		/*
		 * store characters until one that can't be part of a token
		 * comes along; check the current token against certain
		 * reserved words.
		 */
		default:
			/* ignore whitespace */
			if (c == ' ' || c == '\t') {
				int save = c;
				while (GETC(!=, EOF) && (c == ' ' || c == '\t'))
					;
				if (c == EOF)
					return;
				(void)ungetc(c, inf);
				c = save;
			}
	storec:		if (!intoken(c)) {
				if (sp == tok)
					break;
				*sp = EOS;
#ifdef GLOBAL
				/* ignore assembler in C source */
				if (!memcmp(tok, "_asm",4)) {
					while (GETC(!=, EOF) && (c == ' ' || c == '\t'))
						;
					if (c == EOF)
						return;
					if (c == '{') {
						while (GETC(!=, EOF) && c != '}') {
							if (c == '\n')
								SETLINE;
						}
					} else {
						while (GETC(!=, EOF) && c != '\n')
							;
						if (c == '\n')
							SETLINE;
					}
					if (c == EOF)
						return;
					break;
				}
				if (sflag) {
					if (!isstatement(tok)) {
						getline();
						pfnote(tok, lineno);
					}
					break;
				}
				if (!memcmp(tok, "extern",7)) {
					while (GETC(!=, EOF) && c != ';') {
						if (c == '\n')
							SETLINE;
					}
					if (c == EOF)
						return;
					break;
				}
#endif
				if (tflag) {
					/* no typedefs inside typedefs */
					if (!t_def &&
						   !memcmp(tok, "typedef",8)) {
						t_def = YES;
						t_level = level;
						break;
					}
					/* catch "typedef struct" */
					if ((!t_def || t_level < level)
					    && (!memcmp(tok, "struct", 7)
					    || !memcmp(tok, "union", 6)
					    || !memcmp(tok, "enum", 5))) {
						/*
						 * get line immediately;
						 * may change before '{'
						 */
						getline();
						if (str_entry(c))
							++level;
						break;
						/* } */
					}
				}
				sp = tok;
			}
			else if (sp != tok || begtoken(c)) {
				*sp++ = c;
				token = YES;
			}
#ifdef GLOBAL
			/* skip hex number */
			else if (sp == tok && c == '0') {
				if (GETC(==, 'x') || c == 'X') {
					while (GETC(!=, EOF) && isxdigit(c))
						;
					if (c == EOF)
						return;
				}
				(void)ungetc(c, inf);
			}
#endif
			continue;
			/* end of default */
		} /* end of switch */
		/*
		 * 'break' statement in switch block come here.
		 */
		sp = tok;
		token = NO;
	} /* end of while */
}

/*
 * func_entry --
 *	handle a function reference
 */
static int
func_entry()
{
	int	c;			/* current character */
	int	level = 0;		/* for matching '()' */

	/*
	 * Find the end of the assumed function declaration.
	 * Note that ANSI C functions can have type definitions so keep
	 * track of the parentheses nesting level.
	 */
	while (GETC(!=, EOF)) {
		switch (c) {
		case '\'':
		case '"':
			/* skip strings and character constants */
			skip_string(c);
			break;
		case '/':
			/* skip comments */
			if (GETC(==, '*'))
				skip_comment();
			break;
		case '(':
			level++;
			break;
		case ')':
			if (level == 0)
				goto fnd;
			level--;
			break;
		case '\n':
			SETLINE;
		}
	}
	return (NO);
fnd:
	/*
	 * we assume that the character after a function's right paren
	 * is a token character if it's a function and a non-token
	 * character if it's a declaration.  Comments don't count...
	 */
	for (;;) {
		while (GETC(!=, EOF) && iswhite(c))
			if (c == '\n')
				SETLINE;
		if (intoken(c) || c == '{')
			break;
		if (c == '/' && GETC(==, '*'))
			skip_comment();
		else {				/* don't ever "read" '/' */
			(void)ungetc(c, inf);
			return (NO);
		}
	}
	if (c != '{')
		(void)skip_key('{');
	return (YES);
}

/*
 * hash_entry --
 *	handle a line starting with a '#'
 */
static void
hash_entry()
{
	int	c;			/* character read */
	int	curline;		/* line started on */
	char	*sp;			/* buffer pointer */
	char	tok[MAXTOKEN];		/* storage buffer */

	/* ignore leading whitespace */
	while (GETC(!=, EOF) && (c == ' ' || c == '\t'))
		;
	(void)ungetc(c, inf);
	curline = lineno;
	for (sp = tok;;) {		/* get next token */
		if (GETC(==, EOF))
			return;
		if (iswhite(c))
			break;
		*sp++ = c;
	}
	*sp = EOS;
#ifdef GLOBAL
	if (sflag && memcmp(tok, "include", 7)) {
		(void)ungetc(c, inf);
		define_line();
		return;
	}
#endif
	if (memcmp(tok, "define", 6))	/* only interested in #define's */
		goto skip;
	
	for (;;) {			/* this doesn't handle "#define \n" */
		if (GETC(==, EOF))
			return;
		if (!iswhite(c))
			break;
	}
	for (sp = tok;;) {		/* get next token */
		*sp++ = c;
		if (GETC(==, EOF))
			return;
		/*
		 * this is where it DOESN'T handle
		 * "#define \n"
		 */
		if (!intoken(c))
			break;
	}
	*sp = EOS;
#ifdef GLOBAL
	if (rflag) {
		/*
		 * #define XXX\n
		 */
		if (c == '\n') {
			SETLINE;
			return;
		}
		/*
		 *            v
		 * #define XXX(X)	XXXXXX
		 */
		if (c == '(')
			(void)skip_key(')');
		/*
		 *               v
		 * #define XXX(X)	XXXXXX
		 */
		while (GETC(!=, EOF)) {
			if (c != ' ' && c != '\t') {
				(void)ungetc(c, inf);
				break;
			}
		}
		/*
		 *                      v
		 * #define XXX(X)	XXXXXX
		 */
		define_line();
		return;
	}
#endif
	if (dflag || c == '(') {	/* only want macros */
		getline();
		pfnote(tok, curline);
	}
skip:	if (c == '\n') {		/* get rid of rest of define */
		SETLINE
		if (*(sp - 1) != '\\')
			return;
	}
	(void)skip_key('\n');
}

#ifdef GLOBAL
		/* sorted by alphabet */
static struct words {
	char *name;
} words[] = {
	{"__P"},
	{"auto"},
	{"break"},
	{"case"},
	{"char"},
	{"continue"},
	{"default"},
	{"do"},
	{"double"},
	{"else"},
	{"extern"},
	{"float"},
	{"for"},
	{"goto"},
	{"if"},
	{"int"},
	{"long"},
	{"register"},
	{"return"},
	{"short"},
	{"sizeof"},
	{"static"},
	{"struct"},
	{"switch"},
	{"typedef"},
	{"union"},
	{"unsigned"},
	{"void"},
	{"while"},
};

static int
cmp(s1, s2)
	const void *s1, *s2;
{
	return strcmp(((struct words *)s1)->name, ((struct words *)s2)->name);
}

static int
isstatement(token)
        char *token;
{
	struct words tmp;

	tmp.name = token;
	if (bsearch(&tmp, words, sizeof(words)/sizeof(struct words), sizeof(struct words), cmp))
		return YES;
	return NO;
}

static void
define_line()
{
	int	c;			/* character read */
	int	level;			/* brace level */
	int	token;			/* if reading a token */
	char	*sp;			/* buffer pointer */
	char	tok[MAXTOKEN];		/* storage buffer */

	sp = tok; token = NO; level = 0;
	while (GETC(!=, EOF)) {
		switch (c) {
		case '{':
			++level;
			goto endtok;
		case '}':
			if (--level < 0)
				level = 0;
			goto endtok;

		case '\\':
			if (GETC(==, '\n')) {
				SETLINE;
			}
			continue;

		case '\n':
			if (sflag && token) {
				if (sp != tok)
					*sp = EOS;
				if (!isstatement(tok)) {
					getline();
					pfnote(tok, lineno);
				}
			}
			SETLINE;
			return;
	endtok:		if (sp > tok) {
				*sp = EOS;
				token = YES;
				sp = tok;
			}
			else
				token = NO;
			continue;

		case '"':
		case '\'':
			(void)skip_string(c);
			break;

		case '/':
			if (GETC(==, '*')) {
				skip_comment();
				continue;
			}
			(void)ungetc(c, inf);
			c = '/';
			goto storec;

		case '(':
			if (sflag)
				break;
			if (token) {
				if (sp != tok)
					*sp = EOS;
				getline();
				if (!isstatement(tok) && lookup(tok))
					pfnote(tok, lineno);
				break;
			}
			goto storec;

		case ';':
			goto storec;

		default:
storec:			if (!intoken(c)) {
				if (sp == tok)
					break;
				*sp = EOS;
				sp = tok;
				if (sflag) {
					if (!isstatement(tok)) {
						getline();
						pfnote(tok, lineno);
					}
					break;
				}
			}
			else if (sp != tok || begtoken(c)) {
				*sp++ = c;
				token = YES;
			}
			continue;
		}

		sp = tok;
		token = NO;
	}
}
#endif
/*
 * str_entry --
 *	handle a struct, union or enum entry
 */
static int
str_entry(c)
	int	c;			/* current character */
{
	int	curline;		/* line started on */
	char	*sp;			/* buffer pointer */
	char	tok[LINE_MAX];		/* storage buffer */

	curline = lineno;
	while (iswhite(c))
		if (GETC(==, EOF))
			return (NO);
	if (c == '{')		/* it was "struct {" */
		return (YES);
	for (sp = tok;;) {		/* get next token */
		*sp++ = c;
		if (GETC(==, EOF))
			return (NO);
		if (!intoken(c))
			break;
	}
	switch (c) {
		case '{':		/* it was "struct foo{" */
			--sp;
			break;
		case '\n':		/* it was "struct foo\n" */
			SETLINE;
			/*FALLTHROUGH*/
		default:		/* probably "struct foo " */
			while (GETC(!=, EOF))
				if (!iswhite(c))
					break;
			if (c != '{') {
				(void)ungetc(c, inf);
				return (NO);
			}
	}
	*sp = EOS;
	pfnote(tok, curline);
	return (YES);
}

/*
 * skip_comment --
 *	skip over comment
 */
void
skip_comment()
{
	int	c;			/* character read */
	int	star;			/* '*' flag */

	for (star = 0; GETC(!=, EOF);)
		switch(c) {
		/* comments don't nest, nor can they be escaped. */
		case '*':
			star = YES;
			break;
		case '/':
			if (star)
				return;
			break;
		case '\n':
			SETLINE;
			/*FALLTHROUGH*/
		default:
			star = NO;
			break;
		}
}

/*
 * skip_string --
 *	skip to the end of a string or character constant.
 */
static void
skip_string(key)
	int	key;
{
	int	c,
		skip;

	for (skip = NO; GETC(!=, EOF); )
		switch (c) {
		case '\\':		/* a backslash escapes anything */
			skip = !skip;	/* we toggle in case it's "\\" */
			break;
		case '\n':
			SETLINE;
			/*FALLTHROUGH*/
		default:
			if (c == key && !skip)
				return;
			skip = NO;
		}
}

/*
 * skip_key --
 *	skip to next char "key"
 */
int
skip_key(key)
	int	key;
{
	int	c,
		skip,
		retval;

	for (skip = retval = NO; GETC(!=, EOF);)
		switch(c) {
		case '\\':		/* a backslash escapes anything */
			skip = !skip;	/* we toggle in case it's "\\" */
			break;
		case ';':		/* special case for yacc; if one */
		case '|':		/* of these chars occurs, we may */
			retval = YES;	/* have moved out of the rule */
			break;		/* not used by C */
		case '\'':
		case '"':
			/* skip strings and character constants */
			skip_string(c);
			break;
		case '/':
			/* skip comments */
			if (GETC(==, '*')) {
				skip_comment();
				break;
			}
			(void)ungetc(c, inf);
			c = '/';
			goto norm;
		case '\n':
			SETLINE;
			/*FALLTHROUGH*/
		default:
		norm:
			if (c == key && !skip)
				return (retval);
			skip = NO;
		}
	return (retval);
}
