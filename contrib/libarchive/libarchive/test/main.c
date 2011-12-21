/*
 * Copyright (c) 2003-2009 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "test.h"
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <time.h>

/*
 * This same file is used pretty much verbatim for all test harnesses.
 *
 * The next few lines are the only differences.
 * TODO: Move this into a separate configuration header, have all test
 * suites share one copy of this file.
 */
__FBSDID("$FreeBSD: head/lib/libarchive/test/main.c 201247 2009-12-30 05:59:21Z kientzle $");
#define KNOWNREF	"test_compat_gtar_1.tar.uu"
#define	ENVBASE "LIBARCHIVE" /* Prefix for environment variables. */
#undef	PROGRAM              /* Testing a library, not a program. */
#define	LIBRARY	"libarchive"
#define	EXTRA_DUMP(x)	archive_error_string((struct archive *)(x))
#define	EXTRA_VERSION	archive_version()

/*
 *
 * Windows support routines
 *
 * Note: Configuration is a tricky issue.  Using HAVE_* feature macros
 * in the test harness is dangerous because they cover up
 * configuration errors.  The classic example of this is omitting a
 * configure check.  If libarchive and libarchive_test both look for
 * the same feature macro, such errors are hard to detect.  Platform
 * macros (e.g., _WIN32 or __GNUC__) are a little better, but can
 * easily lead to very messy code.  It's best to limit yourself
 * to only the most generic programming techniques in the test harness
 * and thus avoid conditionals altogether.  Where that's not possible,
 * try to minimize conditionals by grouping platform-specific tests in
 * one place (e.g., test_acl_freebsd) or by adding new assert()
 * functions (e.g., assertMakeHardlink()) to cover up platform
 * differences.  Platform-specific coding in libarchive_test is often
 * a symptom that some capability is missing from libarchive itself.
 */
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <windows.h>
#ifndef F_OK
#define F_OK (0)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m)  ((m) & _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m)  ((m) & _S_IFREG)
#endif
#if !defined(__BORLANDC__)
#define access _access
#define chdir _chdir
#endif
#ifndef fileno
#define fileno _fileno
#endif
/*#define fstat _fstat64*/
#if !defined(__BORLANDC__)
#define getcwd _getcwd
#endif
#define lstat stat
/*#define lstat _stat64*/
/*#define stat _stat64*/
#define rmdir _rmdir
#if !defined(__BORLANDC__)
#define strdup _strdup
#define umask _umask
#endif
#define int64_t __int64
#endif

#if defined(HAVE__CrtSetReportMode)
# include <crtdbg.h>
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
void *GetFunctionKernel32(const char *name)
{
	static HINSTANCE lib;
	static int set;
	if (!set) {
		set = 1;
		lib = LoadLibrary("kernel32.dll");
	}
	if (lib == NULL) {
		fprintf(stderr, "Can't load kernel32.dll?!\n");
		exit(1);
	}
	return (void *)GetProcAddress(lib, name);
}

static int
my_CreateSymbolicLinkA(const char *linkname, const char *target, int flags)
{
	static BOOLEAN (WINAPI *f)(LPCSTR, LPCSTR, DWORD);
	static int set;
	if (!set) {
		set = 1;
		f = GetFunctionKernel32("CreateSymbolicLinkA");
	}
	return f == NULL ? 0 : (*f)(linkname, target, flags);
}

static int
my_CreateHardLinkA(const char *linkname, const char *target)
{
	static BOOLEAN (WINAPI *f)(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES);
	static int set;
	if (!set) {
		set = 1;
		f = GetFunctionKernel32("CreateHardLinkA");
	}
	return f == NULL ? 0 : (*f)(linkname, target, NULL);
}

int
my_GetFileInformationByName(const char *path, BY_HANDLE_FILE_INFORMATION *bhfi)
{
	HANDLE h;
	int r;

	memset(bhfi, 0, sizeof(*bhfi));
	h = CreateFile(path, FILE_READ_ATTRIBUTES, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return (0);
	r = GetFileInformationByHandle(h, bhfi);
	CloseHandle(h);
	return (r);
}
#endif

#if defined(HAVE__CrtSetReportMode)
static void
invalid_parameter_handler(const wchar_t * expression,
    const wchar_t * function, const wchar_t * file,
    unsigned int line, uintptr_t pReserved)
{
	/* nop */
}
#endif

/*
 *
 * OPTIONS FLAGS
 *
 */

/* Enable core dump on failure. */
static int dump_on_failure = 0;
/* Default is to remove temp dirs and log data for successful tests. */
static int keep_temp_files = 0;
/* Default is to just report pass/fail for each test. */
static int verbosity = 0;
#define	VERBOSITY_SUMMARY_ONLY -1 /* -q */
#define VERBOSITY_PASSFAIL 0   /* Default */
#define VERBOSITY_LIGHT_REPORT 1 /* -v */
#define VERBOSITY_FULL 2 /* -vv */
/* A few places generate even more output for verbosity > VERBOSITY_FULL,
 * mostly for debugging the test harness itself. */
/* Cumulative count of assertion failures. */
static int failures = 0;
/* Cumulative count of reported skips. */
static int skips = 0;
/* Cumulative count of assertions checked. */
static int assertions = 0;

/* Directory where uuencoded reference files can be found. */
static const char *refdir;

/*
 * Report log information selectively to console and/or disk log.
 */
static int log_console = 0;
static FILE *logfile;
static void
vlogprintf(const char *fmt, va_list ap)
{
#ifdef va_copy
	va_list lfap;
	va_copy(lfap, ap);
#endif
	if (log_console)
		vfprintf(stdout, fmt, ap);
	if (logfile != NULL)
#ifdef va_copy
		vfprintf(logfile, fmt, lfap);
	va_end(lfap);
#else
		vfprintf(logfile, fmt, ap);
#endif
}

static void
logprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlogprintf(fmt, ap);
	va_end(ap);
}

/* Set up a message to display only if next assertion fails. */
static char msgbuff[4096];
static const char *msg, *nextmsg;
void
failure(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsprintf(msgbuff, fmt, ap);
	va_end(ap);
	nextmsg = msgbuff;
}

/*
 * Copy arguments into file-local variables.
 * This was added to permit vararg assert() functions without needing
 * variadic wrapper macros.  Turns out that the vararg capability is almost
 * never used, so almost all of the vararg assertions can be simplified
 * by removing the vararg capability and reworking the wrapper macro to
 * pass __FILE__, __LINE__ directly into the function instead of using
 * this hook.  I suspect this machinery is used so rarely that we
 * would be better off just removing it entirely.  That would simplify
 * the code here noticably.
 */
static const char *test_filename;
static int test_line;
static void *test_extra;
void assertion_setup(const char *filename, int line)
{
	test_filename = filename;
	test_line = line;
}

/* Called at the beginning of each assert() function. */
static void
assertion_count(const char *file, int line)
{
	(void)file; /* UNUSED */
	(void)line; /* UNUSED */
	++assertions;
	/* Proper handling of "failure()" message. */
	msg = nextmsg;
	nextmsg = NULL;
	/* Uncomment to print file:line after every assertion.
	 * Verbose, but occasionally useful in tracking down crashes. */
	/* printf("Checked %s:%d\n", file, line); */
}

/*
 * For each test source file, we remember how many times each
 * assertion was reported.  Cleared before each new test,
 * used by test_summarize().
 */
static struct line {
	int count;
	int skip;
}  failed_lines[10000];

