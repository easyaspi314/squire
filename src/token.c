#include "token.h"
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include "shared.h"
#include "roman.h"

const char *sq_stream;
static char put_back_quote;

static struct sq_token next_macro_token(void);
static void parse_macro_statement(char *);
static bool parse_macro_identifier(char *);

static unsigned fraktur_length(const char *stream, unsigned *index) {
	// Yes, it's terrible. On this day I learned that UTF-8 played nicely with
	// strcmp. But I already wrote all this out and it works, so .. ¯\_(ツ)_/¯
	const uint32_t FRAKTUR[26 * 2] = {
		/*  A 𝔄  */  /*  B 𝔅  */ /*  C ℭ  */  /*  D 𝔇  */  /*  E 𝔈  */
		0x0F09D9484, 0x0F09D9485, 0x000E284AD, 0x0F09D9487, 0x0F09D9488,
		/*  F 𝔉  */  /*  G 𝔊  */ /*  H ℌ  */  /*  I ℑ  */  /*  J 𝔍  */
		0x0F09D9489, 0x0F09D948A, 0x000E2848C, 0x000E28491, 0x0F09D948D,
		/*  K 𝔎  */  /*  L 𝔏  */ /*  M 𝔐  */  /*  N 𝔑  */  /*  O 𝔒  */
		0x0F09D948E, 0x0F09D948F, 0x0F09D9490, 0x0F09D9491, 0x0F09D9492,
		/*  P 𝔓  */  /*  Q 𝔔  */ /*  R ℜ c*/  /*  S 𝔖  */  /*  T 𝔗  */
		0x0F09D9493, 0x0F09D9494, 0x000E2849C, 0x0F09D9496, 0x0F09D9497,
		/*  U 𝔘  */  /*  V 𝔙  */ /*  W 𝔚  */  /*  X 𝔛  */  /*  Y 𝔜  */  /*  Z ?  */
		0x0F09D9498, 0x0F09D9499, 0x0F09D949A, 0x0F09D949B, 0x0F09D949C, 0x000E284A8,

		/*  a 𝔞   */  /*  b 𝔟  */  /*  c 𝔠  */  /*  d 𝔡  */  /*  e 𝔢   */
		0x0F09D949E, 0x0F09D949F, 0x0F09D94A0, 0x0F09D94A1, 0x0F09D94A2,
		/*  f 𝔣   */  /*  g 𝔤  */  /*  h 𝔥  */  /*  i 𝔦  */  /*  j 𝔧   */
		0x0F09D94A3, 0x0F09D94A4, 0x0F09D94A5, 0x0F09D94A6, 0x0F09D94A7,
		/*  k 𝔨   */  /*  l 𝔩  */  /*  m 𝔪  */ /*  n 𝔫  */  /*  o 𝔬   */
		0x0F09D94A8, 0x0F09D94A9, 0x0F09D94AA, 0x0F09D94AB, 0x0F09D94AC,
		/*  p 𝔭  */  /*  q 𝔮  */  /*  r 𝔯  */   /*  s 𝔰  */  /*  t 𝔱  */
		0x0F09D94AD, 0x0F09D94AE, 0x0F09D94AF, 0x0F09D94B0, 0x0F09D94B1,
		/*  u 𝔲  */  /*  v 𝔳  */  /*  w 𝔴  */  /*  x 𝔵  */   /*  y 𝔶  */ /*  z 𝔷  */ 
		0x0F09D94B2, 0x0F09D94B3, 0x0F09D94B4, 0x0F09D94B5, 0x0F09D94B6, 0x0F09D94B7
	};

	unsigned i, j, bytes;
	uint32_t fraktur;

	for (i = 0; i < (26*2); ++i) {
		fraktur = FRAKTUR[i];
		bytes = (fraktur & 0xff000000) ? 4 : 3;
		for (j = 0; j < bytes; ++j)
			if ((unsigned char) stream[j] != ((fraktur >> ((bytes - j - 1) << 3)) & 0xff))
				goto not_equal;

		*index = i;
		return bytes;
	not_equal:;
	}

	return 0;
}

static struct sq_string *parse_fraktur_bareword(void) {
	unsigned fraktur_len, fraktur_pos;

	if (!(fraktur_len = fraktur_length(sq_stream, &fraktur_pos)))
		return NULL;

	char *fraktur = xmalloc(16);
	unsigned cap = 16, len = 0;

	do {
		if (cap == len)
			fraktur = xrealloc(fraktur, cap *= 2);

		if ((fraktur_len = fraktur_length(sq_stream, &fraktur_pos))) {
			sq_stream += fraktur_len;
			fraktur[len++] = (fraktur_pos < 26) ? ('A' + fraktur_pos) : ('a' + (fraktur_pos - 26));
			continue;
		}

		if (isspace(*sq_stream)) {
			fraktur[len++] = *(sq_stream++);
			continue;
		}

		break;
	} while (*sq_stream != '\0');

