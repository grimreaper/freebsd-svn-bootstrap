/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Ariff Abdullah <ariff@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SND_G711_H_
#define _SND_G711_H_

#define G711_TABLE_SIZE		256

#define ULAW_TO_U8	{						\
	    3,    7,   11,   15,   19,   23,   27,   31,		\
	   35,   39,   43,   47,   51,   55,   59,   63,		\
	   66,   68,   70,   72,   74,   76,   78,   80,		\
	   82,   84,   86,   88,   90,   92,   94,   96,		\
	   98,   99,  100,  101,  102,  103,  104,  105,		\
	  106,  107,  108,  109,  110,  111,  112,  113,		\
	  113,  114,  114,  115,  115,  116,  116,  117,		\
	  117,  118,  118,  119,  119,  120,  120,  121,		\
	  121,  121,  122,  122,  122,  122,  123,  123,		\
	  123,  123,  124,  124,  124,  124,  125,  125,		\
	  125,  125,  125,  125,  126,  126,  126,  126,		\
	  126,  126,  126,  126,  127,  127,  127,  127,		\
	  127,  127,  127,  127,  127,  127,  127,  127,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  253,  249,  245,  241,  237,  233,  229,  225,		\
	  221,  217,  213,  209,  205,  201,  197,  193,		\
	  190,  188,  186,  184,  182,  180,  178,  176,		\
	  174,  172,  170,  168,  166,  164,  162,  160,		\
	  158,  157,  156,  155,  154,  153,  152,  151,		\
	  150,  149,  148,  147,  146,  145,  144,  143,		\
	  143,  142,  142,  141,  141,  140,  140,  139,		\
	  139,  138,  138,  137,  137,  136,  136,  135,		\
	  135,  135,  134,  134,  134,  134,  133,  133,		\
	  133,  133,  132,  132,  132,  132,  131,  131,		\
	  131,  131,  131,  131,  130,  130,  130,  130,		\
	  130,  130,  130,  130,  129,  129,  129,  129,		\
	  129,  129,  129,  129,  129,  129,  129,  129,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	}

#define ALAW_TO_U8	{						\
	  108,  109,  106,  107,  112,  113,  110,  111,		\
	  100,  101,   98,   99,  104,  105,  102,  103,		\
	  118,  118,  117,  117,  120,  120,  119,  119,		\
	  114,  114,  113,  113,  116,  116,  115,  115,		\
	   43,   47,   35,   39,   59,   63,   51,   55,		\
	   11,   15,    3,    7,   27,   31,   19,   23,		\
	   86,   88,   82,   84,   94,   96,   90,   92,		\
	   70,   72,   66,   68,   78,   80,   74,   76,		\
	  127,  127,  127,  127,  127,  127,  127,  127,		\
	  127,  127,  127,  127,  127,  127,  127,  127,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  123,  123,  123,  123,  124,  124,  124,  124,		\
	  121,  121,  121,  121,  122,  122,  122,  122,		\
	  126,  126,  126,  126,  126,  126,  126,  126,		\
	  125,  125,  125,  125,  125,  125,  125,  125,		\
	  148,  147,  150,  149,  144,  143,  146,  145,		\
	  156,  155,  158,  157,  152,  151,  154,  153,		\
	  138,  138,  139,  139,  136,  136,  137,  137,		\
	  142,  142,  143,  143,  140,  140,  141,  141,		\
	  213,  209,  221,  217,  197,  193,  205,  201,		\
	  245,  241,  253,  249,  229,  225,  237,  233,		\
	  170,  168,  174,  172,  162,  160,  166,  164,		\
	  186,  184,  190,  188,  178,  176,  182,  180,		\
	  129,  129,  129,  129,  129,  129,  129,  129,		\
	  129,  129,  129,  129,  129,  129,  129,  129,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  128,  128,  128,  128,  128,  128,  128,  128,		\
	  133,  133,  133,  133,  132,  132,  132,  132,		\
	  135,  135,  135,  135,  134,  134,  134,  134,		\
	  130,  130,  130,  130,  130,  130,  130,  130,		\
	  131,  131,  131,  131,  131,  131,  131,  131,		\
	}

#define U8_TO_ULAW	{						\
	     0,    0,    0,    0,    0,    1,    1,    1,		\
	     1,    2,    2,    2,    2,    3,    3,    3,		\
	     3,    4,    4,    4,    4,    5,    5,    5,		\
	     5,    6,    6,    6,    6,    7,    7,    7,		\
	     7,    8,    8,    8,    8,    9,    9,    9,		\
	     9,   10,   10,   10,   10,   11,   11,   11,		\
	    11,   12,   12,   12,   12,   13,   13,   13,		\
	    13,   14,   14,   14,   14,   15,   15,   15,		\
	    15,   16,   16,   17,   17,   18,   18,   19,		\
	    19,   20,   20,   21,   21,   22,   22,   23,		\
	    23,   24,   24,   25,   25,   26,   26,   27,		\
	    27,   28,   28,   29,   29,   30,   30,   31,		\
	    31,   32,   33,   34,   35,   36,   37,   38,		\
	    39,   40,   41,   42,   43,   44,   45,   46,		\
	    47,   49,   51,   53,   55,   57,   59,   61,		\
	    63,   66,   70,   74,   78,   84,   92,  104,		\
	   254,  231,  219,  211,  205,  201,  197,  193,		\
	   190,  188,  186,  184,  182,  180,  178,  176,		\
	   175,  174,  173,  172,  171,  170,  169,  168,		\
	   167,  166,  165,  164,  163,  162,  161,  160,		\
	   159,  159,  158,  158,  157,  157,  156,  156,		\
	   155,  155,  154,  154,  153,  153,  152,  152,		\
	   151,  151,  150,  150,  149,  149,  148,  148,		\
	   147,  147,  146,  146,  145,  145,  144,  144,		\
	   143,  143,  143,  143,  142,  142,  142,  142,		\
	   141,  141,  141,  141,  140,  140,  140,  140,		\
	   139,  139,  139,  139,  138,  138,  138,  138,		\
	   137,  137,  137,  137,  136,  136,  136,  136,		\
	   135,  135,  135,  135,  134,  134,  134,  134,		\
	   133,  133,  133,  133,  132,  132,  132,  132,		\
	   131,  131,  131,  131,  130,  130,  130,  130,		\
	   129,  129,  129,  129,  128,  128,  128,  128,		\
	}