/* Count this failure, setup up log destination and handle initial report. */
static void
failure_start(const char *filename, int line, const char *fmt, ...)
{
	va_list ap;

	/* Record another failure for this line. */
	++failures;
	/* test_filename = filename; */
	failed_lines[line].count++;

	/* Determine whether to log header to console. */
	switch (verbosity) {
	case VERBOSITY_LIGHT_REPORT:
		log_console = (failed_lines[line].count < 2);
		break;
	default:
		log_console = (verbosity >= VERBOSITY_FULL);
	}

	/* Log file:line header for this failure */
	va_start(ap, fmt);
#if _MSC_VER
	logprintf("%s(%d): ", filename, line);
#else
	logprintf("%s:%d: ", filename, line);
#endif
	vlogprintf(fmt, ap);
	va_end(ap);
	logprintf("\n");

	if (msg != NULL && msg[0] != '\0') {
		logprintf("   Description: %s\n", msg);
		msg = NULL;
	}

	/* Determine whether to log details to console. */
	if (verbosity == VERBOSITY_LIGHT_REPORT)
		log_console = 0;
}

/* Complete reporting of failed tests. */
/*
 * The 'extra' hook here is used by libarchive to include libarchive
 * error messages with assertion failures.  It could also be used
 * to add strerror() output, for example.  Just define the EXTRA_DUMP()
 * macro appropriately.
 */
static void
failure_finish(void *extra)
{
	(void)extra; /* UNUSED (maybe) */
#ifdef EXTRA_DUMP
	if (extra != NULL)
		logprintf("   detail: %s\n", EXTRA_DUMP(extra));
#endif

	if (dump_on_failure) {
		fprintf(stderr,
		    " *** forcing core dump so failure can be debugged ***\n");
		abort();
		exit(1);
	}
}

/* Inform user that we're skipping some checks. */
void
test_skipping(const char *fmt, ...)
{
	char buff[1024];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buff, fmt, ap);
	va_end(ap);
	/* failure_start() isn't quite right, but is awfully convenient. */
	failure_start(test_filename, test_line, "SKIPPING: %s", buff);
	--failures; /* Undo failures++ in failure_start() */
	/* Don't failure_finish() here. */
	/* Mark as skip, so doesn't count as failed test. */
	failed_lines[test_line].skip = 1;
	++skips;
}

/*
 *
 * ASSERTIONS
 *
 */

/* Generic assert() just displays the failed condition. */
int
assertion_assert(const char *file, int line, int value,
    const char *condition, void *extra)
{
	assertion_count(file, line);
	if (!value) {
		failure_start(file, line, "Assertion failed: %s", condition);
		failure_finish(extra);
	}
	return (value);
}

/* chdir() and report any errors */
int
assertion_chdir(const char *file, int line, const char *pathname)
{
	assertion_count(file, line);
	if (chdir(pathname) == 0)
		return (1);
	failure_start(file, line, "chdir(\"%s\")", pathname);
	failure_finish(NULL);
	return (0);

}

/* Verify two integers are equal. */
int
assertion_equal_int(const char *file, int line,
    long long v1, const char *e1, long long v2, const char *e2, void *extra)
{
	assertion_count(file, line);
	if (v1 == v2)
		return (1);
	failure_start(file, line, "%s != %s", e1, e2);
	logprintf("      %s=%lld (0x%llx, 0%llo)\n", e1, v1, v1, v1);
	logprintf("      %s=%lld (0x%llx, 0%llo)\n", e2, v2, v2, v2);
	failure_finish(extra);
	return (0);
}

static void strdump(const char *e, const char *p)
{
	const char *q = p;

	logprintf("      %s = ", e);
	if (p == NULL) {
		logprintf("NULL");
		return;
	}
	logprintf("\"");
	while (*p != '\0') {
		unsigned int c = 0xff & *p++;
		switch (c) {
		case '\a': printf("\a"); break;
		case '\b': printf("\b"); break;
		case '\n': printf("\n"); break;
		case '\r': printf("\r"); break;
		default:
			if (c >= 32 && c < 127)
				logprintf("%c", c);
			else
				logprintf("\\x%02X", c);
		}
	}
	logprintf("\"");
	logprintf(" (length %d)\n", q == NULL ? -1 : (int)strlen(q));
}

/* Verify two strings are equal, dump them if not. */
int
assertion_equal_string(const char *file, int line,
    const char *v1, const char *e1,
    const char *v2, const char *e2,
    void *extra)
{
	assertion_count(file, line);
	if (v1 == v2 || (v1 != NULL && v2 != NULL && strcmp(v1, v2) == 0))
		return (1);
	failure_start(file, line, "%s != %s", e1, e2);
	strdump(e1, v1);
	strdump(e2, v2);
	failure_finish(extra);
	return (0);
}

static void
wcsdump(const char *e, const wchar_t *w)
{
	logprintf("      %s = ", e);
	if (w == NULL) {
		logprintf("(null)");
		return;
	}
	logprintf("\"");
	while (*w != L'\0') {
		unsigned int c = *w++;
		if (c >= 32 && c < 127)
			logprintf("%c", c);
		else if (c < 256)
			logprintf("\\x%02X", c);
		else if (c < 0x10000)
			logprintf("\\u%04X", c);
		else
			logprintf("\\U%08X", c);
	}
	logprintf("\"\n");
}

#ifndef HAVE_WCSCMP
static int
wcscmp(const wchar_t *s1, const wchar_t *s2)
{

	while (*s1 == *s2++) {
		if (*s1++ == L'\0')
			return 0;
	}
	if (*s1 > *--s2)
		return 1;
	else
		return -1;
}
#endif

/* Verify that two wide strings are equal, dump them if not. */
int
assertion_equal_wstring(const char *file, int line,
    const wchar_t *v1, const char *e1,
    const wchar_t *v2, const char *e2,
    void *extra)
{
	assertion_count(file, line);
	if (v1 == v2 || wcscmp(v1, v2) == 0)
		return (1);
	failure_start(file, line, "%s != %s", e1, e2);
	wcsdump(e1, v1);
	wcsdump(e2, v2);
	failure_finish(extra);
	return (0);
}

/*
 * Pretty standard hexdump routine.  As a bonus, if ref != NULL, then
 * any bytes in p that differ from ref will be highlighted with '_'
 * before and after the hex value.
 */
static void
hexdump(const char *p, const char *ref, size_t l, size_t offset)
{
	size_t i, j;
	char sep;

	if (p == NULL) {
		logprintf("(null)\n");
		return;
	}
	for(i=0; i < l; i+=16) {
		logprintf("%04x", (unsigned)(i + offset));
		sep = ' ';
		for (j = 0; j < 16 && i + j < l; j++) {
			if (ref != NULL && p[i + j] != ref[i + j])
				sep = '_';
			logprintf("%c%02x", sep, 0xff & (int)p[i+j]);
			if (ref != NULL && p[i + j] == ref[i + j])
				sep = ' ';
		}
		for (; j < 16; j++) {
			logprintf("%c  ", sep);
			sep = ' ';
		}
		logprintf("%c", sep);
		for (j=0; j < 16 && i + j < l; j++) {
			int c = p[i + j];
			if (c >= ' ' && c <= 126)
				logprintf("%c", c);
			else
				logprintf(".");
		}
		logprintf("\n");
	}
}

/* Verify that two blocks of memory are the same, display the first
 * block of differences if they're not. */