	while (isspace(fraktur[len - 1]))
		--len;

	fraktur[len] = '\0';
	return sq_string_new2(fraktur, len);
}


static void strip_whitespace(bool strip_newline) {
	char c;

	// strip whitespace
	while ((c = *sq_stream)) {
		if (c == '#') {
			do {
				c = *++sq_stream;
			} while (c && c != '\n');
			if (c == '\n') ++sq_stream;
			continue;
		}

		if (*sq_stream == '\\') {
			++sq_stream;

			if (*sq_stream && *sq_stream++ != '\n') 
				die("unexpected '\\' on its own.");
			continue;
		}

		if (!isspace(c) || (!strip_newline && c == '\n'))
			break;

		while (isspace(c) && c != '\n')
			c = *++sq_stream;
	}
}

#define CHECK_FOR_START(str, tkn) \
	if (!strncmp(str, sq_stream, strlen(str))) {\
		sq_stream += strlen(str); token.kind = tkn; return token; \
	}

#define CHECK_FOR_START_KW(str, tkn) \
	if (!strncmp(str, sq_stream, strlen(str)) \
		&& !isalnum(*(sq_stream + strlen(str))) && *(sq_stream + strlen(str)) != '_') {\
		sq_stream += strlen(str); token.kind = tkn; return token; }


static unsigned tohex(char c) {
	if (isdigit(c)) return c - '0';
	if ('a' <= c && c <= 'f') return c - 'a';
	if ('A' <= c && c <= 'F') return c - 'A';
	die("char '%1$c' (\\x%1$02x) isn't a hex digit", c);
}

static struct sq_token parse_arabic_numeral(void) {
	struct sq_token token;
	token.kind = SQ_TK_NUMBER;
	token.number = 0;

	do {
		token.number = token.number * 10 + (*sq_stream - '0');
	} while (isdigit(*++sq_stream));

	if (isalpha(*sq_stream) || *sq_stream == '_')
		die("invalid trailing characters on arabic numeral literal: %llu%c\n",
			(long long) token.number, *sq_stream);

	return token;
}

static struct sq_token next_normal_token(void);

#define MAX_INTERPOLATIONS 256
static struct { int stage; unsigned depth; char quote; } interpolations[MAX_INTERPOLATIONS];
static unsigned interpolation_length;

static struct sq_token next_interpolation_token(void) {
	struct sq_token token;

	switch (interpolations[interpolation_length].stage++){
	case -1: // `-1` is what's set to after we do the final `)`.
		put_back_quote = interpolations[interpolation_length--].quote;

	case 0:
		token.kind = SQ_TK_ADD;
		return token;

	case 1:
		token.kind = SQ_TK_IDENT;
		token.identifier = strdup("string");
		return token;

	case 2:
		token.kind = SQ_TK_LPAREN;
		return token;
	}

	token = next_normal_token();

	if (token.kind == SQ_TK_LPAREN) {
		++interpolations[interpolation_length].depth;
	} else if (token.kind == SQ_TK_RPAREN) {
		if (!--interpolations[interpolation_length].depth)
			interpolations[interpolation_length].stage = -1;
	}

	return token;
}
// whelp, `"foo \(bar)!" * 3` will not expand properly, so we need to have
// every string return a leading `(` first...; but that's too hard rn.

static struct sq_token parse_string(void) {
	unsigned length = 0;
	char *dst = xmalloc(strlen(sq_stream));
	char quote, c;

	if (put_back_quote)
		quote = put_back_quote, put_back_quote = '\0';
	else
		quote = *sq_stream++;

	while ((c = *sq_stream++) != quote) {
	top:
		if (!c)
			die("unterminated quote encountered");

		if (c != '\\') {
			dst[length++] = c;
			continue;
		}

		switch (c = *sq_stream++) {
		case '\\':
		case '\'': 
		case '\"':
			break;

		case '\n':
			goto top;

		case '(':
			if (MAX_INTERPOLATIONS < interpolation_length)
				die("too many interpolations");

			interpolations[interpolation_length + 1].depth = 1;
			interpolations[interpolation_length + 1].quote = quote;
			interpolations[++interpolation_length].stage = 0;

			goto done;

		case 'n': c = '\n'; break;
		case 't': c = '\t'; break;
		case 'f': c = '\f'; break;
		case 'v': c = '\v'; break;
		case 'r': c = '\r'; break;

		case 'x':
			if (sq_stream[0] == quote || sq_stream[0] == '\0' || sq_stream[1] == quote)
				die("unterminated escape sequence");

			c = tohex(sq_stream[0]) * 16 + tohex(sq_stream[1]);
			sq_stream += 2;
			break;
		}

		dst[length++] = c;
	}

done:

	dst[length] = '\0';

	struct sq_token token;
	token.kind = SQ_TK_STRING;
	token.string = sq_string_new2(dst, length);

	return token;
}

static struct sq_token parse_identifier(void) {
	struct sq_token token;
	token.kind = SQ_TK_IDENT;
	const char *start = sq_stream;

	while (isalnum(*sq_stream) || *sq_stream == '_')
		++sq_stream;

	token.identifier = strndup(start, sq_stream - start);

	// check to see if we're a label
	while (isspace(*sq_stream) || *sq_stream == '#')
		if (*sq_stream == '#')
			while (*sq_stream && *sq_stream++ != '\n');
		else
			++sq_stream;

	if (*sq_stream == ':')
		++sq_stream, token.kind = SQ_TK_LABEL;

	return token;
}

// TODO: an entire program `a + 4` doesn't work.
struct sq_token sq_next_token() {
	struct sq_token token = next_macro_token();

	if (token.kind != SQ_TK_UNDEFINED)
		return token;

	return interpolation_length ? next_interpolation_token() : next_normal_token();
}

static struct sq_token next_normal_token(void) {
	struct sq_token token;

	if (put_back_quote) return parse_string();

	strip_whitespace(false);
	CHECK_FOR_START("\n", SQ_TK_SOFT_ENDL);

	if (!*sq_stream || !strncmp(sq_stream, "__END__", 7))
		return token.kind = SQ_TK_UNDEFINED, token;

	if (isdigit(*sq_stream))
		return parse_arabic_numeral();

	if (sq_roman_is_numeral(*sq_stream)) {
		token.number = sq_roman_to_number(sq_stream, &sq_stream);
		if (token.number >= 0)
			return (token.kind = SQ_TK_NUMBER), token;
	}

	struct sq_string *fraktur;
	if ((fraktur = parse_fraktur_bareword()) != NULL)  {
		token.kind = SQ_TK_STRING;
		token.string = fraktur;
		return token;
	}

	if (*sq_stream == '\'' || *sq_stream == '\"')
		return parse_string();

	if (*sq_stream == '@') {
		++sq_stream;
		parse_macro_statement(parse_identifier().identifier);
		return sq_next_token();
	} else if (*sq_stream == '$') {
		++sq_stream;
		if (parse_macro_identifier(token.identifier = parse_identifier().identifier))
			return token.kind = SQ_TK_MACRO_VAR, token;
		return sq_next_token();
	}

	CHECK_FOR_START_KW("form",         SQ_TK_CLASS);
	CHECK_FOR_START_KW("matter",       SQ_TK_FIELD);
	CHECK_FOR_START_KW("change",       SQ_TK_METHOD);
	CHECK_FOR_START_KW("recollect",    SQ_TK_CLASSFN);
	CHECK_FOR_START_KW("imitate",      SQ_TK_CONSTRUCTOR);
	CHECK_FOR_START_KW("essence",      SQ_TK_ESSENCE);
	// substance?
	// essence is static  variable

	CHECK_FOR_START_KW("journey",      SQ_TK_FUNC);
	CHECK_FOR_START_KW("renowned",     SQ_TK_GLOBAL);
	CHECK_FOR_START_KW("nigh",         SQ_TK_LOCAL);
	CHECK_FOR_START_KW("import",       SQ_TK_IMPORT); // `befriend`? `beseech`?

	CHECK_FOR_START_KW("if",           SQ_TK_IF); // _should_ we have a better one?
	CHECK_FOR_START_KW("alas",         SQ_TK_ELSE);
	CHECK_FOR_START_KW("whence",       SQ_TK_COMEFROM);
	CHECK_FOR_START_KW("whilst",       SQ_TK_WHILE);
	CHECK_FOR_START_KW("reward",       SQ_TK_RETURN);
	CHECK_FOR_START_KW("attempt",      SQ_TK_TRY);
	CHECK_FOR_START_KW("catapult",     SQ_TK_THROW);
	CHECK_FOR_START_KW("retreat",      SQ_TK_CATCH);
	CHECK_FOR_START_KW("fork",         SQ_TK_SWITCH);
	CHECK_FOR_START_KW("path",         SQ_TK_CASE);

	CHECK_FOR_START_KW("yay",          SQ_TK_TRUE);
	CHECK_FOR_START_KW("nay",          SQ_TK_FALSE);
	CHECK_FOR_START_KW("ni",           SQ_TK_NULL);

	if (isalpha(*sq_stream) || *sq_stream == '_')
		return parse_identifier();

