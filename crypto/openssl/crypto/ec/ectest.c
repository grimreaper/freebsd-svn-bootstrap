/* crypto/ec/ectest.c */
/* ====================================================================
 * Copyright (c) 1998-2001 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <stdio.h>
#include <stdlib.h>
#ifdef FLAT_INC
#include "e_os.h"
#else
#include "../e_os.h"
#endif
#include <string.h>
#include <time.h>


#ifdef OPENSSL_NO_EC
int main(int argc, char * argv[]) { puts("Elliptic curves are disabled."); return 0; }
#else


#include <openssl/ec.h>
#include <openssl/engine.h>
#include <openssl/err.h>

#define ABORT do { \
	fflush(stdout); \
	fprintf(stderr, "%s:%d: ABORT\n", __FILE__, __LINE__); \
	ERR_print_errors_fp(stderr); \
	EXIT(1); \
} while (0)

#if 0
static void timings(EC_GROUP *group, int multi, BN_CTX *ctx)
	{
	clock_t clck;
	int i, j;
	BIGNUM *s, *s0;
	EC_POINT *P;
		
	s = BN_new();
	s0 = BN_new();
	if (s == NULL || s0 == NULL) ABORT;

	if (!EC_GROUP_get_curve_GFp(group, s, NULL, NULL, ctx)) ABORT;
	fprintf(stdout, "Timings for %d bit prime, ", (int)BN_num_bits(s));
	if (!EC_GROUP_get_order(group, s, ctx)) ABORT;
	fprintf(stdout, "%d bit scalars ", (int)BN_num_bits(s));
	fflush(stdout);

	P = EC_POINT_new(group);
	if (P == NULL) ABORT;
	EC_POINT_copy(P, EC_GROUP_get0_generator(group));

	clck = clock();
	for (i = 0; i < 10; i++)
		{
		if (!BN_pseudo_rand(s, BN_num_bits(s), 0, 0)) ABORT;
		if (multi)
			{
			if (!BN_pseudo_rand(s0, BN_num_bits(s), 0, 0)) ABORT;
			}
		for (j = 0; j < 10; j++)
			{
			if (!EC_POINT_mul(group, P, s, multi ? P : NULL, multi ? s0 : NULL, ctx)) ABORT;
			}
		fprintf(stdout, ".");
		fflush(stdout);
		}
	fprintf(stdout, "\n");
	
	clck = clock() - clck;

#ifdef CLOCKS_PER_SEC
	/* "To determine the time in seconds, the value returned
	 * by the clock function should be divided by the value
	 * of the macro CLOCKS_PER_SEC."
	 *                                       -- ISO/IEC 9899 */
#	define UNIT "s"
#else
	/* "`CLOCKS_PER_SEC' undeclared (first use this function)"
	 *                            -- cc on NeXTstep/OpenStep */
#	define UNIT "units"
#	define CLOCKS_PER_SEC 1
#endif

	fprintf(stdout, "%i %s in %.2f " UNIT "\n", i*j,
		multi ? "s*P+t*Q operations" : "point multiplications",
		(double)clck/CLOCKS_PER_SEC);
	fprintf(stdout, "average: %.4f " UNIT "\n", (double)clck/(CLOCKS_PER_SEC*i*j));

	EC_POINT_free(P);
	BN_free(s);
	BN_free(s0);
	}
#endif

int main(int argc, char *argv[])
	{	
	BN_CTX *ctx = NULL;
	BIGNUM *p, *a, *b;
	EC_GROUP *group;
	EC_GROUP *P_192 = NULL, *P_224 = NULL, *P_256 = NULL, *P_384 = NULL, *P_521 = NULL;
	EC_POINT *P, *Q, *R;
	BIGNUM *x, *y, *z;
	unsigned char buf[100];
	size_t i, len;
	int k;
	
	/* enable memory leak checking unless explicitly disabled */
	if (!((getenv("OPENSSL_DEBUG_MEMORY") != NULL) && (0 == strcmp(getenv("OPENSSL_DEBUG_MEMORY"), "off"))))
		{
		CRYPTO_malloc_debug_init();
		CRYPTO_set_mem_debug_options(V_CRYPTO_MDEBUG_ALL);
		}
	else
		{
		/* OPENSSL_DEBUG_MEMORY=off */
		CRYPTO_set_mem_debug_functions(0, 0, 0, 0, 0);
		}
	CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);
	ERR_load_crypto_strings();