int
assertion_equal_mem(const char *file, int line,
    const void *_v1, const char *e1,
    const void *_v2, const char *e2,
    size_t l, const char *ld, void *extra)
{
	const char *v1 = (const char *)_v1;
	const char *v2 = (const char *)_v2;
	size_t offset;

	assertion_count(file, line);
	if (v1 == v2 || (v1 != NULL && v2 != NULL && memcmp(v1, v2, l) == 0))
		return (1);

	failure_start(file, line, "%s != %s", e1, e2);
	logprintf("      size %s = %d\n", ld, (int)l);
	/* Dump 48 bytes (3 lines) so that the first difference is
	 * in the second line. */
	offset = 0;
	while (l > 64 && memcmp(v1, v2, 32) == 0) {
		/* Two lines agree, so step forward one line. */
		v1 += 16;
		v2 += 16;
		l -= 16;
		offset += 16;
	}
	logprintf("      Dump of %s\n", e1);
	hexdump(v1, v2, l < 64 ? l : 64, offset);
	logprintf("      Dump of %s\n", e2);
	hexdump(v2, v1, l < 64 ? l : 64, offset);
	logprintf("\n");
	failure_finish(extra);
	return (0);
}

/* Verify that the named file exists and is empty. */
int
assertion_empty_file(const char *f1fmt, ...)
{
	char buff[1024];
	char f1[1024];
	struct stat st;
	va_list ap;
	ssize_t s;
	FILE *f;

	assertion_count(test_filename, test_line);
	va_start(ap, f1fmt);
	vsprintf(f1, f1fmt, ap);
	va_end(ap);

	if (stat(f1, &st) != 0) {
		failure_start(test_filename, test_line, "Stat failed: %s", f1);
		failure_finish(NULL);
		return (0);
	}
	if (st.st_size == 0)
		return (1);

	failure_start(test_filename, test_line, "File should be empty: %s", f1);
	logprintf("    File size: %d\n", (int)st.st_size);
	logprintf("    Contents:\n");
	f = fopen(f1, "rb");
	if (f == NULL) {
		logprintf("    Unable to open %s\n", f1);
	} else {
		s = ((off_t)sizeof(buff) < st.st_size) ?
		    (ssize_t)sizeof(buff) : (ssize_t)st.st_size;
		s = fread(buff, 1, s, f);
		hexdump(buff, NULL, s, 0);
		fclose(f);
	}
	failure_finish(NULL);
	return (0);
}

/* Verify that the named file exists and is not empty. */
int
assertion_non_empty_file(const char *f1fmt, ...)
{
	char f1[1024];
	struct stat st;
	va_list ap;

	assertion_count(test_filename, test_line);
	va_start(ap, f1fmt);
	vsprintf(f1, f1fmt, ap);
	va_end(ap);

	if (stat(f1, &st) != 0) {
		failure_start(test_filename, test_line, "Stat failed: %s", f1);
		failure_finish(NULL);
		return (0);
	}
	if (st.st_size == 0) {
		failure_start(test_filename, test_line, "File empty: %s", f1);
		failure_finish(NULL);
		return (0);
	}
	return (1);
}

/* Verify that two files have the same contents. */
/* TODO: hexdump the first bytes that actually differ. */
int
assertion_equal_file(const char *fn1, const char *f2pattern, ...)
{
	char fn2[1024];
	va_list ap;
	char buff1[1024];
	char buff2[1024];
	FILE *f1, *f2;
	int n1, n2;

	assertion_count(test_filename, test_line);
	va_start(ap, f2pattern);
	vsprintf(fn2, f2pattern, ap);
	va_end(ap);

	f1 = fopen(fn1, "rb");
	f2 = fopen(fn2, "rb");
	for (;;) {
		n1 = fread(buff1, 1, sizeof(buff1), f1);
		n2 = fread(buff2, 1, sizeof(buff2), f2);
		if (n1 != n2)
			break;
		if (n1 == 0 && n2 == 0) {
			fclose(f1);
			fclose(f2);
			return (1);
		}
		if (memcmp(buff1, buff2, n1) != 0)
			break;
	}
	fclose(f1);
	fclose(f2);
	failure_start(test_filename, test_line, "Files not identical");
	logprintf("  file1=\"%s\"\n", fn1);
	logprintf("  file2=\"%s\"\n", fn2);
	failure_finish(test_extra);
	return (0);
}

/* Verify that the named file does exist. */
int
assertion_file_exists(const char *fpattern, ...)
{
	char f[1024];
	va_list ap;

	assertion_count(test_filename, test_line);
	va_start(ap, fpattern);
	vsprintf(f, fpattern, ap);
	va_end(ap);

#if defined(_WIN32) && !defined(__CYGWIN__)
	if (!_access(f, 0))
		return (1);
#else
	if (!access(f, F_OK))
		return (1);
#endif
	failure_start(test_filename, test_line, "File should exist: %s", f);
	failure_finish(test_extra);
	return (0);
}

/* Verify that the named file doesn't exist. */
int
assertion_file_not_exists(const char *fpattern, ...)
{
	char f[1024];
	va_list ap;

	assertion_count(test_filename, test_line);
	va_start(ap, fpattern);
	vsprintf(f, fpattern, ap);
	va_end(ap);

#if defined(_WIN32) && !defined(__CYGWIN__)
	if (_access(f, 0))
		return (1);
#else
	if (access(f, F_OK))
		return (1);
#endif
	failure_start(test_filename, test_line, "File should not exist: %s", f);
	failure_finish(test_extra);
	return (0);
}

/* Compare the contents of a file to a block of memory. */
int
assertion_file_contents(const void *buff, int s, const char *fpattern, ...)
{
	char fn[1024];
	va_list ap;
	char *contents;
	FILE *f;
	int n;

	assertion_count(test_filename, test_line);
	va_start(ap, fpattern);
	vsprintf(fn, fpattern, ap);
	va_end(ap);

	f = fopen(fn, "rb");
	if (f == NULL) {
		failure_start(test_filename, test_line,
		    "File should exist: %s", fn);
		failure_finish(test_extra);
		return (0);
	}
	contents = malloc(s * 2);
	n = fread(contents, 1, s * 2, f);
	fclose(f);
	if (n == s && memcmp(buff, contents, s) == 0) {
		free(contents);
		return (1);
	}
	failure_start(test_filename, test_line, "File contents don't match");
	logprintf("  file=\"%s\"\n", fn);
	if (n > 0)
		hexdump(contents, buff, n > 512 ? 512 : n, 0);
	else {
		logprintf("  File empty, contents should be:\n");
		hexdump(buff, NULL, s > 512 ? 512 : n, 0);
	}
	failure_finish(test_extra);
	free(contents);
	return (0);
}

/* Check the contents of a text file, being tolerant of line endings. */
int
assertion_text_file_contents(const char *buff, const char *fn)
{
	char *contents;
	const char *btxt, *ftxt;
	FILE *f;
	int n, s;

	assertion_count(test_filename, test_line);
	f = fopen(fn, "r");
	s = strlen(buff);
	contents = malloc(s * 2 + 128);
	n = fread(contents, 1, s * 2 + 128 - 1, f);
	if (n >= 0)
		contents[n] = '\0';
	fclose(f);
	/* Compare texts. */
	btxt = buff;
	ftxt = (const char *)contents;
	while (*btxt != '\0' && *ftxt != '\0') {
		if (*btxt == *ftxt) {
			++btxt;
			++ftxt;
			continue;
		}
		if (btxt[0] == '\n' && ftxt[0] == '\r' && ftxt[1] == '\n') {
			/* Pass over different new line characters. */
			++btxt;
			ftxt += 2;
			continue;
		}
		break;
	}
	if (*btxt == '\0' && *ftxt == '\0') {
		free(contents);
		return (1);
	}
	failure_start(test_filename, test_line, "Contents don't match");
	logprintf("  file=\"%s\"\n", fn);
	if (n > 0)
		hexdump(contents, buff, n, 0);
	else {
		logprintf("  File empty, contents should be:\n");
		hexdump(buff, NULL, s, 0);
	}
	failure_finish(test_extra);
	free(contents);
	return (0);
}