	CHECK_FOR_START("[]", SQ_TK_INDEX);
	CHECK_FOR_START("[]=", SQ_TK_INDEX_ASSIGN);
	CHECK_FOR_START("{", SQ_TK_LBRACE);
	CHECK_FOR_START("}", SQ_TK_RBRACE);
	CHECK_FOR_START("}", SQ_TK_RBRACE);
	CHECK_FOR_START("(", SQ_TK_LPAREN);
	CHECK_FOR_START(")", SQ_TK_RPAREN);
	CHECK_FOR_START("[", SQ_TK_LBRACKET);
	CHECK_FOR_START("]", SQ_TK_RBRACKET);
	CHECK_FOR_START(";", SQ_TK_ENDL);
	CHECK_FOR_START("\n", SQ_TK_SOFT_ENDL);
	CHECK_FOR_START(",", SQ_TK_COMMA);
	CHECK_FOR_START(".", SQ_TK_DOT);
	CHECK_FOR_START(":", SQ_TK_COLON);

	CHECK_FOR_START("==", SQ_TK_EQL);
	CHECK_FOR_START("!=", SQ_TK_NEQ);
	CHECK_FOR_START("<=", SQ_TK_LEQ);
	CHECK_FOR_START(">=", SQ_TK_GEQ);
	CHECK_FOR_START("<", SQ_TK_LTH);
	CHECK_FOR_START(">", SQ_TK_GTH);
	CHECK_FOR_START("+", SQ_TK_ADD);
	CHECK_FOR_START("-@", SQ_TK_NEG);
	CHECK_FOR_START("-", SQ_TK_SUB);
	CHECK_FOR_START("*", SQ_TK_MUL);
	CHECK_FOR_START("/", SQ_TK_DIV);
	CHECK_FOR_START("%", SQ_TK_MOD);
	CHECK_FOR_START("!", SQ_TK_NOT);
	CHECK_FOR_START("&&", SQ_TK_AND);
	CHECK_FOR_START("||", SQ_TK_OR);
	CHECK_FOR_START("=", SQ_TK_ASSIGN);

	die("unknown token start '%c'", *sq_stream);
}


void sq_token_dump(const struct sq_token *token) {
	// this isn't exactly correct anymore...
	switch (token->kind) {
	case SQ_TK_UNDEFINED: printf("Keyword(undefined)"); break;
	case SQ_TK_CLASS: printf("Keyword(class)"); break;
	case SQ_TK_FUNC: printf("Keyword(func)"); break;
	case SQ_TK_GLOBAL: printf("Keyword(global)"); break;
	case SQ_TK_IF: printf("Keyword(if)"); break;
	case SQ_TK_ELSE: printf("Keyword(else)"); break;
	case SQ_TK_RETURN: printf("Keyword(return)"); break;
	case SQ_TK_TRUE: printf("Keyword(true)"); break;
	case SQ_TK_FALSE: printf("Keyword(false)"); break;

	case SQ_TK_IDENT: printf("Ident(%s)", token->identifier); break;
	case SQ_TK_NUMBER: printf("Number(%lld)", (long long) token->number); break;
	case SQ_TK_STRING: printf("String(%s)", token->string->ptr); break;

	case SQ_TK_LBRACE: printf("Punct({)"); break;
	case SQ_TK_RBRACE: printf("Punct(})"); break;
	case SQ_TK_LPAREN: printf("Punct(()"); break;
	case SQ_TK_RPAREN: printf("Punct())"); break;
	case SQ_TK_LBRACKET: printf("Punct([)"); break;
	case SQ_TK_RBRACKET: printf("Punct(])"); break;
	case SQ_TK_ENDL: printf("Punct(;)"); break;
	case SQ_TK_SOFT_ENDL: printf("Punct(\\n)"); break;
	case SQ_TK_COMMA: printf("Punct(,)"); break;
	case SQ_TK_DOT: printf("Punct(.)"); break;

	case SQ_TK_EQL: printf("Operator(==)"); break;
	case SQ_TK_NEQ: printf("Operator(!=)"); break;
	case SQ_TK_ADD: printf("Operator(+)"); break;
	case SQ_TK_SUB: printf("Operator(-)"); break;
	case SQ_TK_MUL: printf("Operator(*)"); break;
	case SQ_TK_DIV: printf("Operator(/)"); break;
	case SQ_TK_MOD: printf("Operator(%%)"); break;
	case SQ_TK_NOT: printf("Operator(!)"); break;
	case SQ_TK_AND: printf("Operator(&&)"); break;
	case SQ_TK_OR: printf("Operator(||)"); break;
	case SQ_TK_ASSIGN: printf("Operator(=)"); break;

	default: printf("Unknown(%d)", token->kind); break;
	}
}


#define SQ_MACRO_INCLUDE
#include "macro.c"