#if 1 /* optional */
	ctx = BN_CTX_new();
	if (!ctx) ABORT;
#endif

	p = BN_new();
	a = BN_new();
	b = BN_new();
	if (!p || !a || !b) ABORT;

	if (!BN_hex2bn(&p, "17")) ABORT;
	if (!BN_hex2bn(&a, "1")) ABORT;
	if (!BN_hex2bn(&b, "1")) ABORT;
	
	group = EC_GROUP_new(EC_GFp_mont_method()); /* applications should use EC_GROUP_new_curve_GFp
	                                             * so that the library gets to choose the EC_METHOD */
	if (!group) ABORT;

	if (!EC_GROUP_set_curve_GFp(group, p, a, b, ctx)) ABORT;

	{
		EC_GROUP *tmp;
		tmp = EC_GROUP_new(EC_GROUP_method_of(group));
		if (!tmp) ABORT;
		if (!EC_GROUP_copy(tmp, group));
		EC_GROUP_free(group);
		group = tmp;
	}
	
	if (!EC_GROUP_get_curve_GFp(group, p, a, b, ctx)) ABORT;

	fprintf(stdout, "Curve defined by Weierstrass equation\n     y^2 = x^3 + a*x + b  (mod 0x");
	BN_print_fp(stdout, p);
	fprintf(stdout, ")\n     a = 0x");
	BN_print_fp(stdout, a);
	fprintf(stdout, "\n     b = 0x");
	BN_print_fp(stdout, b);
	fprintf(stdout, "\n");

	P = EC_POINT_new(group);
	Q = EC_POINT_new(group);
	R = EC_POINT_new(group);
	if (!P || !Q || !R) ABORT;
	
	if (!EC_POINT_set_to_infinity(group, P)) ABORT;
	if (!EC_POINT_is_at_infinity(group, P)) ABORT;

	buf[0] = 0;
	if (!EC_POINT_oct2point(group, Q, buf, 1, ctx)) ABORT;

	if (!EC_POINT_add(group, P, P, Q, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, P)) ABORT;

	x = BN_new();
	y = BN_new();
	z = BN_new();
	if (!x || !y || !z) ABORT;

	if (!BN_hex2bn(&x, "D")) ABORT;
	if (!EC_POINT_set_compressed_coordinates_GFp(group, Q, x, 1, ctx)) ABORT;
	if (!EC_POINT_is_on_curve(group, Q, ctx))
		{
		if (!EC_POINT_get_affine_coordinates_GFp(group, Q, x, y, ctx)) ABORT;
		fprintf(stderr, "Point is not on curve: x = 0x");
		BN_print_fp(stderr, x);
		fprintf(stderr, ", y = 0x");
		BN_print_fp(stderr, y);
		fprintf(stderr, "\n");
		ABORT;
		}

	fprintf(stdout, "A cyclic subgroup:\n");
	k = 100;
	do
		{
		if (k-- == 0) ABORT;

		if (EC_POINT_is_at_infinity(group, P))
			fprintf(stdout, "     point at infinity\n");
		else
			{
			if (!EC_POINT_get_affine_coordinates_GFp(group, P, x, y, ctx)) ABORT;

			fprintf(stdout, "     x = 0x");
			BN_print_fp(stdout, x);
			fprintf(stdout, ", y = 0x");
			BN_print_fp(stdout, y);
			fprintf(stdout, "\n");
			}
		
		if (!EC_POINT_copy(R, P)) ABORT;
		if (!EC_POINT_add(group, P, P, Q, ctx)) ABORT;

#if 0 /* optional */
		{
			EC_POINT *points[3];
		
			points[0] = R;
			points[1] = Q;
			points[2] = P;
			if (!EC_POINTs_make_affine(group, 2, points, ctx)) ABORT;
		}
#endif

		}
	while (!EC_POINT_is_at_infinity(group, P));

	if (!EC_POINT_add(group, P, Q, R, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, P)) ABORT;

	len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_COMPRESSED, buf, sizeof buf, ctx);
	if (len == 0) ABORT;
	if (!EC_POINT_oct2point(group, P, buf, len, ctx)) ABORT;
	if (0 != EC_POINT_cmp(group, P, Q, ctx)) ABORT;
	fprintf(stdout, "Generator as octect string, compressed form:\n     ");
	for (i = 0; i < len; i++) fprintf(stdout, "%02X", buf[i]);
	
	len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_UNCOMPRESSED, buf, sizeof buf, ctx);
	if (len == 0) ABORT;
	if (!EC_POINT_oct2point(group, P, buf, len, ctx)) ABORT;
	if (0 != EC_POINT_cmp(group, P, Q, ctx)) ABORT;
	fprintf(stdout, "\nGenerator as octect string, uncompressed form:\n     ");
	for (i = 0; i < len; i++) fprintf(stdout, "%02X", buf[i]);
	
	len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_HYBRID, buf, sizeof buf, ctx);
	if (len == 0) ABORT;
	if (!EC_POINT_oct2point(group, P, buf, len, ctx)) ABORT;
	if (0 != EC_POINT_cmp(group, P, Q, ctx)) ABORT;
	fprintf(stdout, "\nGenerator as octect string, hybrid form:\n     ");
	for (i = 0; i < len; i++) fprintf(stdout, "%02X", buf[i]);
	
	if (!EC_POINT_get_Jprojective_coordinates_GFp(group, R, x, y, z, ctx)) ABORT;
	fprintf(stdout, "\nA representation of the inverse of that generator in\nJacobian projective coordinates:\n     X = 0x");
	BN_print_fp(stdout, x);
	fprintf(stdout, ", Y = 0x");
	BN_print_fp(stdout, y);
	fprintf(stdout, ", Z = 0x");
	BN_print_fp(stdout, z);
	fprintf(stdout, "\n");

	if (!EC_POINT_invert(group, P, ctx)) ABORT;
	if (0 != EC_POINT_cmp(group, P, R, ctx)) ABORT;


	/* Curve P-192 (FIPS PUB 186-2, App. 6) */
	
	if (!BN_hex2bn(&p, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFFFFFFFFFF")) ABORT;
	if (1 != BN_is_prime(p, BN_prime_checks, 0, ctx, NULL)) ABORT;
	if (!BN_hex2bn(&a, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFFFFFFFFFC")) ABORT;
	if (!BN_hex2bn(&b, "64210519E59C80E70FA7E9AB72243049FEB8DEECC146B9B1")) ABORT;
	if (!EC_GROUP_set_curve_GFp(group, p, a, b, ctx)) ABORT;

	if (!BN_hex2bn(&x, "188DA80EB03090F67CBF20EB43A18800F4FF0AFD82FF1012")) ABORT;
	if (!EC_POINT_set_compressed_coordinates_GFp(group, P, x, 1, ctx)) ABORT;
	if (!EC_POINT_is_on_curve(group, P, ctx)) ABORT;
	if (!BN_hex2bn(&z, "FFFFFFFFFFFFFFFFFFFFFFFF99DEF836146BC9B1B4D22831")) ABORT;
	if (!EC_GROUP_set_generator(group, P, z, BN_value_one())) ABORT;

	if (!EC_POINT_get_affine_coordinates_GFp(group, P, x, y, ctx)) ABORT;
	fprintf(stdout, "\nNIST curve P-192 -- Generator:\n     x = 0x");
	BN_print_fp(stdout, x);
	fprintf(stdout, "\n     y = 0x");
	BN_print_fp(stdout, y);
	fprintf(stdout, "\n");
	/* G_y value taken from the standard: */
	if (!BN_hex2bn(&z, "07192B95FFC8DA78631011ED6B24CDD573F977A11E794811")) ABORT;
	if (0 != BN_cmp(y, z)) ABORT;
	
	fprintf(stdout, "verify group order ...");
	fflush(stdout);
	if (!EC_GROUP_get_order(group, z, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, ".");
	fflush(stdout);
	if (!EC_GROUP_precompute_mult(group, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, " ok\n");

	if (!(P_192 = EC_GROUP_new(EC_GROUP_method_of(group)))) ABORT;
	if (!EC_GROUP_copy(P_192, group)) ABORT;


	/* Curve P-224 (FIPS PUB 186-2, App. 6) */
	
	if (!BN_hex2bn(&p, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF000000000000000000000001")) ABORT;
	if (1 != BN_is_prime(p, BN_prime_checks, 0, ctx, NULL)) ABORT;
	if (!BN_hex2bn(&a, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFE")) ABORT;
	if (!BN_hex2bn(&b, "B4050A850C04B3ABF54132565044B0B7D7BFD8BA270B39432355FFB4")) ABORT;
	if (!EC_GROUP_set_curve_GFp(group, p, a, b, ctx)) ABORT;

	if (!BN_hex2bn(&x, "B70E0CBD6BB4BF7F321390B94A03C1D356C21122343280D6115C1D21")) ABORT;
	if (!EC_POINT_set_compressed_coordinates_GFp(group, P, x, 0, ctx)) ABORT;
	if (!EC_POINT_is_on_curve(group, P, ctx)) ABORT;
	if (!BN_hex2bn(&z, "FFFFFFFFFFFFFFFFFFFFFFFFFFFF16A2E0B8F03E13DD29455C5C2A3D")) ABORT;
	if (!EC_GROUP_set_generator(group, P, z, BN_value_one())) ABORT;

	if (!EC_POINT_get_affine_coordinates_GFp(group, P, x, y, ctx)) ABORT;
	fprintf(stdout, "\nNIST curve P-224 -- Generator:\n     x = 0x");
	BN_print_fp(stdout, x);
	fprintf(stdout, "\n     y = 0x");
	BN_print_fp(stdout, y);
	fprintf(stdout, "\n");
	/* G_y value taken from the standard: */
	if (!BN_hex2bn(&z, "BD376388B5F723FB4C22DFE6CD4375A05A07476444D5819985007E34")) ABORT;
	if (0 != BN_cmp(y, z)) ABORT;
	
	fprintf(stdout, "verify group order ...");
	fflush(stdout);
	if (!EC_GROUP_get_order(group, z, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, ".");
	fflush(stdout);
	if (!EC_GROUP_precompute_mult(group, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, " ok\n");

	if (!(P_224 = EC_GROUP_new(EC_GROUP_method_of(group)))) ABORT;
	if (!EC_GROUP_copy(P_224, group)) ABORT;


	/* Curve P-256 (FIPS PUB 186-2, App. 6) */
	
	if (!BN_hex2bn(&p, "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF")) ABORT;
	if (1 != BN_is_prime(p, BN_prime_checks, 0, ctx, NULL)) ABORT;
	if (!BN_hex2bn(&a, "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC")) ABORT;
	if (!BN_hex2bn(&b, "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B")) ABORT;
	if (!EC_GROUP_set_curve_GFp(group, p, a, b, ctx)) ABORT;

	if (!BN_hex2bn(&x, "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296")) ABORT;
	if (!EC_POINT_set_compressed_coordinates_GFp(group, P, x, 1, ctx)) ABORT;
	if (!EC_POINT_is_on_curve(group, P, ctx)) ABORT;
	if (!BN_hex2bn(&z, "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E"
		"84F3B9CAC2FC632551")) ABORT;
	if (!EC_GROUP_set_generator(group, P, z, BN_value_one())) ABORT;

	if (!EC_POINT_get_affine_coordinates_GFp(group, P, x, y, ctx)) ABORT;
	fprintf(stdout, "\nNIST curve P-256 -- Generator:\n     x = 0x");
	BN_print_fp(stdout, x);
	fprintf(stdout, "\n     y = 0x");
	BN_print_fp(stdout, y);
	fprintf(stdout, "\n");
	/* G_y value taken from the standard: */
	if (!BN_hex2bn(&z, "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5")) ABORT;
	if (0 != BN_cmp(y, z)) ABORT;
	
	fprintf(stdout, "verify group order ...");
	fflush(stdout);
	if (!EC_GROUP_get_order(group, z, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, ".");
	fflush(stdout);
	if (!EC_GROUP_precompute_mult(group, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, " ok\n");

	if (!(P_256 = EC_GROUP_new(EC_GROUP_method_of(group)))) ABORT;
	if (!EC_GROUP_copy(P_256, group)) ABORT;


	/* Curve P-384 (FIPS PUB 186-2, App. 6) */
	
	if (!BN_hex2bn(&p, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF")) ABORT;
	if (1 != BN_is_prime(p, BN_prime_checks, 0, ctx, NULL)) ABORT;
	if (!BN_hex2bn(&a, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFC")) ABORT;
	if (!BN_hex2bn(&b, "B3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141"
		"120314088F5013875AC656398D8A2ED19D2A85C8EDD3EC2AEF")) ABORT;
	if (!EC_GROUP_set_curve_GFp(group, p, a, b, ctx)) ABORT;

	if (!BN_hex2bn(&x, "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B"
		"9859F741E082542A385502F25DBF55296C3A545E3872760AB7")) ABORT;
	if (!EC_POINT_set_compressed_coordinates_GFp(group, P, x, 1, ctx)) ABORT;
	if (!EC_POINT_is_on_curve(group, P, ctx)) ABORT;
	if (!BN_hex2bn(&z, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973")) ABORT;
	if (!EC_GROUP_set_generator(group, P, z, BN_value_one())) ABORT;

	if (!EC_POINT_get_affine_coordinates_GFp(group, P, x, y, ctx)) ABORT;
	fprintf(stdout, "\nNIST curve P-384 -- Generator:\n     x = 0x");
	BN_print_fp(stdout, x);
	fprintf(stdout, "\n     y = 0x");
	BN_print_fp(stdout, y);
	fprintf(stdout, "\n");
	/* G_y value taken from the standard: */
	if (!BN_hex2bn(&z, "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A14"
		"7CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F")) ABORT;
	if (0 != BN_cmp(y, z)) ABORT;
	
	fprintf(stdout, "verify group order ...");
	fflush(stdout);
	if (!EC_GROUP_get_order(group, z, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, ".");
	fflush(stdout);
	if (!EC_GROUP_precompute_mult(group, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, " ok\n");

	if (!(P_384 = EC_GROUP_new(EC_GROUP_method_of(group)))) ABORT;
	if (!EC_GROUP_copy(P_384, group)) ABORT;


	/* Curve P-521 (FIPS PUB 186-2, App. 6) */
	
	if (!BN_hex2bn(&p, "1FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFF")) ABORT;
	if (1 != BN_is_prime(p, BN_prime_checks, 0, ctx, NULL)) ABORT;
	if (!BN_hex2bn(&a, "1FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFC")) ABORT;
	if (!BN_hex2bn(&b, "051953EB9618E1C9A1F929A21A0B68540EEA2DA725B99B"
		"315F3B8B489918EF109E156193951EC7E937B1652C0BD3BB1BF073573"
		"DF883D2C34F1EF451FD46B503F00")) ABORT;
	if (!EC_GROUP_set_curve_GFp(group, p, a, b, ctx)) ABORT;

	if (!BN_hex2bn(&x, "C6858E06B70404E9CD9E3ECB662395B4429C648139053F"
		"B521F828AF606B4D3DBAA14B5E77EFE75928FE1DC127A2FFA8DE3348B"
		"3C1856A429BF97E7E31C2E5BD66")) ABORT;
	if (!EC_POINT_set_compressed_coordinates_GFp(group, P, x, 0, ctx)) ABORT;
	if (!EC_POINT_is_on_curve(group, P, ctx)) ABORT;
	if (!BN_hex2bn(&z, "1FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
		"FFFFFFFFFFFFFFFFFFFFA51868783BF2F966B7FCC0148F709A5D03BB5"
		"C9B8899C47AEBB6FB71E91386409")) ABORT;
	if (!EC_GROUP_set_generator(group, P, z, BN_value_one())) ABORT;

	if (!EC_POINT_get_affine_coordinates_GFp(group, P, x, y, ctx)) ABORT;
	fprintf(stdout, "\nNIST curve P-521 -- Generator:\n     x = 0x");
	BN_print_fp(stdout, x);
	fprintf(stdout, "\n     y = 0x");
	BN_print_fp(stdout, y);
	fprintf(stdout, "\n");
	/* G_y value taken from the standard: */
	if (!BN_hex2bn(&z, "11839296A789A3BC0045C8A5FB42C7D1BD998F54449579"
		"B446817AFBD17273E662C97EE72995EF42640C550B9013FAD0761353C"
		"7086A272C24088BE94769FD16650")) ABORT;
	if (0 != BN_cmp(y, z)) ABORT;
	
	fprintf(stdout, "verify group order ...");
	fflush(stdout);
	if (!EC_GROUP_get_order(group, z, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, ".");
	fflush(stdout);
	if (!EC_GROUP_precompute_mult(group, ctx)) ABORT;
	if (!EC_POINT_mul(group, Q, z, NULL, NULL, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, Q)) ABORT;
	fprintf(stdout, " ok\n");

	if (!(P_521 = EC_GROUP_new(EC_GROUP_method_of(group)))) ABORT;
	if (!EC_GROUP_copy(P_521, group)) ABORT;


	/* more tests using the last curve */

	if (!EC_POINT_copy(Q, P)) ABORT;
	if (EC_POINT_is_at_infinity(group, Q)) ABORT;
	if (!EC_POINT_dbl(group, P, P, ctx)) ABORT;
	if (!EC_POINT_is_on_curve(group, P, ctx)) ABORT;
	if (!EC_POINT_invert(group, Q, ctx)) ABORT; /* P = -2Q */

	if (!EC_POINT_add(group, R, P, Q, ctx)) ABORT;
	if (!EC_POINT_add(group, R, R, Q, ctx)) ABORT;
	if (!EC_POINT_is_at_infinity(group, R)) ABORT; /* R = P + 2Q */

	{
		const EC_POINT *points[3];
		const BIGNUM *scalars[3];
	
		if (EC_POINT_is_at_infinity(group, Q)) ABORT;
		points[0] = Q;
		points[1] = Q;
		points[2] = Q;

		if (!BN_add(y, z, BN_value_one())) ABORT;
		if (BN_is_odd(y)) ABORT;
		if (!BN_rshift1(y, y)) ABORT;
		scalars[0] = y; /* (group order + 1)/2,  so  y*Q + y*Q = Q */
		scalars[1] = y;

		fprintf(stdout, "combined multiplication ...");
		fflush(stdout);

		/* z is still the group order */
		if (!EC_POINTs_mul(group, P, NULL, 2, points, scalars, ctx)) ABORT;
		if (!EC_POINTs_mul(group, R, z, 2, points, scalars, ctx)) ABORT;
		if (0 != EC_POINT_cmp(group, P, R, ctx)) ABORT;
		if (0 != EC_POINT_cmp(group, R, Q, ctx)) ABORT;

		fprintf(stdout, ".");
		fflush(stdout);

		if (!BN_pseudo_rand(y, BN_num_bits(y), 0, 0)) ABORT;
		if (!BN_add(z, z, y)) ABORT;
		z->neg = 1;
		scalars[0] = y;
		scalars[1] = z; /* z = -(order + y) */

		if (!EC_POINTs_mul(group, P, NULL, 2, points, scalars, ctx)) ABORT;
		if (!EC_POINT_is_at_infinity(group, P)) ABORT;

		fprintf(stdout, ".");
		fflush(stdout);

		if (!BN_pseudo_rand(x, BN_num_bits(y) - 1, 0, 0)) ABORT;
		if (!BN_add(z, x, y)) ABORT;
		z->neg = 1;
		scalars[0] = x;
		scalars[1] = y;
		scalars[2] = z; /* z = -(x+y) */

		if (!EC_POINTs_mul(group, P, NULL, 3, points, scalars, ctx)) ABORT;
		if (!EC_POINT_is_at_infinity(group, P)) ABORT;

		fprintf(stdout, " ok\n\n");
	}


#if 0
	timings(P_192, 0, ctx);
	timings(P_192, 1, ctx);
	timings(P_224, 0, ctx);
	timings(P_224, 1, ctx);
	timings(P_256, 0, ctx);
	timings(P_256, 1, ctx);
	timings(P_384, 0, ctx);
	timings(P_384, 1, ctx);
	timings(P_521, 0, ctx);
	timings(P_521, 1, ctx);
#endif


	if (ctx)
		BN_CTX_free(ctx);
	BN_free(p); BN_free(a);	BN_free(b);
	EC_GROUP_free(group);
	EC_POINT_free(P);
	EC_POINT_free(Q);
	EC_POINT_free(R);
	BN_free(x); BN_free(y); BN_free(z);

	if (P_192) EC_GROUP_free(P_192);
	if (P_224) EC_GROUP_free(P_224);
	if (P_256) EC_GROUP_free(P_256);
	if (P_384) EC_GROUP_free(P_384);
	if (P_521) EC_GROUP_free(P_521);

	ENGINE_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ERR_free_strings();
	ERR_remove_state(0);
	CRYPTO_mem_leaks_fp(stderr);
	
	return 0;
	}
#endif