/* Test that two paths point to the same file. */
/* As a side-effect, asserts that both files exist. */
static int
is_hardlink(const char *file, int line,
    const char *path1, const char *path2)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	BY_HANDLE_FILE_INFORMATION bhfi1, bhfi2;
	int r;

	assertion_count(file, line);
	r = my_GetFileInformationByName(path1, &bhfi1);
	if (r == 0) {
		failure_start(file, line, "File %s can't be inspected?", path1);
		failure_finish(NULL);
		return (0);
	}
	r = my_GetFileInformationByName(path2, &bhfi2);
	if (r == 0) {
		failure_start(file, line, "File %s can't be inspected?", path2);
		failure_finish(NULL);
		return (0);
	}
	return (bhfi1.dwVolumeSerialNumber == bhfi2.dwVolumeSerialNumber
		&& bhfi1.nFileIndexHigh == bhfi2.nFileIndexHigh
		&& bhfi1.nFileIndexLow == bhfi2.nFileIndexLow);
#else
	struct stat st1, st2;
	int r;

	assertion_count(file, line);
	r = lstat(path1, &st1);
	if (r != 0) {
		failure_start(file, line, "File should exist: %s", path1);
		failure_finish(NULL);
		return (0);
	}
	r = lstat(path2, &st2);
	if (r != 0) {
		failure_start(file, line, "File should exist: %s", path2);
		failure_finish(NULL);
		return (0);
	}
	return (st1.st_ino == st2.st_ino && st1.st_dev == st2.st_dev);
#endif
}

int
assertion_is_hardlink(const char *file, int line,
    const char *path1, const char *path2)
{
	if (is_hardlink(file, line, path1, path2))
		return (1);
	failure_start(file, line,
	    "Files %s and %s are not hardlinked", path1, path2);
	failure_finish(NULL);
	return (0);
}

int
assertion_is_not_hardlink(const char *file, int line,
    const char *path1, const char *path2)
{
	if (!is_hardlink(file, line, path1, path2))
		return (1);
	failure_start(file, line,
	    "Files %s and %s should not be hardlinked", path1, path2);
	failure_finish(NULL);
	return (0);
}

/* Verify a/b/mtime of 'pathname'. */
/* If 'recent', verify that it's within last 10 seconds. */
static int
assertion_file_time(const char *file, int line,
    const char *pathname, long t, long nsec, char type, int recent)
{
	long long filet, filet_nsec;
	int r;

#if defined(_WIN32) && !defined(__CYGWIN__)
#define EPOC_TIME	(116444736000000000ULL)
	FILETIME ftime, fbirthtime, fatime, fmtime;
	ULARGE_INTEGER wintm;
	HANDLE h;
	ftime.dwLowDateTime = 0;
	ftime.dwHighDateTime = 0;

	assertion_count(file, line);
	h = CreateFile(pathname, FILE_READ_ATTRIBUTES, 0, NULL,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		failure_start(file, line, "Can't access %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	r = GetFileTime(h, &fbirthtime, &fatime, &fmtime);
	switch (type) {
	case 'a': ftime = fatime; break;
	case 'b': ftime = fbirthtime; break;
	case 'm': ftime = fmtime; break;
	}
	CloseHandle(h);
	if (r == 0) {
		failure_start(file, line, "Can't GetFileTime %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	wintm.LowPart = ftime.dwLowDateTime;
	wintm.HighPart = ftime.dwHighDateTime;
	filet = (wintm.QuadPart - EPOC_TIME) / 10000000;
	filet_nsec = ((wintm.QuadPart - EPOC_TIME) % 10000000) * 100;
	nsec = (nsec / 100) * 100; /* Round the request */
#else
	struct stat st;

	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0) {
		failure_start(file, line, "Can't stat %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	switch (type) {
	case 'a': filet = st.st_atime; break;
	case 'm': filet = st.st_mtime; break;
	case 'b': filet = 0; break;
	default: fprintf(stderr, "INTERNAL: Bad type %c for file time", type);
		exit(1);
	}
#if defined(__FreeBSD__)
	switch (type) {
	case 'a': filet_nsec = st.st_atimespec.tv_nsec; break;
	case 'b': filet = st.st_birthtime;
		filet_nsec = st.st_birthtimespec.tv_nsec; break;
	case 'm': filet_nsec = st.st_mtimespec.tv_nsec; break;
	default: fprintf(stderr, "INTERNAL: Bad type %c for file time", type);
		exit(1);
	}
	/* FreeBSD generally only stores to microsecond res, so round. */
	filet_nsec = (filet_nsec / 1000) * 1000;
	nsec = (nsec / 1000) * 1000;
#else
	filet_nsec = nsec = 0;	/* Generic POSIX only has whole seconds. */
	if (type == 'b') return (1); /* Generic POSIX doesn't have birthtime */
#if defined(__HAIKU__)
	if (type == 'a') return (1); /* Haiku doesn't have atime. */
#endif
#endif
#endif
	if (recent) {
		/* Check that requested time is up-to-date. */
		time_t now = time(NULL);
		if (filet < now - 10 || filet > now + 1) {
			failure_start(file, line,
			    "File %s has %ctime %ld, %ld seconds ago\n",
			    pathname, type, filet, now - filet);
			failure_finish(NULL);
			return (0);
		}
	} else if (filet != t || filet_nsec != nsec) {
		failure_start(file, line,
		    "File %s has %ctime %ld.%09ld, expected %ld.%09ld",
		    pathname, type, filet, filet_nsec, t, nsec);
		failure_finish(NULL);
		return (0);
	}
	return (1);
}

/* Verify atime of 'pathname'. */
int
assertion_file_atime(const char *file, int line,
    const char *pathname, long t, long nsec)
{
	return assertion_file_time(file, line, pathname, t, nsec, 'a', 0);
}

/* Verify atime of 'pathname' is up-to-date. */
int
assertion_file_atime_recent(const char *file, int line, const char *pathname)
{
	return assertion_file_time(file, line, pathname, 0, 0, 'a', 1);
}

/* Verify birthtime of 'pathname'. */
int
assertion_file_birthtime(const char *file, int line,
    const char *pathname, long t, long nsec)
{
	return assertion_file_time(file, line, pathname, t, nsec, 'b', 0);
}

/* Verify birthtime of 'pathname' is up-to-date. */
int
assertion_file_birthtime_recent(const char *file, int line,
    const char *pathname)
{
	return assertion_file_time(file, line, pathname, 0, 0, 'b', 1);
}

/* Verify mtime of 'pathname'. */
int
assertion_file_mtime(const char *file, int line,
    const char *pathname, long t, long nsec)
{
	return assertion_file_time(file, line, pathname, t, nsec, 'm', 0);
}

/* Verify mtime of 'pathname' is up-to-date. */
int
assertion_file_mtime_recent(const char *file, int line, const char *pathname)
{
	return assertion_file_time(file, line, pathname, 0, 0, 'm', 1);
}

/* Verify number of links to 'pathname'. */
int
assertion_file_nlinks(const char *file, int line,
    const char *pathname, int nlinks)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	BY_HANDLE_FILE_INFORMATION bhfi;
	int r;

	assertion_count(file, line);
	r = my_GetFileInformationByName(pathname, &bhfi);
	if (r != 0 && bhfi.nNumberOfLinks == (DWORD)nlinks)
		return (1);
	failure_start(file, line, "File %s has %d links, expected %d",
	    pathname, bhfi.nNumberOfLinks, nlinks);
	failure_finish(NULL);
	return (0);
#else
	struct stat st;
	int r;

	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r == 0 && st.st_nlink == nlinks)
			return (1);
	failure_start(file, line, "File %s has %d links, expected %d",
	    pathname, st.st_nlink, nlinks);
	failure_finish(NULL);
	return (0);
#endif
}

/* Verify size of 'pathname'. */
int
assertion_file_size(const char *file, int line, const char *pathname, long size)
{
	int64_t filesize;
	int r;

	assertion_count(file, line);
#if defined(_WIN32) && !defined(__CYGWIN__)
	{
		BY_HANDLE_FILE_INFORMATION bhfi;
		r = !my_GetFileInformationByName(pathname, &bhfi);
		filesize = ((int64_t)bhfi.nFileSizeHigh << 32) + bhfi.nFileSizeLow;
	}
#else
	{
		struct stat st;
		r = lstat(pathname, &st);
		filesize = st.st_size;
	}
#endif
	if (r == 0 && filesize == size)
			return (1);
	failure_start(file, line, "File %s has size %ld, expected %ld",
	    pathname, (long)filesize, (long)size);
	failure_finish(NULL);
	return (0);
}

/* Assert that 'pathname' is a dir.  If mode >= 0, verify that too. */
int
assertion_is_dir(const char *file, int line, const char *pathname, int mode)
{
	struct stat st;
	int r;

#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)mode; /* UNUSED */
#endif
	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0) {
		failure_start(file, line, "Dir should exist: %s", pathname);
		failure_finish(NULL);
		return (0);
	}
	if (!S_ISDIR(st.st_mode)) {
		failure_start(file, line, "%s is not a dir", pathname);
		failure_finish(NULL);
		return (0);
	}
#if !defined(_WIN32) || defined(__CYGWIN__)
	/* Windows doesn't handle permissions the same way as POSIX,
	 * so just ignore the mode tests. */
	/* TODO: Can we do better here? */
	if (mode >= 0 && mode != (st.st_mode & 07777)) {
		failure_start(file, line, "Dir %s has wrong mode", pathname);
		logprintf("  Expected: 0%3o\n", mode);
		logprintf("  Found: 0%3o\n", st.st_mode & 07777);
		failure_finish(NULL);
		return (0);
	}
#endif
	return (1);
}

/* Verify that 'pathname' is a regular file.  If 'mode' is >= 0,
 * verify that too. */
int
assertion_is_reg(const char *file, int line, const char *pathname, int mode)
{
	struct stat st;
	int r;

#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)mode; /* UNUSED */
#endif
	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0 || !S_ISREG(st.st_mode)) {
		failure_start(file, line, "File should exist: %s", pathname);
		failure_finish(NULL);
		return (0);
	}
#if !defined(_WIN32) || defined(__CYGWIN__)
	/* Windows doesn't handle permissions the same way as POSIX,
	 * so just ignore the mode tests. */
	/* TODO: Can we do better here? */
	if (mode >= 0 && mode != (st.st_mode & 07777)) {
		failure_start(file, line, "File %s has wrong mode", pathname);
		logprintf("  Expected: 0%3o\n", mode);
		logprintf("  Found: 0%3o\n", st.st_mode & 07777);
		failure_finish(NULL);
		return (0);
	}
#endif
	return (1);
}