#define U8_TO_ALAW	{						\
	   42,   42,   42,   42,   42,   43,   43,   43,		\
	   43,   40,   40,   40,   40,   41,   41,   41,		\
	   41,   46,   46,   46,   46,   47,   47,   47,		\
	   47,   44,   44,   44,   44,   45,   45,   45,		\
	   45,   34,   34,   34,   34,   35,   35,   35,		\
	   35,   32,   32,   32,   32,   33,   33,   33,		\
	   33,   38,   38,   38,   38,   39,   39,   39,		\
	   39,   36,   36,   36,   36,   37,   37,   37,		\
	   37,   58,   58,   59,   59,   56,   56,   57,		\
	   57,   62,   62,   63,   63,   60,   60,   61,		\
	   61,   50,   50,   51,   51,   48,   48,   49,		\
	   49,   54,   54,   55,   55,   52,   52,   53,		\
	   53,   10,   11,    8,    9,   14,   15,   12,		\
	   13,    2,    3,    0,    1,    6,    7,    4,		\
	    5,   24,   30,   28,   18,   16,   22,   20,		\
	  106,  110,   98,  102,  122,  114,   75,   90,		\
	  213,  197,  245,  253,  229,  225,  237,  233,		\
	  149,  151,  145,  147,  157,  159,  153,  155,		\
	  133,  132,  135,  134,  129,  128,  131,  130,		\
	  141,  140,  143,  142,  137,  136,  139,  138,		\
	  181,  181,  180,  180,  183,  183,  182,  182,		\
	  177,  177,  176,  176,  179,  179,  178,  178,		\
	  189,  189,  188,  188,  191,  191,  190,  190,		\
	  185,  185,  184,  184,  187,  187,  186,  186,		\
	  165,  165,  165,  165,  164,  164,  164,  164,		\
	  167,  167,  167,  167,  166,  166,  166,  166,		\
	  161,  161,  161,  161,  160,  160,  160,  160,		\
	  163,  163,  163,  163,  162,  162,  162,  162,		\
	  173,  173,  173,  173,  172,  172,  172,  172,		\
	  175,  175,  175,  175,  174,  174,  174,  174,		\
	  169,  169,  169,  169,  168,  168,  168,  168,		\
	  171,  171,  171,  171,  170,  170,  170,  170,		\
	}


#define _G711_TO_INTPCM(t, v)	((intpcm_t)				\
				 ((int8_t)((t)[(uint8_t)(v)] ^ 0x80)))

#define _INTPCM_TO_G711(t, v)	((t)[(uint8_t)((v) ^ 0x80)])


#define G711_DECLARE_TABLE(t)						\
static const struct {							\
	const uint8_t ulaw_to_u8[G711_TABLE_SIZE];			\
	const uint8_t alaw_to_u8[G711_TABLE_SIZE];			\
	const uint8_t u8_to_ulaw[G711_TABLE_SIZE];			\
	const uint8_t u8_to_alaw[G711_TABLE_SIZE];			\
} t = {									\
	ULAW_TO_U8, ALAW_TO_U8,						\
	U8_TO_ULAW, U8_TO_ALAW						\
}

#define G711_DECLARE_OP(t)						\
static __inline intpcm_t						\
pcm_read_ulaw(uint8_t v)						\
{									\
									\
	return (_G711_TO_INTPCM((t).ulaw_to_u8, v));			\
}									\
									\
static __inline intpcm_t						\
pcm_read_alaw(uint8_t v)						\
{									\
									\
	return (_G711_TO_INTPCM((t).alaw_to_u8, v));			\
}									\
									\
static __inline void							\
pcm_write_ulaw(uint8_t *dst, intpcm_t v)				\
{									\
									\
	*dst = _INTPCM_TO_G711((t).u8_to_ulaw, v);			\
}									\
									\
static __inline void							\
pcm_write_alaw(uint8_t *dst, intpcm_t v)				\
{									\
									\
	*dst = _INTPCM_TO_G711((t).u8_to_alaw, v);			\
}

#define G711_DECLARE(t)							\
	G711_DECLARE_TABLE(t);						\
	G711_DECLARE_OP(t)

#endif	/* !_SND_G711_H_ */
