typedef union {
	char	 *s_value;
	char	  c_value;
	int	  i_value;
	arg_list *a_value;
       } YYSTYPE;
#define	NEWLINE	258
#define	AND	259
#define	OR	260
#define	NOT	261
#define	STRING	262
#define	NAME	263
#define	NUMBER	264
#define	ASSIGN_OP	265
#define	REL_OP	266
#define	INCR_DECR	267
#define	Define	268
#define	Break	269
#define	Quit	270
#define	Length	271
#define	Return	272
#define	For	273
#define	If	274
#define	While	275
#define	Sqrt	276
#define	Else	277
#define	Scale	278
#define	Ibase	279
#define	Obase	280
#define	Auto	281
#define	Read	282
#define	Warranty	283
#define	Halt	284
#define	Last	285
#define	Continue	286
#define	Print	287
#define	Limits	288
#define	UNARY_MINUS	289
#define	History	290


extern YYSTYPE yylval;