/* Check whether 'pathname' is a symbolic link.  If 'contents' is
 * non-NULL, verify that the symlink has those contents. */
static int
is_symlink(const char *file, int line,
    const char *pathname, const char *contents)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)pathname; /* UNUSED */
	(void)contents; /* UNUSED */
	assertion_count(file, line);
	/* Windows sort-of has real symlinks, but they're only usable
	 * by privileged users and are crippled even then, so there's
	 * really not much point in bothering with this. */
	return (0);
#else
	char buff[300];
	struct stat st;
	ssize_t linklen;
	int r;

	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0) {
		failure_start(file, line,
		    "Symlink should exist: %s", pathname);
		failure_finish(NULL);
		return (0);
	}
	if (!S_ISLNK(st.st_mode))
		return (0);
	if (contents == NULL)
		return (1);
	linklen = readlink(pathname, buff, sizeof(buff));
	if (linklen < 0) {
		failure_start(file, line, "Can't read symlink %s", pathname);
		failure_finish(NULL);
		return (0);
	}
	buff[linklen] = '\0';
	if (strcmp(buff, contents) != 0)
		return (0);
	return (1);
#endif
}

/* Assert that path is a symlink that (optionally) contains contents. */
int
assertion_is_symlink(const char *file, int line,
    const char *path, const char *contents)
{
	if (is_symlink(file, line, path, contents))
		return (1);
	if (contents)
		failure_start(file, line, "File %s is not a symlink to %s",
		    path, contents);
	else
		failure_start(file, line, "File %s is not a symlink", path);
	failure_finish(NULL);
	return (0);
}


/* Create a directory and report any errors. */
int
assertion_make_dir(const char *file, int line, const char *dirname, int mode)
{
	assertion_count(file, line);
#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)mode; /* UNUSED */
	if (0 == _mkdir(dirname))
		return (1);
#else
	if (0 == mkdir(dirname, mode))
		return (1);
#endif
	failure_start(file, line, "Could not create directory %s", dirname);
	failure_finish(NULL);
	return(0);
}

/* Create a file with the specified contents and report any failures. */
int
assertion_make_file(const char *file, int line,
    const char *path, int mode, const char *contents)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	/* TODO: Rework this to set file mode as well. */
	FILE *f;
	(void)mode; /* UNUSED */
	assertion_count(file, line);
	f = fopen(path, "wb");
	if (f == NULL) {
		failure_start(file, line, "Could not create file %s", path);
		failure_finish(NULL);
		return (0);
	}
	if (contents != NULL) {
		if (strlen(contents)
		    != fwrite(contents, 1, strlen(contents), f)) {
			fclose(f);
			failure_start(file, line,
			    "Could not write file %s", path);
			failure_finish(NULL);
			return (0);
		}
	}
	fclose(f);
	return (1);
#else
	int fd;
	assertion_count(file, line);
	fd = open(path, O_CREAT | O_WRONLY, mode >= 0 ? mode : 0644);
	if (fd < 0) {
		failure_start(file, line, "Could not create %s", path);
		failure_finish(NULL);
		return (0);
	}
	if (contents != NULL) {
		if ((ssize_t)strlen(contents)
		    != write(fd, contents, strlen(contents))) {
			close(fd);
			failure_start(file, line, "Could not write to %s", path);
			failure_finish(NULL);
			return (0);
		}
	}
	close(fd);
	return (1);
#endif
}

/* Create a hardlink and report any failures. */
int
assertion_make_hardlink(const char *file, int line,
    const char *newpath, const char *linkto)
{
	int succeeded;

	assertion_count(file, line);
#if defined(_WIN32) && !defined(__CYGWIN__)
	succeeded = my_CreateHardLinkA(newpath, linkto);
#elif HAVE_LINK
	succeeded = !link(linkto, newpath);
#else
	succeeded = 0;
#endif
	if (succeeded)
		return (1);
	failure_start(file, line, "Could not create hardlink");
	logprintf("   New link: %s\n", newpath);
	logprintf("   Old name: %s\n", linkto);
	failure_finish(NULL);
	return(0);
}

/* Create a symlink and report any failures. */
int
assertion_make_symlink(const char *file, int line,
    const char *newpath, const char *linkto)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	int targetIsDir = 0;  /* TODO: Fix this */
	assertion_count(file, line);
	if (my_CreateSymbolicLinkA(newpath, linkto, targetIsDir))
		return (1);
#elif HAVE_SYMLINK
	assertion_count(file, line);
	if (0 == symlink(linkto, newpath))
		return (1);
#endif
	failure_start(file, line, "Could not create symlink");
	logprintf("   New link: %s\n", newpath);
	logprintf("   Old name: %s\n", linkto);
	failure_finish(NULL);
	return(0);
}

/* Set umask, report failures. */
int
assertion_umask(const char *file, int line, int mask)
{
	assertion_count(file, line);
	(void)file; /* UNUSED */
	(void)line; /* UNUSED */
	umask(mask);
	return (1);
}

/*
 *
 *  UTILITIES for use by tests.
 *
 */

/*
 * Check whether platform supports symlinks.  This is intended
 * for tests to use in deciding whether to bother testing symlink
 * support; if the platform doesn't support symlinks, there's no point
 * in checking whether the program being tested can create them.
 *
 * Note that the first time this test is called, we actually go out to
 * disk to create and verify a symlink.  This is necessary because
 * symlink support is actually a property of a particular filesystem
 * and can thus vary between directories on a single system.  After
 * the first call, this returns the cached result from memory, so it's
 * safe to call it as often as you wish.
 */
int
canSymlink(void)
{
	/* Remember the test result */
	static int value = 0, tested = 0;
	if (tested)
		return (value);

	++tested;
	assertion_make_file(__FILE__, __LINE__, "canSymlink.0", 0644, "a");
	/* Note: Cygwin has its own symlink() emulation that does not
	 * use the Win32 CreateSymbolicLink() function. */
#if defined(_WIN32) && !defined(__CYGWIN__)
	value = my_CreateSymbolicLinkA("canSymlink.1", "canSymlink.0", 0)
	    && is_symlink(__FILE__, __LINE__, "canSymlink.1", "canSymlink.0");
#elif HAVE_SYMLINK
	value = (0 == symlink("canSymlink.0", "canSymlink.1"))
	    && is_symlink(__FILE__, __LINE__, "canSymlink.1","canSymlink.0");
#endif
	return (value);
}

/*
 * Can this platform run the gzip program?
 */
/* Platform-dependent options for hiding the output of a subcommand. */
#if defined(_WIN32) && !defined(__CYGWIN__)
static const char *redirectArgs = ">NUL 2>NUL"; /* Win32 cmd.exe */
#else
static const char *redirectArgs = ">/dev/null 2>/dev/null"; /* POSIX 'sh' */
#endif
int
canGzip(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("gzip -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the gunzip program?
 */
int
canGunzip(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("gunzip -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Sleep as needed; useful for verifying disk timestamp changes by
 * ensuring that the wall-clock time has actually changed before we
 * go back to re-read something from disk.
 */
void
sleepUntilAfter(time_t t)
{
	while (t >= time(NULL))
#if defined(_WIN32) && !defined(__CYGWIN__)
		Sleep(500);
#else
		sleep(1);
#endif
}

/*
 * Call standard system() call, but build up the command line using
 * sprintf() conventions.
 */
int
systemf(const char *fmt, ...)
{
	char buff[8192];
	va_list ap;
	int r;

	va_start(ap, fmt);
	vsprintf(buff, fmt, ap);
	if (verbosity > VERBOSITY_FULL)
		logprintf("Cmd: %s\n", buff);
	r = system(buff);
	va_end(ap);
	return (r);
}

/*
 * Slurp a file into memory for ease of comparison and testing.
 * Returns size of file in 'sizep' if non-NULL, null-terminates
 * data in memory for ease of use.
 */
char *
slurpfile(size_t * sizep, const char *fmt, ...)
{
	char filename[8192];
	struct stat st;
	va_list ap;
	char *p;
	ssize_t bytes_read;
	FILE *f;
	int r;

	va_start(ap, fmt);
	vsprintf(filename, fmt, ap);
	va_end(ap);

	f = fopen(filename, "rb");
	if (f == NULL) {
		/* Note: No error; non-existent file is okay here. */
		return (NULL);
	}
	r = fstat(fileno(f), &st);
	if (r != 0) {
		logprintf("Can't stat file %s\n", filename);
		fclose(f);
		return (NULL);
	}
	p = malloc((size_t)st.st_size + 1);
	if (p == NULL) {
		logprintf("Can't allocate %ld bytes of memory to read file %s\n",
		    (long int)st.st_size, filename);
		fclose(f);
		return (NULL);
	}
	bytes_read = fread(p, 1, (size_t)st.st_size, f);
	if (bytes_read < st.st_size) {
		logprintf("Can't read file %s\n", filename);
		fclose(f);
		free(p);
		return (NULL);
	}
	p[st.st_size] = '\0';
	if (sizep != NULL)
		*sizep = (size_t)st.st_size;
	fclose(f);
	return (p);
}

/* Read a uuencoded file from the reference directory, decode, and
 * write the result into the current directory. */
#define	UUDECODE(c) (((c) - 0x20) & 0x3f)
void
extract_reference_file(const char *name)
{
	char buff[1024];
	FILE *in, *out;

	sprintf(buff, "%s/%s.uu", refdir, name);
	in = fopen(buff, "r");
	failure("Couldn't open reference file %s", buff);
	assert(in != NULL);
	if (in == NULL)
		return;
	/* Read up to and including the 'begin' line. */
	for (;;) {
		if (fgets(buff, sizeof(buff), in) == NULL) {
			/* TODO: This is a failure. */
			return;
		}
		if (memcmp(buff, "begin ", 6) == 0)
			break;
	}
	/* Now, decode the rest and write it. */
	/* Not a lot of error checking here; the input better be right. */
	out = fopen(name, "wb");
	while (fgets(buff, sizeof(buff), in) != NULL) {
		char *p = buff;
		int bytes;

		if (memcmp(buff, "end", 3) == 0)
			break;

		bytes = UUDECODE(*p++);
		while (bytes > 0) {
			int n = 0;
			/* Write out 1-3 bytes from that. */
			if (bytes > 0) {
				n = UUDECODE(*p++) << 18;
				n |= UUDECODE(*p++) << 12;
				fputc(n >> 16, out);
				--bytes;
			}
			if (bytes > 0) {
				n |= UUDECODE(*p++) << 6;
				fputc((n >> 8) & 0xFF, out);
				--bytes;
			}
			if (bytes > 0) {
				n |= UUDECODE(*p++);
				fputc(n & 0xFF, out);
				--bytes;
			}
		}
	}
	fclose(out);
	fclose(in);
}

/*
 *
 * TEST management
 *
 */

/*
 * "list.h" is simply created by "grep DEFINE_TEST test_*.c"; it has
 * a line like
 *      DEFINE_TEST(test_function)
 * for each test.
 */

/* Use "list.h" to declare all of the test functions. */
#undef DEFINE_TEST
#define	DEFINE_TEST(name) void name(void);
#include "list.h"

/* Use "list.h" to create a list of all tests (functions and names). */
#undef DEFINE_TEST
#define	DEFINE_TEST(n) { n, #n, 0 },
struct { void (*func)(void); const char *name; int failures; } tests[] = {
	#include "list.h"
};

/*
 * Summarize repeated failures in the just-completed test.
 */
static void
test_summarize(const char *filename, int failed)
{
	unsigned int i;

	switch (verbosity) {
	case VERBOSITY_SUMMARY_ONLY:
		printf(failed ? "E" : ".");
		fflush(stdout);
		break;
	case VERBOSITY_PASSFAIL:
		printf(failed ? "FAIL\n" : "ok\n");
		break;
	}

	log_console = (verbosity == VERBOSITY_LIGHT_REPORT);

	for (i = 0; i < sizeof(failed_lines)/sizeof(failed_lines[0]); i++) {
		if (failed_lines[i].count > 1 && !failed_lines[i].skip)
			logprintf("%s:%d: Summary: Failed %d times\n",
			    filename, i, failed_lines[i].count);
	}
	/* Clear the failure history for the next file. */
	memset(failed_lines, 0, sizeof(failed_lines));
}

/*
 * Actually run a single test, with appropriate setup and cleanup.
 */
static int
test_run(int i, const char *tmpdir)
{
	char logfilename[64];
	int failures_before = failures;
	int oldumask;

	switch (verbosity) {
	case VERBOSITY_SUMMARY_ONLY: /* No per-test reports at all */
		break;
	case VERBOSITY_PASSFAIL: /* rest of line will include ok/FAIL marker */
		printf("%3d: %-50s", i, tests[i].name);
		fflush(stdout);
		break;
	default: /* Title of test, details will follow */
		printf("%3d: %s\n", i, tests[i].name);
	}

	/* Chdir to the top-level work directory. */
	if (!assertChdir(tmpdir)) {
		fprintf(stderr,
		    "ERROR: Can't chdir to top work dir %s\n", tmpdir);
		exit(1);
	}
	/* Create a log file for this test. */
	sprintf(logfilename, "%s.log", tests[i].name);
	logfile = fopen(logfilename, "w");
	fprintf(logfile, "%s\n\n", tests[i].name);
	/* Chdir() to a work dir for this specific test. */
	if (!assertMakeDir(tests[i].name, 0755)
	    || !assertChdir(tests[i].name)) {
		fprintf(stderr,
		    "ERROR: Can't chdir to work dir %s/%s\n",
		    tmpdir, tests[i].name);
		exit(1);
	}
	/* Explicitly reset the locale before each test. */
	setlocale(LC_ALL, "C");
	/* Record the umask before we run the test. */
	umask(oldumask = umask(0));
	/*
	 * Run the actual test.
	 */
	(*tests[i].func)();
	/*
	 * Clean up and report afterwards.
	 */
	/* Restore umask */
	umask(oldumask);
	/* Reset locale. */
	setlocale(LC_ALL, "C");
	/* Reset directory. */
	if (!assertChdir(tmpdir)) {
		fprintf(stderr, "ERROR: Couldn't chdir to temp dir %s\n",
		    tmpdir);
		exit(1);
	}
	/* Report per-test summaries. */
	tests[i].failures = failures - failures_before;
	test_summarize(test_filename, tests[i].failures);
	/* Close the per-test log file. */
	fclose(logfile);
	logfile = NULL;
	/* If there were no failures, we can remove the work dir and logfile. */
	if (tests[i].failures == 0) {
		if (!keep_temp_files && assertChdir(tmpdir)) {
#if defined(_WIN32) && !defined(__CYGWIN__)
			/* Make sure not to leave empty directories.
			 * Sometimes a processing of closing files used by tests
			 * is not done, then rmdir will be failed and it will
			 * leave a empty test directory. So we should wait a few
			 * seconds and retry rmdir. */
			int r, t;
			for (t = 0; t < 10; t++) {
				if (t > 0)
					Sleep(1000);
				r = systemf("rmdir /S /Q %s", tests[i].name);
				if (r == 0)
					break;
			}
			systemf("del %s", logfilename);
#else
			systemf("rm -rf %s", tests[i].name);
			systemf("rm %s", logfilename);
#endif
		}
	}
	/* Return appropriate status. */
	return (tests[i].failures);
}

/*
 *
 *
 * MAIN and support routines.
 *
 *
 */

static void
usage(const char *program)
{
	static const int limit = sizeof(tests) / sizeof(tests[0]);
	int i;

	printf("Usage: %s [options] <test> <test> ...\n", program);
	printf("Default is to run all tests.\n");
	printf("Otherwise, specify the numbers of the tests you wish to run.\n");
	printf("Options:\n");
	printf("  -d  Dump core after any failure, for debugging.\n");
	printf("  -k  Keep all temp files.\n");
	printf("      Default: temp files for successful tests deleted.\n");
#ifdef PROGRAM
	printf("  -p <path>  Path to executable to be tested.\n");
	printf("      Default: path taken from " ENVBASE " environment variable.\n");
#endif
	printf("  -q  Quiet.\n");
	printf("  -r <dir>   Path to dir containing reference files.\n");
	printf("      Default: Current directory.\n");
	printf("  -v  Verbose.\n");
	printf("Available tests:\n");
	for (i = 0; i < limit; i++)
		printf("  %d: %s\n", i, tests[i].name);
	exit(1);
}

static char *
get_refdir(const char *d)
{
	char tried[512] = { '\0' };
	char buff[128];
	char *pwd, *p;

	/* If a dir was specified, try that */
	if (d != NULL) {
		pwd = NULL;
		snprintf(buff, sizeof(buff), "%s", d);
		p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
		if (p != NULL) goto success;
		strncat(tried, buff, sizeof(tried) - strlen(tried) - 1);
		strncat(tried, "\n", sizeof(tried) - strlen(tried) - 1);
		goto failure;
	}

	/* Get the current dir. */
	pwd = getcwd(NULL, 0);
	while (pwd[strlen(pwd) - 1] == '\n')
		pwd[strlen(pwd) - 1] = '\0';

	/* Look for a known file. */
	snprintf(buff, sizeof(buff), "%s", pwd);
	p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
	if (p != NULL) goto success;
	strncat(tried, buff, sizeof(tried) - strlen(tried) - 1);
	strncat(tried, "\n", sizeof(tried) - strlen(tried) - 1);

	snprintf(buff, sizeof(buff), "%s/test", pwd);
	p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
	if (p != NULL) goto success;
	strncat(tried, buff, sizeof(tried) - strlen(tried) - 1);
	strncat(tried, "\n", sizeof(tried) - strlen(tried) - 1);

#if defined(LIBRARY)
	snprintf(buff, sizeof(buff), "%s/%s/test", pwd, LIBRARY);
#else
	snprintf(buff, sizeof(buff), "%s/%s/test", pwd, PROGRAM);
#endif
	p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
	if (p != NULL) goto success;
	strncat(tried, buff, sizeof(tried) - strlen(tried) - 1);
	strncat(tried, "\n", sizeof(tried) - strlen(tried) - 1);

	if (memcmp(pwd, "/usr/obj", 8) == 0) {
		snprintf(buff, sizeof(buff), "%s", pwd + 8);
		p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
		if (p != NULL) goto success;
		strncat(tried, buff, sizeof(tried) - strlen(tried) - 1);
		strncat(tried, "\n", sizeof(tried) - strlen(tried) - 1);

		snprintf(buff, sizeof(buff), "%s/test", pwd + 8);
		p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
		if (p != NULL) goto success;
		strncat(tried, buff, sizeof(tried) - strlen(tried) - 1);
		strncat(tried, "\n", sizeof(tried) - strlen(tried) - 1);
	}

failure:
	printf("Unable to locate known reference file %s\n", KNOWNREF);
	printf("  Checked following directories:\n%s\n", tried);
#if defined(_WIN32) && !defined(__CYGWIN__) && defined(_DEBUG)
	DebugBreak();
#endif
	exit(1);

success:
	free(p);
	free(pwd);
	return strdup(buff);
}

int
main(int argc, char **argv)
{
	static const int limit = sizeof(tests) / sizeof(tests[0]);
	int i, tests_run = 0, tests_failed = 0, option;
	time_t now;
	char *refdir_alloc = NULL;
	const char *progname;
	const char *tmp, *option_arg, *p;
	char tmpdir[256];
	char tmpdir_timestamp[256];

	(void)argc; /* UNUSED */

#if defined(HAVE__CrtSetReportMode)
	/* To stop to run the default invalid parameter handler. */
	_set_invalid_parameter_handler(invalid_parameter_handler);
	/* Disable annoying assertion message box. */
	_CrtSetReportMode(_CRT_ASSERT, 0);
#endif

	/*
	 * Name of this program, used to build root of our temp directory
	 * tree.
	 */
	progname = p = argv[0];
	while (*p != '\0') {
		/* Support \ or / dir separators for Windows compat. */
		if (*p == '/' || *p == '\\')
			progname = p + 1;
		++p;
	}

#ifdef PROGRAM
	/* Get the target program from environment, if available. */
	testprogfile = getenv(ENVBASE);
#endif

	if (getenv("TMPDIR") != NULL)
		tmp = getenv("TMPDIR");
	else if (getenv("TMP") != NULL)
		tmp = getenv("TMP");
	else if (getenv("TEMP") != NULL)
		tmp = getenv("TEMP");
	else if (getenv("TEMPDIR") != NULL)
		tmp = getenv("TEMPDIR");
	else
		tmp = "/tmp";

	/* Allow -d to be controlled through the environment. */
	if (getenv(ENVBASE "_DEBUG") != NULL)
		dump_on_failure = 1;

	/* Get the directory holding test files from environment. */
	refdir = getenv(ENVBASE "_TEST_FILES");

	/*
	 * Parse options, without using getopt(), which isn't available
	 * on all platforms.
	 */
	++argv; /* Skip program name */
	while (*argv != NULL) {
		if (**argv != '-')
			break;
		p = *argv++;
		++p; /* Skip '-' */
		while (*p != '\0') {
			option = *p++;
			option_arg = NULL;
			/* If 'opt' takes an argument, parse that. */
			if (option == 'p' || option == 'r') {
				if (*p != '\0')
					option_arg = p;
				else if (*argv == NULL) {
					fprintf(stderr,
					    "Option -%c requires argument.\n",
					    option);
					usage(progname);
				} else
					option_arg = *argv++;
				p = ""; /* End of this option word. */
			}

			/* Now, handle the option. */
			switch (option) {
			case 'd':
				dump_on_failure = 1;
				break;
			case 'k':
				keep_temp_files = 1;
				break;
			case 'p':
#ifdef PROGRAM
				testprogfile = option_arg;
#else
				usage(progname);
#endif
				break;
			case 'q':
				verbosity--;
				break;
			case 'r':
				refdir = option_arg;
				break;
			case 'v':
				verbosity++;
				break;
			default:
				usage(progname);
			}
		}
	}

	/*
	 * Sanity-check that our options make sense.
	 */
#ifdef PROGRAM
	if (testprogfile == NULL)
		usage(progname);
	{
		char *testprg;
#if defined(_WIN32) && !defined(__CYGWIN__)
		/* Command.com sometimes rejects '/' separators. */
		testprg = strdup(testprogfile);
		for (i = 0; testprg[i] != '\0'; i++) {
			if (testprg[i] == '/')
				testprg[i] = '\\';
		}
		testprogfile = testprg;
#endif
		/* Quote the name that gets put into shell command lines. */
		testprg = malloc(strlen(testprogfile) + 3);
		strcpy(testprg, "\"");
		strcat(testprg, testprogfile);
		strcat(testprg, "\"");
		testprog = testprg;
	}
#endif

	/*
	 * Create a temp directory for the following tests.
	 * Include the time the tests started as part of the name,
	 * to make it easier to track the results of multiple tests.
	 */
	now = time(NULL);
	for (i = 0; ; i++) {
		strftime(tmpdir_timestamp, sizeof(tmpdir_timestamp),
		    "%Y-%m-%dT%H.%M.%S",
		    localtime(&now));
		sprintf(tmpdir, "%s/%s.%s-%03d", tmp, progname,
		    tmpdir_timestamp, i);
		if (assertMakeDir(tmpdir,0755))
			break;
		if (i >= 999) {
			fprintf(stderr,
			    "ERROR: Unable to create temp directory %s\n",
			    tmpdir);
			exit(1);
		}
	}

	/*
	 * If the user didn't specify a directory for locating
	 * reference files, try to find the reference files in
	 * the "usual places."
	 */
	refdir = refdir_alloc = get_refdir(refdir);

	/*
	 * Banner with basic information.
	 */
	printf("\n");
	printf("If tests fail or crash, details will be in:\n");
	printf("   %s\n", tmpdir);
	printf("\n");
	if (verbosity > VERBOSITY_SUMMARY_ONLY) {
		printf("Reference files will be read from: %s\n", refdir);
#ifdef PROGRAM
		printf("Running tests on: %s\n", testprog);
#endif
		printf("Exercising: ");
		fflush(stdout);
		printf("%s\n", EXTRA_VERSION);
	} else {
		printf("Running ");
		fflush(stdout);
	}

	/*
	 * Run some or all of the individual tests.
	 */
	if (*argv == NULL) {
		/* Default: Run all tests. */
		for (i = 0; i < limit; i++) {
			if (test_run(i, tmpdir))
				tests_failed++;
			tests_run++;
		}
	} else {
		while (*(argv) != NULL) {
			if (**argv >= '0' && **argv <= '9') {
				i = atoi(*argv);
				if (i < 0 || i >= limit) {
					printf("*** INVALID Test %s\n", *argv);
					free(refdir_alloc);
					usage(progname);
					/* usage() never returns */
				}
			} else {
				for (i = 0; i < limit; ++i) {
					if (strcmp(*argv, tests[i].name) == 0)
						break;
				}
				if (i >= limit) {
					printf("*** INVALID Test ``%s''\n",
					       *argv);
					free(refdir_alloc);
					usage(progname);
					/* usage() never returns */
				}
			}
			if (test_run(i, tmpdir))
				tests_failed++;
			tests_run++;
			argv++;
		}
	}

	/*
	 * Report summary statistics.
	 */
	if (verbosity > VERBOSITY_SUMMARY_ONLY) {
		printf("\n");
		printf("Totals:\n");
		printf("  Tests run:         %8d\n", tests_run);
		printf("  Tests failed:      %8d\n", tests_failed);
		printf("  Assertions checked:%8d\n", assertions);
		printf("  Assertions failed: %8d\n", failures);
		printf("  Skips reported:    %8d\n", skips);
	}
	if (failures) {
		printf("\n");
		printf("Failing tests:\n");
		for (i = 0; i < limit; ++i) {
			if (tests[i].failures)
				printf("  %d: %s (%d failures)\n", i,
				    tests[i].name, tests[i].failures);
		}
		printf("\n");
		printf("Details for failing tests: %s\n", tmpdir);
		printf("\n");
	} else {
		if (verbosity == VERBOSITY_SUMMARY_ONLY)
			printf("\n");
		printf("%d tests passed, no failures\n", tests_run);
	}

	free(refdir_alloc);

	/* If the final tmpdir is empty, we can remove it. */
	/* This should be the usual case when all tests succeed. */
	assertChdir("..");
	rmdir(tmpdir);

	return (tests_failed ? 1 : 0);
}
