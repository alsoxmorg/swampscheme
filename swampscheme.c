#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <setjmp.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define TB_IMPL
#include "termbox2.h"

#ifdef WITH_SDL3
#include <SDL3/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_BOOL
#define NK_ASSERT assert
#define NK_MEMSET memset
#define NK_MEMCPY memcpy
#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "nuklear_sdl3_renderer.h"
#endif

typedef struct Val Val;
typedef struct Env Env;
typedef Val *(*Native)(Val *, Env *);

enum { T_NIL, T_BOOL, T_NUM, T_STR, T_SYM, T_PAIR, T_NATIVE, T_CLOSURE, T_EOF, T_PORT, T_VECTOR, T_RECORD, T_GAP, T_HASH,
#ifdef WITH_SDL3
	T_SDL_TEXTURE,
	T_SDL_SOUND,
#endif
};

struct Val {
	int t;
	unsigned mark;
	struct Val *gc_next;
	union {
		int b;
		double n;
		char *s;
		struct { Val *a, *d; } p;
		Native native;
		struct { Val *params, *body; Env *env; } clo;
		struct { FILE *f; int input, output, close; } port;
		struct { int len; Val **items; } vec;
		struct { char *type; int len; char **keys; Val **vals; } rec;
		struct { char *buf; int cap, gap_start, gap_end; } gap;
		struct { int len, cap; char **keys; Val **vals; } hash;
#ifdef WITH_SDL3
		SDL_Texture *sdl_texture;
		struct { Uint8 *data; Uint32 len; } sdl_sound;
#endif
	} as;
};

typedef struct Bind {
	char *name;
	Val *val;
	struct Bind *next;
} Bind;

struct Env {
	Bind *bind;
	Env *up;
};

static Val g_nil = { .t = T_NIL };
static Val g_true = { .t = T_BOOL, .as.b = 1 };
static Val g_false = { .t = T_BOOL, .as.b = 0 };
static Val g_eof = { .t = T_EOF };
static Val g_stdin = { .t = T_PORT };
static Val g_stdout = { .t = T_PORT };
static Val *NIL = &g_nil, *TRUE = &g_true, *FALSE = &g_false;
static Val *EOFV = &g_eof, *STDIN_PORT = &g_stdin, *STDOUT_PORT = &g_stdout;
static Val *gc_heap = NULL;
static size_t gc_heap_count = 0;
static size_t gc_last_freed = 0;
static jmp_buf *repl_error_target = NULL;
static int repl_error_silent = 0;
static char last_error[1024] = "";
static int swamp_tb_active = 0;
static int swamp_tb_x = 0, swamp_tb_y = 0;
static uintattr_t swamp_tb_fg = TB_DEFAULT, swamp_tb_bg = TB_DEFAULT;
static int saved_argc = 0;
static char **saved_argv = NULL;

#ifdef WITH_SDL3
static SDL_Window *swamp_sdl_window = NULL;
static SDL_Renderer *swamp_sdl_renderer = NULL;
static struct nk_context *swamp_nk = NULL;
static int swamp_sdl_frame = 0;
static SDL_AudioStream *swamp_sdl_audio = NULL;
static SDL_AudioSpec swamp_sdl_audio_spec = { SDL_AUDIO_F32, 2, 48000 };
#endif

static void termbox_cleanup(void)
{
	if (!swamp_tb_active) return;
	tb_shutdown();
	swamp_tb_active = 0;
	swamp_tb_x = swamp_tb_y = 0;
	swamp_tb_fg = swamp_tb_bg = TB_DEFAULT;
}

#ifdef WITH_SDL3
static void sdl3_cleanup(void)
{
	if (swamp_nk) {
		nk_sdl_shutdown(swamp_nk);
		swamp_nk = NULL;
	}
	if (swamp_sdl_renderer) {
		SDL_DestroyRenderer(swamp_sdl_renderer);
		swamp_sdl_renderer = NULL;
	}
	if (swamp_sdl_window) {
		SDL_DestroyWindow(swamp_sdl_window);
		swamp_sdl_window = NULL;
	}
	if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_QuitSubSystem(SDL_INIT_VIDEO);
	if (swamp_sdl_audio) {
		SDL_DestroyAudioStream(swamp_sdl_audio);
		swamp_sdl_audio = NULL;
	}
	if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
	swamp_sdl_frame = 0;
}
#endif

static void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(last_error, sizeof(last_error), fmt, ap);
	va_end(ap);
	if (repl_error_target) {
		if (!repl_error_silent) fprintf(stderr, "swampscheme: %s\n", last_error);
		longjmp(*repl_error_target, 1);
	}
	fprintf(stderr, "swampscheme: %s\n", last_error);
	termbox_cleanup();
	exit(1);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) die("out of memory");
	return p;
}

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = xmalloc(n);
	memcpy(p, s, n);
	return p;
}

static Val *val(int t)
{
	Val *v = xmalloc(sizeof(*v));
	memset(v, 0, sizeof(*v));
	v->t = t;
	v->gc_next = gc_heap;
	gc_heap = v;
	gc_heap_count++;
	return v;
}

static Val *num(double n) { Val *v = val(T_NUM); v->as.n = n; return v; }
static Val *str(const char *s) { Val *v = val(T_STR); v->as.s = xstrdup(s); return v; }
static Val *sym(const char *s) { Val *v = val(T_SYM); v->as.s = xstrdup(s); return v; }
static Val *cons(Val *a, Val *d) { Val *v = val(T_PAIR); v->as.p.a = a; v->as.p.d = d; return v; }
static Val *native(Native f) { Val *v = val(T_NATIVE); v->as.native = f; return v; }
static Val *closure(Val *p, Val *b, Env *e) { Val *v = val(T_CLOSURE); v->as.clo.params = p; v->as.clo.body = b; v->as.clo.env = e; return v; }
static Val *port(FILE *f, int input, int output, int close) { Val *v = val(T_PORT); v->as.port.f = f; v->as.port.input = input; v->as.port.output = output; v->as.port.close = close; return v; }
static Val *vector_new(int n, Val *fill)
{
	if (n < 0) die("bad vector length");
	Val *v = val(T_VECTOR);
	v->as.vec.len = n;
	v->as.vec.items = xmalloc(sizeof(*v->as.vec.items) * (size_t)n);
	for (int i = 0; i < n; i++) v->as.vec.items[i] = fill;
	return v;
}

static Val *record_new(const char *type, int n)
{
	if (n < 0) die("bad record length");
	Val *v = val(T_RECORD);
	v->as.rec.type = xstrdup(type);
	v->as.rec.len = n;
	v->as.rec.keys = xmalloc(sizeof(*v->as.rec.keys) * (size_t)n);
	v->as.rec.vals = xmalloc(sizeof(*v->as.rec.vals) * (size_t)n);
	for (int i = 0; i < n; i++) {
		v->as.rec.keys[i] = NULL;
		v->as.rec.vals[i] = NIL;
	}
	return v;
}

static int gap_length(Val *v)
{
	return v->as.gap.cap - (v->as.gap.gap_end - v->as.gap.gap_start);
}

static Val *gap_new(const char *text)
{
	int n = (int)strlen(text);
	int gap = n < 64 ? 64 : n;
	Val *v = val(T_GAP);
	v->as.gap.cap = n + gap;
	v->as.gap.buf = xmalloc((size_t)v->as.gap.cap);
	memcpy(v->as.gap.buf, text, (size_t)n);
	v->as.gap.gap_start = n;
	v->as.gap.gap_end = v->as.gap.cap;
	return v;
}

static Val *need_gap(Val *v)
{
	if (v->t != T_GAP) die("expected gap buffer");
	return v;
}

static int gap_physical_index(Val *v, int i)
{
	int len = gap_length(v);
	if (i < 0 || i >= len) die("gap-buffer-ref range");
	return i < v->as.gap.gap_start ? i : i + (v->as.gap.gap_end - v->as.gap.gap_start);
}

static int gap_char_at(Val *v, int i)
{
	return (unsigned char)v->as.gap.buf[gap_physical_index(v, i)];
}

static void gap_move(Val *v, int pos)
{
	int len = gap_length(v);
	if (pos < 0 || pos > len) die("gap-buffer point range");
	if (pos < v->as.gap.gap_start) {
		int n = v->as.gap.gap_start - pos;
		memmove(v->as.gap.buf + v->as.gap.gap_end - n, v->as.gap.buf + pos, (size_t)n);
		v->as.gap.gap_start -= n;
		v->as.gap.gap_end -= n;
	} else if (pos > v->as.gap.gap_start) {
		int n = pos - v->as.gap.gap_start;
		memmove(v->as.gap.buf + v->as.gap.gap_start, v->as.gap.buf + v->as.gap.gap_end, (size_t)n);
		v->as.gap.gap_start += n;
		v->as.gap.gap_end += n;
	}
}

static void gap_ensure(Val *v, int need)
{
	int have = v->as.gap.gap_end - v->as.gap.gap_start;
	if (have >= need) return;
	int len = gap_length(v);
	int add = need - have;
	if (add < v->as.gap.cap) add = v->as.gap.cap;
	if (add < 64) add = 64;
	int newcap = v->as.gap.cap + add;
	char *next = xmalloc((size_t)newcap);
	int before = v->as.gap.gap_start;
	int after = v->as.gap.cap - v->as.gap.gap_end;
	memcpy(next, v->as.gap.buf, (size_t)before);
	memcpy(next + newcap - after, v->as.gap.buf + v->as.gap.gap_end, (size_t)after);
	free(v->as.gap.buf);
	v->as.gap.buf = next;
	v->as.gap.cap = newcap;
	v->as.gap.gap_start = before;
	v->as.gap.gap_end = newcap - after;
	if (gap_length(v) != len) die("gap buffer resize failed");
}

static char *gap_to_cstr(Val *v)
{
	need_gap(v);
	int before = v->as.gap.gap_start;
	int after = v->as.gap.cap - v->as.gap.gap_end;
	char *s = xmalloc((size_t)before + (size_t)after + 1);
	memcpy(s, v->as.gap.buf, (size_t)before);
	memcpy(s + before, v->as.gap.buf + v->as.gap.gap_end, (size_t)after);
	s[before + after] = 0;
	return s;
}

static Val *hash_new(void)
{
	Val *v = val(T_HASH);
	v->as.hash.cap = 16;
	v->as.hash.len = 0;
	v->as.hash.keys = xmalloc(sizeof(*v->as.hash.keys) * (size_t)v->as.hash.cap);
	v->as.hash.vals = xmalloc(sizeof(*v->as.hash.vals) * (size_t)v->as.hash.cap);
	for (int i = 0; i < v->as.hash.cap; i++) {
		v->as.hash.keys[i] = NULL;
		v->as.hash.vals[i] = NIL;
	}
	return v;
}

static const char *hash_key_name(Val *v)
{
	if (v->t == T_SYM || v->t == T_STR) return v->as.s;
	die("expected hash key string or symbol");
	return "";
}

static Val *need_hash(Val *v)
{
	if (v->t != T_HASH) die("expected hash table");
	return v;
}

static int hash_index(Val *h, const char *key)
{
	for (int i = 0; i < h->as.hash.len; i++)
		if (strcmp(h->as.hash.keys[i], key) == 0) return i;
	return -1;
}

static void hash_ensure(Val *h)
{
	if (h->as.hash.len < h->as.hash.cap) return;
	int old = h->as.hash.cap;
	h->as.hash.cap *= 2;
	char **keys = xmalloc(sizeof(*keys) * (size_t)h->as.hash.cap);
	Val **vals = xmalloc(sizeof(*vals) * (size_t)h->as.hash.cap);
	for (int i = 0; i < old; i++) {
		keys[i] = h->as.hash.keys[i];
		vals[i] = h->as.hash.vals[i];
	}
	for (int i = old; i < h->as.hash.cap; i++) {
		keys[i] = NULL;
		vals[i] = NIL;
	}
	free(h->as.hash.keys);
	free(h->as.hash.vals);
	h->as.hash.keys = keys;
	h->as.hash.vals = vals;
}
static Val *boolean(int b) { return b ? TRUE : FALSE; }

static char *current_load_dir = NULL;

#define car(v) ((v)->as.p.a)
#define cdr(v) ((v)->as.p.d)
#define cadr(v) (car(cdr(v)))
#define caddr(v) (car(cdr(cdr(v))))
#define cddr(v) (cdr(cdr(v)))
#define cadddr(v) (car(cdr(cdr(cdr(v)))))

static int is_nil(Val *v) { return v == NIL || v->t == T_NIL; }
static int truthy(Val *v) { return v != FALSE; }
static int is_sym(Val *v, const char *s) { return v->t == T_SYM && strcmp(v->as.s, s) == 0; }

#define GC_ROOT_MAX 4096
static Val **gc_roots[GC_ROOT_MAX];
static int gc_root_count = 0;

static void gc_push(Val **slot)
{
	if (gc_root_count >= GC_ROOT_MAX) die("gc root stack overflow");
	gc_roots[gc_root_count++] = slot;
}

static void gc_pop(int n)
{
	if (n < 0 || n > gc_root_count) die("gc root stack underflow");
	gc_root_count -= n;
}

static Env *env_new(Env *up)
{
	Env *e = xmalloc(sizeof(*e));
	e->bind = NULL;
	e->up = up;
	return e;
}

static Bind *env_find(Env *e, const char *name)
{
	for (; e; e = e->up)
		for (Bind *b = e->bind; b; b = b->next)
			if (strcmp(b->name, name) == 0) return b;
	return NULL;
}

static void env_def(Env *e, const char *name, Val *v)
{
	Bind *b = env_find(e, name);
	if (b && e->bind == b) { b->val = v; return; }
	b = xmalloc(sizeof(*b));
	b->name = xstrdup(name);
	b->val = v;
	b->next = e->bind;
	e->bind = b;
}

static void env_set(Env *e, const char *name, Val *v)
{
	Bind *b = env_find(e, name);
	if (!b) die("unbound variable: %s", name);
	b->val = v;
}

static Val *env_get(Env *e, const char *name)
{
	Bind *b = env_find(e, name);
	if (!b) die("unbound variable: %s", name);
	return b->val;
}

static void gc_mark_env(Env *e);

static void gc_mark_val(Val *v)
{
	if (!v || v->mark) return;
	v->mark = 1;
	switch (v->t) {
	case T_PAIR:
		gc_mark_val(car(v));
		gc_mark_val(cdr(v));
		break;
	case T_CLOSURE:
		gc_mark_val(v->as.clo.params);
		gc_mark_val(v->as.clo.body);
		gc_mark_env(v->as.clo.env);
		break;
	case T_VECTOR:
		for (int i = 0; i < v->as.vec.len; i++) gc_mark_val(v->as.vec.items[i]);
		break;
	case T_RECORD:
		for (int i = 0; i < v->as.rec.len; i++) gc_mark_val(v->as.rec.vals[i]);
		break;
	case T_HASH:
		for (int i = 0; i < v->as.hash.len; i++) gc_mark_val(v->as.hash.vals[i]);
		break;
	default:
		break;
	}
}

static void gc_mark_env(Env *e)
{
	for (; e; e = e->up)
		for (Bind *b = e->bind; b; b = b->next)
			gc_mark_val(b->val);
}

static void gc_free_val(Val *v)
{
	switch (v->t) {
	case T_STR:
	case T_SYM:
		free(v->as.s);
		break;
	case T_PORT:
		if (v->as.port.close && v->as.port.f) fclose(v->as.port.f);
		break;
	case T_VECTOR:
		free(v->as.vec.items);
		break;
	case T_RECORD:
		free(v->as.rec.type);
		for (int i = 0; i < v->as.rec.len; i++) free(v->as.rec.keys[i]);
		free(v->as.rec.keys);
		free(v->as.rec.vals);
		break;
	case T_GAP:
		free(v->as.gap.buf);
		break;
	case T_HASH:
		for (int i = 0; i < v->as.hash.len; i++) free(v->as.hash.keys[i]);
		free(v->as.hash.keys);
		free(v->as.hash.vals);
		break;
#ifdef WITH_SDL3
	case T_SDL_TEXTURE:
		if (v->as.sdl_texture) SDL_DestroyTexture(v->as.sdl_texture);
		break;
	case T_SDL_SOUND:
		if (v->as.sdl_sound.data) SDL_free(v->as.sdl_sound.data);
		break;
#endif
	default:
		break;
	}
	free(v);
}

static size_t gc_collect(Env *root_env)
{
	gc_mark_val(NIL);
	gc_mark_val(TRUE);
	gc_mark_val(FALSE);
	gc_mark_val(EOFV);
	gc_mark_val(STDIN_PORT);
	gc_mark_val(STDOUT_PORT);
	gc_mark_env(root_env);
	for (int i = 0; i < gc_root_count; i++)
		if (gc_roots[i]) gc_mark_val(*gc_roots[i]);

	size_t freed = 0;
	Val **link = &gc_heap;
	while (*link) {
		Val *v = *link;
		if (v->mark) {
			v->mark = 0;
			link = &v->gc_next;
		} else {
			*link = v->gc_next;
			gc_free_val(v);
			freed++;
			gc_heap_count--;
		}
	}
	gc_last_freed = freed;
	return freed;
}

typedef struct {
	const char *s;
	size_t i;
} Reader;

static void skip_ws(Reader *r)
{
	for (;;) {
		while (isspace((unsigned char)r->s[r->i])) r->i++;
		if (r->i == 0 && r->s[0] == '#' && r->s[1] == '!') {
			while (r->s[r->i] && r->s[r->i] != '\n') r->i++;
			continue;
		}
		if ((r->i == 0 || r->s[r->i - 1] == '\n') && r->s[r->i] == '!' && r->s[r->i + 1] == '#') {
			while (r->s[r->i] && r->s[r->i] != '\n') r->i++;
			continue;
		}
		if (r->s[r->i] == ';') {
			while (r->s[r->i] && r->s[r->i] != '\n') r->i++;
		} else break;
	}
}

static int delim(int c)
{
	return c == 0 || isspace((unsigned char)c) || c == '(' || c == ')' ||
	       c == '\'' || c == '`' || c == ',' || c == '"';
}

static Val *read_expr(Reader *r);
static Val *list_to_vector_value(Val *xs);

static Val *read_list(Reader *r)
{
	Val *head = NIL, **tail = &head;
	skip_ws(r);
	if (r->s[r->i] == ')') { r->i++; return NIL; }
	while (r->s[r->i] && r->s[r->i] != ')') {
		if (r->s[r->i] == '.') {
			r->i++;
			*tail = read_expr(r);
			skip_ws(r);
			if (r->s[r->i++] != ')') die("expected ) after dotted pair");
			return head;
		}
		*tail = cons(read_expr(r), NIL);
		tail = &cdr(*tail);
		skip_ws(r);
	}
	if (r->s[r->i] != ')') die("unterminated list");
	r->i++;
	return head;
}

static Val *read_string(Reader *r)
{
	char buf[8192];
	size_t n = 0;
	r->i++;
	while (r->s[r->i] && r->s[r->i] != '"') {
		int c = r->s[r->i++];
		if (c == '\\') {
			c = r->s[r->i++];
			if (c == 'n') c = '\n';
			else if (c == 't') c = '\t';
		}
		if (n + 1 >= sizeof(buf)) die("string too long");
		buf[n++] = (char)c;
	}
	if (r->s[r->i] != '"') die("unterminated string");
	r->i++;
	buf[n] = 0;
	return str(buf);
}

static Val *read_atom(Reader *r)
{
	char buf[512];
	size_t n = 0;
	while (!delim((unsigned char)r->s[r->i])) {
		if (n + 1 >= sizeof(buf)) die("atom too long");
		buf[n++] = r->s[r->i++];
	}
	buf[n] = 0;
	if (strcmp(buf, "#t") == 0 || strcmp(buf, "#true") == 0) return TRUE;
	if (strcmp(buf, "#f") == 0 || strcmp(buf, "#false") == 0) return FALSE;
	char *end = NULL;
	errno = 0;
	double d = strtod(buf, &end);
	if (!errno && end && *end == 0 && strpbrk(buf, "0123456789")) return num(d);
	return sym(buf);
}

static Val *read_expr(Reader *r)
{
	skip_ws(r);
	if (!r->s[r->i]) return NULL;
	if (r->s[r->i] == '(') { r->i++; return read_list(r); }
	if (r->s[r->i] == ')') die("unexpected )");
	if (r->s[r->i] == '#' && r->s[r->i + 1] == '(') {
		r->i += 2;
		return list_to_vector_value(read_list(r));
	}
	if (r->s[r->i] == '"') return read_string(r);
	if (r->s[r->i] == '\'') { r->i++; return cons(sym("quote"), cons(read_expr(r), NIL)); }
	if (r->s[r->i] == '`') { r->i++; return cons(sym("quasiquote"), cons(read_expr(r), NIL)); }
	if (r->s[r->i] == ',') {
		r->i++;
		if (r->s[r->i] == '@') {
			r->i++;
			return cons(sym("unquote-splicing"), cons(read_expr(r), NIL));
		}
		return cons(sym("unquote"), cons(read_expr(r), NIL));
	}
	return read_atom(r);
}

static void print_val(Val *v);

static void swamp_putc(int c)
{
	if (!swamp_tb_active) {
		fputc(c, stdout);
		return;
	}
	if (c == '\n') {
		swamp_tb_x = 0;
		if (swamp_tb_y + 1 < tb_height()) swamp_tb_y++;
		return;
	}
	if (swamp_tb_x >= tb_width()) {
		swamp_tb_x = 0;
		if (swamp_tb_y + 1 < tb_height()) swamp_tb_y++;
	}
	if (swamp_tb_y < tb_height()) {
		tb_set_cell(swamp_tb_x, swamp_tb_y, (uint32_t)(unsigned char)c,
		            swamp_tb_fg, swamp_tb_bg);
		swamp_tb_x++;
	}
}

static void swamp_puts(const char *s)
{
	for (; *s; s++) swamp_putc((unsigned char)*s);
}

static void port_putc(Val *portv, int c)
{
	if (portv == STDOUT_PORT) {
		swamp_putc(c);
		return;
	}
	if (portv->t != T_PORT || !portv->as.port.f || !portv->as.port.output) die("expected output port");
	if (fputc(c, portv->as.port.f) == EOF) die("port write failed");
}

static void port_puts(Val *portv, const char *s)
{
	for (; *s; s++) port_putc(portv, (unsigned char)*s);
}

static void swamp_printf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	swamp_puts(buf);
}

static void print_list(Val *v)
{
	for (;;) {
		print_val(car(v));
		v = cdr(v);
		if (is_nil(v)) return;
		if (v->t != T_PAIR) { swamp_puts(" . "); print_val(v); return; }
		swamp_putc(' ');
	}
}

static void print_vector(Val *v)
{
	swamp_puts("#(");
	for (int i = 0; i < v->as.vec.len; i++) {
		if (i) swamp_putc(' ');
		print_val(v->as.vec.items[i]);
	}
	swamp_putc(')');
}

static void print_escaped(const char *s)
{
	swamp_putc('"');
	for (; *s; s++) {
		if (*s == '\n') swamp_puts("\\n");
		else if (*s == '\t') swamp_puts("\\t");
		else if (*s == '"' || *s == '\\') { swamp_putc('\\'); swamp_putc(*s); }
		else swamp_putc(*s);
	}
	swamp_putc('"');
}

static void print_val(Val *v)
{
	switch (v->t) {
	case T_NIL: swamp_puts("()"); break;
	case T_BOOL: swamp_puts(v->as.b ? "#t" : "#f"); break;
	case T_NUM:
		if (fabs(v->as.n - (long long)v->as.n) < 0.000000001) swamp_printf("%lld", (long long)v->as.n);
		else swamp_printf("%.15g", v->as.n);
		break;
	case T_STR: print_escaped(v->as.s); break;
	case T_SYM: swamp_puts(v->as.s); break;
	case T_PAIR: swamp_putc('('); print_list(v); swamp_putc(')'); break;
	case T_NATIVE: swamp_puts("#<procedure>"); break;
	case T_CLOSURE: swamp_puts("#<closure>"); break;
	case T_EOF: swamp_puts("#<eof>"); break;
	case T_PORT: swamp_puts("#<port>"); break;
	case T_VECTOR: print_vector(v); break;
	case T_RECORD: swamp_printf("#<record %s>", v->as.rec.type); break;
	case T_GAP: swamp_printf("#<gap-buffer %d>", gap_length(v)); break;
	case T_HASH: swamp_printf("#<hash-table %d>", v->as.hash.len); break;
	}
}

static void print_val_port(Val *portv, Val *v);

static void print_list_port(Val *portv, Val *v)
{
	for (;;) {
		print_val_port(portv, car(v));
		v = cdr(v);
		if (is_nil(v)) return;
		if (v->t != T_PAIR) { port_puts(portv, " . "); print_val_port(portv, v); return; }
		port_putc(portv, ' ');
	}
}

static void print_vector_port(Val *portv, Val *v)
{
	port_puts(portv, "#(");
	for (int i = 0; i < v->as.vec.len; i++) {
		if (i) port_putc(portv, ' ');
		print_val_port(portv, v->as.vec.items[i]);
	}
	port_putc(portv, ')');
}

static void print_escaped_port(Val *portv, const char *s)
{
	port_putc(portv, '"');
	for (; *s; s++) {
		if (*s == '\n') port_puts(portv, "\\n");
		else if (*s == '\t') port_puts(portv, "\\t");
		else if (*s == '"' || *s == '\\') { port_putc(portv, '\\'); port_putc(portv, *s); }
		else port_putc(portv, *s);
	}
	port_putc(portv, '"');
}

static void print_val_port(Val *portv, Val *v)
{
	char buf[64];
	switch (v->t) {
	case T_NIL: port_puts(portv, "()"); break;
	case T_BOOL: port_puts(portv, v->as.b ? "#t" : "#f"); break;
	case T_NUM:
		if (fabs(v->as.n - (long long)v->as.n) < 0.000000001) snprintf(buf, sizeof(buf), "%lld", (long long)v->as.n);
		else snprintf(buf, sizeof(buf), "%.15g", v->as.n);
		port_puts(portv, buf);
		break;
	case T_STR: print_escaped_port(portv, v->as.s); break;
	case T_SYM: port_puts(portv, v->as.s); break;
	case T_PAIR: port_putc(portv, '('); print_list_port(portv, v); port_putc(portv, ')'); break;
	case T_NATIVE: port_puts(portv, "#<procedure>"); break;
	case T_CLOSURE: port_puts(portv, "#<closure>"); break;
	case T_EOF: port_puts(portv, "#<eof>"); break;
	case T_PORT: port_puts(portv, "#<port>"); break;
	case T_VECTOR: print_vector_port(portv, v); break;
	case T_RECORD: port_puts(portv, "#<record "); port_puts(portv, v->as.rec.type); port_putc(portv, '>'); break;
	case T_GAP: snprintf(buf, sizeof(buf), "#<gap-buffer %d>", gap_length(v)); port_puts(portv, buf); break;
	case T_HASH: snprintf(buf, sizeof(buf), "#<hash-table %d>", v->as.hash.len); port_puts(portv, buf); break;
	}
}

static Val *eval(Val *x, Env *e);

static Val *eval_list(Val *xs, Env *e)
{
	Val *head = NIL, **tail = &head;
	gc_push(&head);
	for (; !is_nil(xs); xs = cdr(xs)) {
		if (xs->t != T_PAIR) die("bad argument list");
		*tail = cons(eval(car(xs), e), NIL);
		tail = &cdr(*tail);
	}
	gc_pop(1);
	return head;
}

static int len(Val *xs)
{
	int n = 0;
	for (; !is_nil(xs); xs = cdr(xs)) {
		if (xs->t != T_PAIR) die("expected proper list");
		n++;
	}
	return n;
}

static int proper_list(Val *xs)
{
	for (; !is_nil(xs); xs = cdr(xs))
		if (xs->t != T_PAIR) return 0;
	return 1;
}

static Val *list_to_vector_value(Val *xs)
{
	int n = len(xs);
	Val *v = vector_new(n, NIL);
	int i = 0;
	for (Val *p = xs; !is_nil(p); p = cdr(p)) v->as.vec.items[i++] = car(p);
	return v;
}

static Val *vector_to_list_value(Val *v)
{
	if (v->t != T_VECTOR) die("expected vector");
	Val *r = NIL;
	for (int i = v->as.vec.len - 1; i >= 0; i--) r = cons(v->as.vec.items[i], r);
	return r;
}

static const char *field_name(Val *v)
{
	if (v->t == T_SYM || v->t == T_STR) return v->as.s;
	die("expected field name");
	return "";
}

static int record_field_index(Val *r, const char *key)
{
	if (r->t != T_RECORD) die("expected record");
	for (int i = 0; i < r->as.rec.len; i++)
		if (strcmp(r->as.rec.keys[i], key) == 0) return i;
	return -1;
}

static Val *eval_begin(Val *body, Env *e)
{
	Val *r = NIL;
	for (; !is_nil(body); body = cdr(body)) r = eval(car(body), e);
	return r;
}

static Val *eval_try(Val *args, Env *e)
{
	if (len(args) != 2) die("try arity");
	jmp_buf try_error;
	jmp_buf *prev_target = repl_error_target;
	int prev_silent = repl_error_silent;
	int prev_gc_roots = gc_root_count;
	repl_error_target = &try_error;
	repl_error_silent = 1;
	if (setjmp(try_error)) {
		gc_root_count = prev_gc_roots;
		repl_error_target = prev_target;
		repl_error_silent = prev_silent;
		return eval(cadr(args), e);
	}
	Val *r = eval(car(args), e);
	gc_root_count = prev_gc_roots;
	repl_error_target = prev_target;
	repl_error_silent = prev_silent;
	return r;
}

static Env *bind_closure(Val *fn, Val *args)
{
	if (fn->t != T_CLOSURE) die("attempt to call non-procedure");
	Env *call = env_new(fn->as.clo.env);
	Val *p = fn->as.clo.params, *a = args;
	while (!is_nil(p)) {
		if (p->t == T_SYM) { env_def(call, p->as.s, a); a = NIL; break; }
		if (p->t != T_PAIR) die("bad lambda parameters");
		if (is_nil(a)) die("too few arguments");
		env_def(call, car(p)->as.s, car(a));
		p = cdr(p);
		a = cdr(a);
	}
	if (!is_nil(a)) die("too many arguments");
	return call;
}

static Val *apply(Val *fn, Val *args, Env *e)
{
	if (fn->t == T_NATIVE) return fn->as.native(args, e);
	Env *call = bind_closure(fn, args);
	return eval_begin(fn->as.clo.body, call);
}

static Val *eval_define(Val *args, Env *e)
{
	Val *target = car(args);
	if (target->t == T_PAIR) {
		Val *name = car(target);
		if (name->t != T_SYM) die("bad define");
		env_def(e, name->as.s, closure(cdr(target), cdr(args), e));
		return name;
	}
	if (target->t != T_SYM || len(args) != 2) die("bad define");
	env_def(e, target->as.s, eval(cadr(args), e));
	return target;
}

static int tagged_pair(Val *v, const char *tag)
{
	return v->t == T_PAIR && car(v)->t == T_SYM && strcmp(car(v)->as.s, tag) == 0;
}

static Val *eval_quasiquote(Val *x, Env *e, int depth);

static Val *eval_quasiquote_list(Val *xs, Env *e, int depth)
{
	Val *out = NIL, **tail = &out;
	gc_push(&out);
	for (; xs->t == T_PAIR; xs = cdr(xs)) {
		Val *item = car(xs);
		if (tagged_pair(item, "unquote-splicing") && depth == 1) {
			if (len(cdr(item)) != 1) die("unquote-splicing arity");
			Val *sp = eval(cadr(item), e);
			for (; !is_nil(sp); sp = cdr(sp)) {
				if (sp->t != T_PAIR) die("unquote-splicing expected proper list");
				*tail = cons(car(sp), NIL);
				tail = &cdr(*tail);
			}
		} else {
			*tail = cons(eval_quasiquote(item, e, depth), NIL);
			tail = &cdr(*tail);
		}
	}
	if (!is_nil(xs)) *tail = eval_quasiquote(xs, e, depth);
	gc_pop(1);
	return out;
}

static Val *eval_quasiquote(Val *x, Env *e, int depth)
{
	if (x->t == T_PAIR) {
		if (tagged_pair(x, "unquote")) {
			if (len(cdr(x)) != 1) die("unquote arity");
			if (depth == 1) return eval(cadr(x), e);
			return cons(sym("unquote"), cons(eval_quasiquote(cadr(x), e, depth - 1), NIL));
		}
		if (tagged_pair(x, "unquote-splicing")) {
			if (len(cdr(x)) != 1) die("unquote-splicing arity");
			if (depth == 1) die("unquote-splicing outside list");
			return cons(sym("unquote-splicing"), cons(eval_quasiquote(cadr(x), e, depth - 1), NIL));
		}
		if (tagged_pair(x, "quasiquote")) {
			if (len(cdr(x)) != 1) die("quasiquote arity");
			return cons(sym("quasiquote"), cons(eval_quasiquote(cadr(x), e, depth + 1), NIL));
		}
		return eval_quasiquote_list(x, e, depth);
	}
	if (x->t == T_VECTOR)
		return list_to_vector_value(eval_quasiquote_list(vector_to_list_value(x), e, depth));
	return x;
}

static int cond_expand_requirement(Val *requirement)
{
	if (requirement->t == T_SYM) return strcmp(requirement->as.s, "swampscheme") == 0;
	if (requirement->t != T_PAIR) return 0;
	Val *op = car(requirement), *args = cdr(requirement);
	if (op->t != T_SYM) return 0;
	if (strcmp(op->as.s, "and") == 0) {
		for (; !is_nil(args); args = cdr(args))
			if (!cond_expand_requirement(car(args))) return 0;
		return 1;
	}
	if (strcmp(op->as.s, "or") == 0) {
		for (; !is_nil(args); args = cdr(args))
			if (cond_expand_requirement(car(args))) return 1;
		return 0;
	}
	if (strcmp(op->as.s, "not") == 0) {
		if (len(args) != 1) die("cond-expand not arity");
		return !cond_expand_requirement(car(args));
	}
	if (strcmp(op->as.s, "library") == 0) return 0;
	return 0;
}

static Val *eval_inner(Val *x, Env *e)
{
tail:
	if (!x) return NIL;
	switch (x->t) {
	case T_SYM: return env_get(e, x->as.s);
	case T_PAIR: break;
	default: return x;
	}
	Val *op = car(x), *args = cdr(x);
	if (op->t == T_SYM) {
		if (is_sym(op, "quote")) return car(args);
		if (is_sym(op, "quasiquote")) {
			if (len(args) != 1) die("quasiquote arity");
			return eval_quasiquote(car(args), e, 1);
		}
		if (is_sym(op, "if")) {
			Val *alt = is_nil(cdr(cdr(args))) ? NIL : caddr(args);
			x = truthy(eval(car(args), e)) ? cadr(args) : alt;
			goto tail;
		}
		if (is_sym(op, "begin")) {
			while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
			x = is_nil(args) ? NIL : car(args);
			goto tail;
		}
		if (is_sym(op, "use-modules")) return NIL;
		if (is_sym(op, "cond-expand")) {
			if (is_nil(args)) die("cond-expand needs a clause");
			for (Val *p = args; !is_nil(p); p = cdr(p)) {
				Val *clause = car(p);
				if (clause->t != T_PAIR || is_nil(clause)) die("bad cond-expand clause");
				Val *requirement = car(clause);
				int selected = is_sym(requirement, "else") || cond_expand_requirement(requirement);
				if (selected) return eval_begin(cdr(clause), e);
			}
			die("cond-expand has no matching clause");
		}
		if (is_sym(op, "define")) return eval_define(args, e);
		if (is_sym(op, "set!")) { env_set(e, car(args)->as.s, eval(cadr(args), e)); return car(args); }
		if (is_sym(op, "lambda")) return closure(car(args), cdr(args), e);
		if (is_sym(op, "let")) {
			if (car(args)->t == T_SYM) {
				Val *name = car(args), *bindings = cadr(args), *body = cdr(cdr(args));
				Env *ne = env_new(e);
				Val *params = NIL, **pt = &params, *vals = NIL, **vt = &vals;
				for (Val *bs = bindings; !is_nil(bs); bs = cdr(bs)) {
					Val *b = car(bs);
					if (len(b) != 2 || car(b)->t != T_SYM) die("bad named let binding");
					*pt = cons(car(b), NIL); pt = &cdr(*pt);
					*vt = cons(eval(cadr(b), e), NIL); vt = &cdr(*vt);
				}
				Val *fn = closure(params, body, ne);
				env_def(ne, name->as.s, fn);
				e = bind_closure(fn, vals);
				args = body;
			} else {
				Env *ne = env_new(e);
				for (Val *bs = car(args); !is_nil(bs); bs = cdr(bs)) {
					Val *b = car(bs);
					if (len(b) != 2 || car(b)->t != T_SYM) die("bad let binding");
					env_def(ne, car(b)->as.s, eval(cadr(b), e));
				}
				e = ne;
				args = cdr(args);
			}
			while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
			x = is_nil(args) ? NIL : car(args);
			goto tail;
		}
		if (is_sym(op, "let*")) {
			Env *ne = env_new(e);
			for (Val *bs = car(args); !is_nil(bs); bs = cdr(bs)) {
				Val *b = car(bs);
				if (len(b) != 2 || car(b)->t != T_SYM) die("bad let* binding");
				env_def(ne, car(b)->as.s, eval(cadr(b), ne));
			}
			e = ne;
			args = cdr(args);
			while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
			x = is_nil(args) ? NIL : car(args);
			goto tail;
		}
		if (is_sym(op, "letrec")) {
			Env *ne = env_new(e);
			for (Val *bs = car(args); !is_nil(bs); bs = cdr(bs)) {
				Val *b = car(bs);
				if (len(b) != 2 || car(b)->t != T_SYM) die("bad letrec binding");
				env_def(ne, car(b)->as.s, FALSE);
			}
			for (Val *bs = car(args); !is_nil(bs); bs = cdr(bs)) {
				Val *b = car(bs);
				env_set(ne, car(b)->as.s, eval(cadr(b), ne));
			}
			e = ne;
			args = cdr(args);
			while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
			x = is_nil(args) ? NIL : car(args);
			goto tail;
		}
		if (is_sym(op, "when")) {
			if (!truthy(eval(car(args), e))) return NIL;
			args = cdr(args);
			while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
			x = is_nil(args) ? NIL : car(args);
			goto tail;
		}
		if (is_sym(op, "unless")) {
			if (truthy(eval(car(args), e))) return NIL;
			args = cdr(args);
			while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
			x = is_nil(args) ? NIL : car(args);
			goto tail;
		}
		if (is_sym(op, "and")) {
			Val *r = TRUE;
			for (; !is_nil(args); args = cdr(args)) if (!truthy((r = eval(car(args), e)))) return FALSE;
			return r;
		}
		if (is_sym(op, "or")) {
			for (; !is_nil(args); args = cdr(args)) { Val *r = eval(car(args), e); if (truthy(r)) return r; }
			return FALSE;
		}
		if (is_sym(op, "cond")) {
			for (; !is_nil(args); args = cdr(args)) {
				Val *c = car(args), *test;
				if (is_nil(c)) die("empty cond clause");
				test = is_sym(car(c), "else") ? TRUE : eval(car(c), e);
				if (truthy(test)) {
					if (!is_nil(cdr(c)) && is_sym(cadr(c), "=>")) {
						if (len(c) != 3) die("bad cond => clause");
						return apply(eval(caddr(c), e), cons(test, NIL), e);
					}
					args = cdr(c);
					if (is_nil(args)) return TRUE;
					while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
					x = car(args);
					goto tail;
				}
			}
			return NIL;
		}
		if (is_sym(op, "try")) {
			return eval_try(args, e);
		}
	}
	Val *fn = eval(op, e);
	Val *actuals = eval_list(args, e);
	if (fn->t == T_NATIVE) return fn->as.native(actuals, e);
	e = bind_closure(fn, actuals);
	args = fn->as.clo.body;
	while (!is_nil(cdr(args))) { eval(car(args), e); args = cdr(args); }
	x = is_nil(args) ? NIL : car(args);
	goto tail;
}

static Val *eval(Val *x, Env *e)
{
	gc_push(&x);
	Val *r = eval_inner(x, e);
	gc_pop(1);
	return r;
}

static double need_num(Val *v)
{
	if (v->t != T_NUM) die("expected number");
	return v->as.n;
}

static char *need_str(Val *v)
{
	if (v->t != T_STR) die("expected string");
	return v->as.s;
}

static Val *need_port(Val *v)
{
	if (v->t != T_PORT || !v->as.port.f) die("expected open port");
	return v;
}

static Val *need_input_port(Val *v)
{
	need_port(v);
	if (!v->as.port.input) die("expected input port");
	return v;
}

static Val *need_output_port(Val *v)
{
	need_port(v);
	if (!v->as.port.output) die("expected output port");
	return v;
}

static int need_int(Val *v)
{
	return (int)need_num(v);
}

static Val *b_add(Val *a, Env *e) { (void)e; double r = 0; for (; !is_nil(a); a = cdr(a)) r += need_num(car(a)); return num(r); }
static Val *b_sub(Val *a, Env *e) { (void)e; if (is_nil(a)) die("- needs args"); double r = need_num(car(a)); a = cdr(a); if (is_nil(a)) return num(-r); for (; !is_nil(a); a = cdr(a)) r -= need_num(car(a)); return num(r); }
static Val *b_mul(Val *a, Env *e) { (void)e; double r = 1; for (; !is_nil(a); a = cdr(a)) r *= need_num(car(a)); return num(r); }
static Val *b_div(Val *a, Env *e) { (void)e; if (is_nil(a)) die("/ needs args"); double r = need_num(car(a)); for (a = cdr(a); !is_nil(a); a = cdr(a)) r /= need_num(car(a)); return num(r); }
static Val *b_abs(Val *a, Env *e) { (void)e; if (len(a) != 1) die("abs arity"); return num(fabs(need_num(car(a)))); }
static Val *b_floor(Val *a, Env *e) { (void)e; if (len(a) != 1) die("floor arity"); return num(floor(need_num(car(a)))); }
static Val *b_ceiling(Val *a, Env *e) { (void)e; if (len(a) != 1) die("ceiling arity"); return num(ceil(need_num(car(a)))); }
static Val *b_round(Val *a, Env *e) { (void)e; if (len(a) != 1) die("round arity"); return num(round(need_num(car(a)))); }
static Val *b_truncate(Val *a, Env *e) { (void)e; if (len(a) != 1) die("truncate arity"); return num(trunc(need_num(car(a)))); }
static Val *b_sqrt(Val *a, Env *e) { (void)e; if (len(a) != 1) die("sqrt arity"); return num(sqrt(need_num(car(a)))); }
static Val *b_expt(Val *a, Env *e) { (void)e; if (len(a) != 2) die("expt arity"); return num(pow(need_num(car(a)), need_num(cadr(a)))); }
static Val *b_sin(Val *a, Env *e) { (void)e; if (len(a) != 1) die("sin arity"); return num(sin(need_num(car(a)))); }
static Val *b_cos(Val *a, Env *e) { (void)e; if (len(a) != 1) die("cos arity"); return num(cos(need_num(car(a)))); }
static Val *b_tan(Val *a, Env *e) { (void)e; if (len(a) != 1) die("tan arity"); return num(tan(need_num(car(a)))); }
static Val *b_exp(Val *a, Env *e) { (void)e; if (len(a) != 1) die("exp arity"); return num(exp(need_num(car(a)))); }
static Val *b_log(Val *a, Env *e)
{
	(void)e;
	if (len(a) == 1) return num(log(need_num(car(a))));
	if (len(a) == 2) return num(log(need_num(car(a))) / log(need_num(cadr(a))));
	die("log arity");
	return NIL;
}
static Val *b_asin(Val *a, Env *e) { (void)e; if (len(a) != 1) die("asin arity"); return num(asin(need_num(car(a)))); }
static Val *b_acos(Val *a, Env *e) { (void)e; if (len(a) != 1) die("acos arity"); return num(acos(need_num(car(a)))); }

static Val *b_atan(Val *a, Env *e)
{
	(void)e;
	if (len(a) == 1) return num(atan(need_num(car(a))));
	if (len(a) == 2) return num(atan2(need_num(car(a)), need_num(cadr(a))));
	die("atan arity");
	return NIL;
}

static Val *b_min(Val *a, Env *e)
{
	(void)e;
	if (is_nil(a)) die("min needs args");
	double r = need_num(car(a));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		double n = need_num(car(a));
		if (n < r) r = n;
	}
	return num(r);
}

static Val *b_max(Val *a, Env *e)
{
	(void)e;
	if (is_nil(a)) die("max needs args");
	double r = need_num(car(a));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		double n = need_num(car(a));
		if (n > r) r = n;
	}
	return num(r);
}

static Val *b_modulo(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("modulo arity");
	long x = (long)need_num(car(a));
	long y = (long)need_num(cadr(a));
	if (y == 0) die("modulo by zero");
	long r = x % y;
	if ((r < 0 && y > 0) || (r > 0 && y < 0)) r += y;
	return num((double)r);
}

static Val *b_remainder(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("remainder arity");
	long x = (long)need_num(car(a));
	long y = (long)need_num(cadr(a));
	if (y == 0) die("remainder by zero");
	return num((double)(x % y));
}

static Val *b_gcd(Val *a, Env *e)
{
	(void)e;
	long result = 0;
	for (; !is_nil(a); a = cdr(a)) {
		long x = (long)need_num(car(a));
		if (x < 0) x = -x;
		while (x != 0) { long t = result % x; result = x; x = t; }
	}
	return num((double)result);
}

static Val *b_lcm(Val *a, Env *e)
{
	(void)e;
	long result = 1;
	if (is_nil(a)) return num(1);
	for (; !is_nil(a); a = cdr(a)) {
		long x = (long)need_num(car(a));
		if (x == 0) return num(0);
		if (x < 0) x = -x;
		long g = result, y = x;
		while (y != 0) { long t = g % y; g = y; y = t; }
		result = (result / g) * x;
	}
	return num((double)result);
}

static Val *b_finitep(Val *a, Env *e) { (void)e; if (len(a) != 1) die("finite? arity"); return boolean(isfinite(need_num(car(a)))); }
static Val *b_infinitep(Val *a, Env *e) { (void)e; if (len(a) != 1) die("infinite? arity"); return boolean(isinf(need_num(car(a)))); }
static Val *b_nanp(Val *a, Env *e) { (void)e; if (len(a) != 1) die("nan? arity"); return boolean(isnan(need_num(car(a)))); }

static Val *b_zerop(Val *a, Env *e) { (void)e; if (len(a) != 1) die("zero? arity"); return boolean(need_num(car(a)) == 0); }
static Val *b_positivep(Val *a, Env *e) { (void)e; if (len(a) != 1) die("positive? arity"); return boolean(need_num(car(a)) > 0); }
static Val *b_negativep(Val *a, Env *e) { (void)e; if (len(a) != 1) die("negative? arity"); return boolean(need_num(car(a)) < 0); }
static Val *b_oddp(Val *a, Env *e) { (void)e; if (len(a) != 1) die("odd? arity"); return boolean(labs((long)need_num(car(a))) % 2 == 1); }
static Val *b_evenp(Val *a, Env *e) { (void)e; if (len(a) != 1) die("even? arity"); return boolean(labs((long)need_num(car(a))) % 2 == 0); }

static Val *cmp(Val *a, int op)
{
	if (len(a) < 2) return TRUE;
	double x = need_num(car(a));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		double y = need_num(car(a));
		int ok = op == '=' ? x == y : op == '<' ? x < y : op == '>' ? x > y : op == 'l' ? x <= y : x >= y;
		if (!ok) return FALSE;
		x = y;
	}
	return TRUE;
}

static Val *b_eqn(Val *a, Env *e) { (void)e; return cmp(a, '='); }
static Val *b_lt(Val *a, Env *e) { (void)e; return cmp(a, '<'); }
static Val *b_gt(Val *a, Env *e) { (void)e; return cmp(a, '>'); }
static Val *b_le(Val *a, Env *e) { (void)e; return cmp(a, 'l'); }
static Val *b_ge(Val *a, Env *e) { (void)e; return cmp(a, 'g'); }

static Val *b_bitor(Val *a, Env *e)
{
	(void)e;
	long r = 0;
	for (; !is_nil(a); a = cdr(a)) r |= (long)need_num(car(a));
	return num((double)r);
}

static Val *b_bitand(Val *a, Env *e)
{
	(void)e;
	if (is_nil(a)) die("bitwise-and needs args");
	long r = (long)need_num(car(a));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) r &= (long)need_num(car(a));
	return num((double)r);
}

static Val *b_cons(Val *a, Env *e) { (void)e; if (len(a) != 2) die("cons arity"); return cons(car(a), cadr(a)); }
static Val *b_car(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR) die("car arity/type"); return car(car(a)); }
static Val *b_cdr(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR) die("cdr arity/type"); return cdr(car(a)); }
static Val *b_list(Val *a, Env *e) { (void)e; return a; }
static Val *b_caar(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR || car(car(a))->t != T_PAIR) die("caar type"); return car(car(car(a))); }
static Val *b_cdar(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR || car(car(a))->t != T_PAIR) die("cdar type"); return cdr(car(car(a))); }
static Val *b_cadr(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR || cdr(car(a))->t != T_PAIR) die("cadr type"); return cadr(car(a)); }
static Val *b_cddr(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR || cdr(car(a))->t != T_PAIR) die("cddr type"); return cddr(car(a)); }
static Val *b_caddr(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR || cddr(car(a))->t != T_PAIR) die("caddr type"); return caddr(car(a)); }
static Val *b_cadddr(Val *a, Env *e) { (void)e; if (len(a) != 1 || car(a)->t != T_PAIR || cdr(cddr(car(a)))->t != T_PAIR) die("cadddr type"); return cadddr(car(a)); }
static Val *b_nullp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && is_nil(car(a))); }
static Val *b_pairp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_PAIR); }
static Val *b_listp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && proper_list(car(a))); }
static Val *b_symbolp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_SYM); }
static Val *b_stringp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_STR); }
static Val *b_numberp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_NUM); }
static Val *b_booleanp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_BOOL); }
static Val *b_vectorp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_VECTOR); }
static Val *b_eofp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_EOF); }
static Val *b_not(Val *a, Env *e) { (void)e; if (len(a) != 1) die("not arity"); return boolean(!truthy(car(a))); }

static Val *b_boolean_eq(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 2) return TRUE;
	if (car(a)->t != T_BOOL) die("boolean=? expected boolean");
	int wanted = car(a)->as.b;
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		if (car(a)->t != T_BOOL) die("boolean=? expected boolean");
		if (car(a)->as.b != wanted) return FALSE;
	}
	return TRUE;
}

static Val *b_length(Val *a, Env *e) { (void)e; if (len(a) != 1) die("length arity"); return num((double)len(car(a))); }

static Val *b_vector(Val *a, Env *e)
{
	(void)e;
	return list_to_vector_value(a);
}

static Val *b_make_vector(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 2) die("make-vector arity");
	int n = need_int(car(a));
	Val *fill = is_nil(cdr(a)) ? NIL : cadr(a);
	return vector_new(n, fill);
}

static Val *b_vector_length(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1 || car(a)->t != T_VECTOR) die("vector-length arity/type");
	return num((double)car(a)->as.vec.len);
}

static Val *b_vector_ref(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2 || car(a)->t != T_VECTOR) die("vector-ref arity/type");
	Val *v = car(a);
	int i = need_int(cadr(a));
	if (i < 0 || i >= v->as.vec.len) die("vector-ref range");
	return v->as.vec.items[i];
}

static Val *b_vector_set(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3 || car(a)->t != T_VECTOR) die("vector-set! arity/type");
	Val *v = car(a);
	int i = need_int(cadr(a));
	if (i < 0 || i >= v->as.vec.len) die("vector-set! range");
	v->as.vec.items[i] = caddr(a);
	return NIL;
}

static Val *b_vector_fill(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2 || car(a)->t != T_VECTOR) die("vector-fill! arity/type");
	Val *v = car(a);
	for (int i = 0; i < v->as.vec.len; i++) v->as.vec.items[i] = cadr(a);
	return NIL;
}

static Val *b_list_to_vector(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("list->vector arity");
	return list_to_vector_value(car(a));
}

static Val *b_vector_to_list(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("vector->list arity");
	return vector_to_list_value(car(a));
}

static Val *b_vector_copy(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 3 || car(a)->t != T_VECTOR) die("vector-copy arity/type");
	Val *src = car(a);
	int n = src->as.vec.len;
	int start = len(a) >= 2 ? need_int(cadr(a)) : 0;
	int end = len(a) == 3 ? need_int(caddr(a)) : n;
	if (start < 0 || end < start || end > n) die("vector-copy range");
	Val *r = vector_new(end - start, NIL);
	for (int i = start; i < end; i++) r->as.vec.items[i - start] = src->as.vec.items[i];
	return r;
}

static Val *b_vector_copy_in(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 3 || len(a) > 5 || car(a)->t != T_VECTOR || caddr(a)->t != T_VECTOR)
		die("vector-copy! arity/type");
	Val *to = car(a), *from = caddr(a);
	int at = need_int(cadr(a));
	int start = len(a) >= 4 ? need_int(car(cdr(cdr(cdr(a))))) : 0;
	int end = len(a) == 5 ? need_int(car(cdr(cdr(cdr(cdr(a)))))) : from->as.vec.len;
	int count = end - start;
	if (at < 0 || start < 0 || end < start || end > from->as.vec.len || at + count > to->as.vec.len)
		die("vector-copy! range");
	if (count > 0) memmove(to->as.vec.items + at, from->as.vec.items + start, sizeof(Val *) * (size_t)count);
	return NIL;
}

static Val *b_vector_append(Val *a, Env *e)
{
	(void)e;
	int total = 0;
	for (Val *p = a; !is_nil(p); p = cdr(p)) {
		if (car(p)->t != T_VECTOR) die("vector-append expected vector");
		total += car(p)->as.vec.len;
	}
	Val *r = vector_new(total, NIL);
	int at = 0;
	for (Val *p = a; !is_nil(p); p = cdr(p)) {
		Val *v = car(p);
		for (int i = 0; i < v->as.vec.len; i++) r->as.vec.items[at++] = v->as.vec.items[i];
	}
	return r;
}

static Val *b_list_tail(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("list-tail arity");
	Val *p = car(a);
	int n = need_int(cadr(a));
	if (n < 0) die("list-tail range");
	while (n-- > 0) {
		if (p->t != T_PAIR) die("list-tail range");
		p = cdr(p);
	}
	if (!is_nil(p) && p->t != T_PAIR) die("list-tail expected proper list");
	return p;
}

static Val *b_list_copy(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("list-copy arity");
	Val *src = car(a), *r = NIL, **tail = &r;
	for (; !is_nil(src); src = cdr(src)) {
		if (src->t != T_PAIR) die("list-copy expected proper list");
		*tail = cons(car(src), NIL);
		tail = &cdr(*tail);
	}
	return r;
}

static Val *b_gap_bufferp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_GAP); }

static Val *b_make_gap_buffer(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("make-gap-buffer arity");
	return gap_new(is_nil(a) ? "" : need_str(car(a)));
}

static Val *b_gap_buffer_length(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("gap-buffer-length arity");
	return num((double)gap_length(need_gap(car(a))));
}

static Val *b_gap_buffer_point(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("gap-buffer-point arity");
	return num((double)need_gap(car(a))->as.gap.gap_start);
}

static Val *b_gap_buffer_set_point(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("gap-buffer-set-point! arity");
	Val *g = need_gap(car(a));
	gap_move(g, need_int(cadr(a)));
	return NIL;
}

static Val *b_gap_buffer_to_string(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("gap-buffer->string arity");
	char *s = gap_to_cstr(need_gap(car(a)));
	Val *r = str(s);
	free(s);
	return r;
}

static Val *b_gap_buffer_ref(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("gap-buffer-ref arity");
	Val *g = need_gap(car(a));
	int p = gap_physical_index(g, need_int(cadr(a)));
	char buf[2] = { g->as.gap.buf[p], 0 };
	return str(buf);
}

static Val *b_gap_buffer_substring(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3) die("gap-buffer-substring arity");
	Val *g = need_gap(car(a));
	int start = need_int(cadr(a));
	int end = need_int(caddr(a));
	int n = gap_length(g);
	if (start < 0 || end < start || end > n) die("gap-buffer-substring range");
	char *s = xmalloc((size_t)(end - start) + 1);
	for (int i = start; i < end; i++) s[i - start] = g->as.gap.buf[gap_physical_index(g, i)];
	s[end - start] = 0;
	Val *r = str(s);
	free(s);
	return r;
}

static Val *b_gap_buffer_insert(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("gap-buffer-insert! arity");
	Val *g = need_gap(car(a));
	char *s = need_str(cadr(a));
	int n = (int)strlen(s);
	gap_ensure(g, n);
	memcpy(g->as.gap.buf + g->as.gap.gap_start, s, (size_t)n);
	g->as.gap.gap_start += n;
	return NIL;
}

static Val *b_gap_buffer_delete(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3) die("gap-buffer-delete! arity");
	Val *g = need_gap(car(a));
	int start = need_int(cadr(a));
	int end = need_int(caddr(a));
	int n = gap_length(g);
	if (start < 0 || end < start || end > n) die("gap-buffer-delete! range");
	gap_move(g, start);
	g->as.gap.gap_end += end - start;
	return NIL;
}

static Val *b_gap_buffer_line_count(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("gap-buffer-line-count arity");
	Val *g = need_gap(car(a));
	int n = gap_length(g), lines = 1;
	for (int i = 0; i < n; i++)
		if (gap_char_at(g, i) == '\n') lines++;
	return num((double)lines);
}

static Val *b_gap_buffer_line_number_at(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("gap-buffer-line-number-at arity");
	Val *g = need_gap(car(a));
	int idx = need_int(cadr(a));
	int n = gap_length(g), line = 0;
	if (idx < 0) idx = 0;
	if (idx > n) idx = n;
	for (int i = 0; i < idx; i++)
		if (gap_char_at(g, i) == '\n') line++;
	return num((double)line);
}

static Val *b_gap_buffer_line_start(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("gap-buffer-line-start arity");
	Val *g = need_gap(car(a));
	int want = need_int(cadr(a));
	if (want < 0) want = 0;
	int n = gap_length(g), line = 0, start = 0;
	while (line < want && start < n) {
		if (gap_char_at(g, start++) == '\n') line++;
	}
	return num((double)start);
}

static Val *b_gap_buffer_line_start_from(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("gap-buffer-line-start-from arity");
	Val *g = need_gap(car(a));
	int p = need_int(cadr(a));
	int n = gap_length(g);
	if (p < 0) p = 0;
	if (p > n) p = n;
	while (p > 0 && gap_char_at(g, p - 1) != '\n') p--;
	return num((double)p);
}

static Val *b_gap_buffer_line_end_from(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("gap-buffer-line-end-from arity");
	Val *g = need_gap(car(a));
	int p = need_int(cadr(a));
	int n = gap_length(g);
	if (p < 0) p = 0;
	if (p > n) p = n;
	while (p < n && gap_char_at(g, p) != '\n') p++;
	return num((double)p);
}

static int gap_match_at(Val *g, const char *needle, int pos, int needle_len)
{
	for (int i = 0; i < needle_len; i++)
		if (gap_char_at(g, pos + i) != (unsigned char)needle[i]) return 0;
	return 1;
}

static Val *b_gap_buffer_find_next(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3) die("gap-buffer-find-next arity");
	Val *g = need_gap(car(a));
	const char *needle = need_str(cadr(a));
	int start = need_int(caddr(a));
	int n = gap_length(g), m = (int)strlen(needle);
	if (m == 0) return num((double)(start < 0 ? 0 : start > n ? n : start));
	if (start < 0) start = 0;
	if (start > n) start = n;
	for (int i = start; i + m <= n; i++)
		if (gap_match_at(g, needle, i, m)) return num((double)i);
	return FALSE;
}

static Val *b_gap_buffer_find_prev(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3) die("gap-buffer-find-prev arity");
	Val *g = need_gap(car(a));
	const char *needle = need_str(cadr(a));
	int before = need_int(caddr(a));
	int n = gap_length(g), m = (int)strlen(needle);
	if (m == 0) return FALSE;
	if (before < 0) before = 0;
	if (before > n + 1) before = n + 1;
	int last = -1;
	for (int i = 0; i + m <= n && i < before; i++)
		if (gap_match_at(g, needle, i, m)) last = i;
	return last >= 0 ? num((double)last) : FALSE;
}

static Val *b_recordp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_RECORD); }

static Val *b_make_record(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("make-record arity");
	const char *type = field_name(car(a));
	Val *fields = cadr(a);
	int n = len(fields);
	Val *r = record_new(type, n);
	int i = 0;
	for (Val *p = fields; !is_nil(p); p = cdr(p), i++) {
		Val *entry = car(p);
		if (entry->t == T_PAIR) {
			r->as.rec.keys[i] = xstrdup(field_name(car(entry)));
			r->as.rec.vals[i] = cdr(entry);
		} else {
			r->as.rec.keys[i] = xstrdup(field_name(entry));
			r->as.rec.vals[i] = FALSE;
		}
	}
	return r;
}

static Val *b_record_type(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1 || car(a)->t != T_RECORD) die("record-type arity/type");
	return sym(car(a)->as.rec.type);
}

static Val *b_record_fields(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1 || car(a)->t != T_RECORD) die("record-fields arity/type");
	Val *r = NIL;
	Val *rec = car(a);
	for (int i = rec->as.rec.len - 1; i >= 0; i--) r = cons(sym(rec->as.rec.keys[i]), r);
	return r;
}

static Val *b_record_ref(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2 || car(a)->t != T_RECORD) die("record-ref arity/type");
	int i = record_field_index(car(a), field_name(cadr(a)));
	if (i < 0) die("unknown record field");
	return car(a)->as.rec.vals[i];
}

static Val *b_record_set(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3 || car(a)->t != T_RECORD) die("record-set! arity/type");
	int i = record_field_index(car(a), field_name(cadr(a)));
	if (i < 0) die("unknown record field");
	car(a)->as.rec.vals[i] = caddr(a);
	return NIL;
}

static Val *b_gc(Val *a, Env *e)
{
	if (!is_nil(a)) die("gc arity");
	return num((double)gc_collect(e));
}

static Val *b_gc_stats(Val *a, Env *e)
{
	(void)e;
	if (!is_nil(a)) die("gc-stats arity");
	return cons(cons(sym("heap"), cons(num((double)gc_heap_count), NIL)),
	       cons(cons(sym("last-freed"), cons(num((double)gc_last_freed), NIL)),
	       cons(cons(sym("roots"), cons(num((double)gc_root_count), NIL)), NIL)));
}

static Val *b_make_hash_table(Val *a, Env *e)
{
	(void)e;
	if (!is_nil(a)) die("make-hash-table arity");
	return hash_new();
}

static Val *b_hash_tablep(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_HASH); }

static Val *b_hash_set(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3) die("hash-set! arity");
	Val *h = need_hash(car(a));
	const char *key = hash_key_name(cadr(a));
	int i = hash_index(h, key);
	if (i < 0) {
		hash_ensure(h);
		i = h->as.hash.len++;
		h->as.hash.keys[i] = xstrdup(key);
	}
	h->as.hash.vals[i] = caddr(a);
	return NIL;
}

static Val *b_hash_ref(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 2 || len(a) > 3) die("hash-ref arity");
	Val *h = need_hash(car(a));
	int i = hash_index(h, hash_key_name(cadr(a)));
	if (i >= 0) return h->as.hash.vals[i];
	if (len(a) == 3) return caddr(a);
	die("hash-ref unknown key");
	return NIL;
}

static Val *b_hash_exists(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("hash-exists? arity");
	return boolean(hash_index(need_hash(car(a)), hash_key_name(cadr(a))) >= 0);
}

static Val *b_hash_delete(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("hash-delete! arity");
	Val *h = need_hash(car(a));
	int i = hash_index(h, hash_key_name(cadr(a)));
	if (i < 0) return FALSE;
	free(h->as.hash.keys[i]);
	for (int j = i + 1; j < h->as.hash.len; j++) {
		h->as.hash.keys[j - 1] = h->as.hash.keys[j];
		h->as.hash.vals[j - 1] = h->as.hash.vals[j];
	}
	h->as.hash.len--;
	h->as.hash.keys[h->as.hash.len] = NULL;
	h->as.hash.vals[h->as.hash.len] = NIL;
	return TRUE;
}

static Val *b_hash_keys(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("hash-keys arity");
	Val *h = need_hash(car(a));
	Val *r = NIL;
	for (int i = h->as.hash.len - 1; i >= 0; i--) r = cons(str(h->as.hash.keys[i]), r);
	return r;
}

static Val *b_hash_count(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("hash-count arity");
	return num((double)need_hash(car(a))->as.hash.len);
}

static Val *b_reverse(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("reverse arity");
	Val *r = NIL;
	for (Val *p = car(a); !is_nil(p); p = cdr(p)) r = cons(car(p), r);
	return r;
}

static Val *b_map(Val *a, Env *e)
{
	if (len(a) < 2) die("map arity");
	Val *fn = car(a);
	Val *lists = cdr(a);
	Val *r = NIL, **tail = &r;

	for (;;) {
		int done = 0;
		Val *call_args = NIL, **arg_tail = &call_args;
		for (Val *lp = lists; !is_nil(lp); lp = cdr(lp)) {
			Val *xs = car(lp);
			if (is_nil(xs)) {
				done = 1;
				continue;
			}
			if (xs->t != T_PAIR) die("map expected proper list");
			if (done) die("map expected lists of same length");
			*arg_tail = cons(car(xs), NIL);
			arg_tail = &cdr(*arg_tail);
		}
		if (done) {
			for (Val *lp = lists; !is_nil(lp); lp = cdr(lp))
				if (!is_nil(car(lp))) die("map expected lists of same length");
			return r;
		}
		*tail = cons(apply(fn, call_args, e), NIL);
		tail = &cdr(*tail);
		for (Val *lp = lists; !is_nil(lp); lp = cdr(lp))
			car(lp) = cdr(car(lp));
	}
}

static Val *b_foreach(Val *a, Env *e)
{
	if (len(a) < 2) die("for-each arity");
	Val *fn = car(a);
	Val *lists = cdr(a);

	for (;;) {
		int done = 0;
		Val *call_args = NIL, **arg_tail = &call_args;
		for (Val *lp = lists; !is_nil(lp); lp = cdr(lp)) {
			Val *xs = car(lp);
			if (is_nil(xs)) {
				done = 1;
				continue;
			}
			if (xs->t != T_PAIR) die("for-each expected proper list");
			if (done) die("for-each expected lists of same length");
			*arg_tail = cons(car(xs), NIL);
			arg_tail = &cdr(*arg_tail);
		}
		if (done) {
			for (Val *lp = lists; !is_nil(lp); lp = cdr(lp))
				if (!is_nil(car(lp))) die("for-each expected lists of same length");
			return NIL;
		}
		apply(fn, call_args, e);
		for (Val *lp = lists; !is_nil(lp); lp = cdr(lp))
			car(lp) = cdr(car(lp));
	}
}

static Val *b_append(Val *a, Env *e)
{
	(void)e;
	Val *r = NIL, **tail = &r;
	for (; !is_nil(a); a = cdr(a)) {
		Val *xs = car(a);
		if (is_nil(cdr(a))) {
			*tail = xs;
			break;
		}
		for (; !is_nil(xs); xs = cdr(xs)) {
			if (xs->t != T_PAIR) die("append expected proper list before final argument");
			*tail = cons(car(xs), NIL);
			tail = &cdr(*tail);
		}
	}
	return r;
}

static Val *b_listref(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("list-ref arity");
	Val *p = car(a);
	int n = (int)need_num(cadr(a));
	while (n-- > 0) {
		if (p->t != T_PAIR) die("list-ref range");
		p = cdr(p);
	}
	if (p->t != T_PAIR) die("list-ref range");
	return car(p);
}

static Val *b_plistref(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 2 || len(a) > 3) die("plist-ref arity");
	Val *xs = car(a);
	Val *key = cadr(a);
	const char *want;
	if (key->t == T_SYM || key->t == T_STR) want = key->as.s;
	else die("plist-ref key type");

	for (; !is_nil(xs); xs = cdr(cdr(xs))) {
		if (xs->t != T_PAIR || cdr(xs)->t != T_PAIR) die("plist-ref expected property list");
		Val *k = car(xs);
		if ((k->t == T_SYM || k->t == T_STR) && strcmp(k->as.s, want) == 0) return cadr(xs);
	}
	return len(a) == 3 ? caddr(a) : FALSE;
}

static int same_val(Val *x, Val *y, int structural)
{
	if (x == y) return 1;
	if (x->t != y->t) return 0;
	switch (x->t) {
	case T_BOOL: return x->as.b == y->as.b;
	case T_NUM: return x->as.n == y->as.n;
	case T_STR:
	case T_SYM: return strcmp(x->as.s, y->as.s) == 0;
	case T_PAIR:
		return structural && same_val(car(x), car(y), 1) && same_val(cdr(x), cdr(y), 1);
	case T_VECTOR:
		if (!structural || x->as.vec.len != y->as.vec.len) return 0;
		for (int i = 0; i < x->as.vec.len; i++)
			if (!same_val(x->as.vec.items[i], y->as.vec.items[i], 1)) return 0;
		return 1;
	case T_RECORD:
		if (!structural || strcmp(x->as.rec.type, y->as.rec.type) != 0 ||
		    x->as.rec.len != y->as.rec.len) return 0;
		for (int i = 0; i < x->as.rec.len; i++) {
			if (strcmp(x->as.rec.keys[i], y->as.rec.keys[i]) != 0) return 0;
			if (!same_val(x->as.rec.vals[i], y->as.rec.vals[i], 1)) return 0;
		}
		return 1;
	case T_GAP: {
		if (!structural || gap_length(x) != gap_length(y)) return 0;
		char *xs = gap_to_cstr(x);
		char *ys = gap_to_cstr(y);
		int ok = strcmp(xs, ys) == 0;
		free(xs);
		free(ys);
		return ok;
	}
	case T_HASH:
		if (!structural || x->as.hash.len != y->as.hash.len) return 0;
		for (int i = 0; i < x->as.hash.len; i++) {
			int j = hash_index(y, x->as.hash.keys[i]);
			if (j < 0) return 0;
			if (!same_val(x->as.hash.vals[i], y->as.hash.vals[j], 1)) return 0;
		}
		return 1;
	default:
		return 0;
	}
}

static Val *member_by(Val *a, int structural)
{
	if (len(a) != 2) die("member arity");
	Val *needle = car(a);
	for (Val *p = cadr(a); !is_nil(p); p = cdr(p)) {
		if (p->t != T_PAIR) die("member expected proper list");
		if (same_val(car(p), needle, structural)) return p;
	}
	return FALSE;
}

static Val *b_memq(Val *a, Env *e) { (void)e; return member_by(a, 0); }
static Val *b_memv(Val *a, Env *e) { (void)e; return member_by(a, 0); }
static Val *b_member(Val *a, Env *e) { (void)e; return member_by(a, 1); }

static Val *assoc_by(Val *a, int structural)
{
	if (len(a) != 2) die("assoc arity");
	Val *needle = car(a);
	for (Val *p = cadr(a); !is_nil(p); p = cdr(p)) {
		if (p->t != T_PAIR || car(p)->t != T_PAIR) die("assoc expected alist");
		if (same_val(car(car(p)), needle, structural)) return car(p);
	}
	return FALSE;
}

static Val *b_assq(Val *a, Env *e) { (void)e; return assoc_by(a, 0); }
static Val *b_assv(Val *a, Env *e) { (void)e; return assoc_by(a, 0); }
static Val *b_assoc(Val *a, Env *e) { (void)e; return assoc_by(a, 1); }

static Val *b_eqp(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("eq? arity");
	return boolean(same_val(car(a), cadr(a), 0));
}

static Val *b_equalp(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("equal? arity");
	return boolean(same_val(car(a), cadr(a), 1));
}

static Val *b_display(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 2) die("display arity");
	Val *out = len(a) == 2 ? need_output_port(cadr(a)) : STDOUT_PORT;
	if (car(a)->t == T_STR) port_puts(out, car(a)->as.s);
	else print_val_port(out, car(a));
	if (out == STDOUT_PORT && swamp_tb_active) tb_present();
	return NIL;
}

static Val *b_newline(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("newline arity");
	Val *out = is_nil(a) ? STDOUT_PORT : need_output_port(car(a));
	port_putc(out, '\n');
	if (out == STDOUT_PORT && swamp_tb_active) tb_present();
	return NIL;
}

static unsigned char need_char_byte(Val *v);

static Val *b_strlen(Val *a, Env *e) { (void)e; if (len(a) != 1) die("string-length arity"); return num((double)strlen(need_str(car(a)))); }

static Val *b_make_string(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 2) die("make-string arity");
	int n = need_int(car(a));
	if (n < 0) die("make-string length");
	unsigned char fill = ' ';
	if (len(a) == 2) fill = need_char_byte(cadr(a));
	char *buf = xmalloc((size_t)n + 1);
	memset(buf, fill, (size_t)n);
	buf[n] = 0;
	Val *r = str(buf);
	free(buf);
	return r;
}

static Val *b_strappend(Val *a, Env *e)
{
	(void)e;
	size_t n = 0;
	for (Val *p = a; !is_nil(p); p = cdr(p)) n += strlen(need_str(car(p)));
	char *buf = xmalloc(n + 1);
	buf[0] = 0;
	for (; !is_nil(a); a = cdr(a)) strcat(buf, need_str(car(a)));
	Val *r = str(buf);
	free(buf);
	return r;
}

static Val *b_symbol_to_string(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1 || car(a)->t != T_SYM) die("symbol->string arity/type");
	return str(car(a)->as.s);
}

static Val *b_string_to_symbol(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("string->symbol arity");
	return sym(need_str(car(a)));
}

static Val *b_string_copy(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 3) die("string-copy arity");
	char *s = need_str(car(a));
	int n = (int)strlen(s);
	int start = len(a) >= 2 ? need_int(cadr(a)) : 0;
	int end = len(a) == 3 ? need_int(caddr(a)) : n;
	if (start < 0 || end < start || end > n) die("string-copy range");
	char *buf = xmalloc((size_t)(end - start) + 1);
	memcpy(buf, s + start, (size_t)(end - start));
	buf[end - start] = 0;
	Val *r = str(buf);
	free(buf);
	return r;
}

static Val *b_string_copy_in(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 3 || len(a) > 5) die("string-copy! arity");
	char *to = need_str(car(a));
	int at = need_int(cadr(a));
	char *from = need_str(caddr(a));
	int source_len = (int)strlen(from);
	int start = len(a) >= 4 ? need_int(car(cdr(cdr(cdr(a))))) : 0;
	int end = len(a) == 5 ? need_int(car(cdr(cdr(cdr(cdr(a)))))) : source_len;
	int count = end - start;
	if (at < 0 || start < 0 || end < start || end > source_len || at + count > (int)strlen(to))
		die("string-copy! range");
	if (count > 0) memmove(to + at, from + start, (size_t)count);
	return NIL;
}

static Val *b_string_fill(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 2 || len(a) > 4) die("string-fill! arity");
	char *s = need_str(car(a));
	int n = (int)strlen(s);
	int start = len(a) >= 3 ? need_int(caddr(a)) : 0;
	int end = len(a) == 4 ? need_int(car(cdr(cdr(cdr(a))))) : n;
	unsigned char fill = need_char_byte(cadr(a));
	if (start < 0 || end < start || end > n) die("string-fill! range");
	memset(s + start, fill, (size_t)(end - start));
	return NIL;
}

static Val *b_substring(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 3) die("substring arity");
	char *s = need_str(car(a));
	int start = need_int(cadr(a));
	int end = need_int(caddr(a));
	int n = (int)strlen(s);
	if (start < 0 || end < start || end > n) die("substring range");
	char *buf = xmalloc((size_t)(end - start) + 1);
	memcpy(buf, s + start, (size_t)(end - start));
	buf[end - start] = 0;
	Val *r = str(buf);
	free(buf);
	return r;
}

static Val *b_stringref(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("string-ref arity");
	char *s = need_str(car(a));
	int i = need_int(cadr(a));
	int n = (int)strlen(s);
	if (i < 0 || i >= n) die("string-ref range");
	char buf[2] = { s[i], 0 };
	return str(buf);
}

static Val *string_cmp(Val *a, int op)
{
	if (len(a) < 2) return TRUE;
	char *x = need_str(car(a));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		char *y = need_str(car(a));
		int c = strcmp(x, y);
		int ok = op == '=' ? c == 0 : op == '<' ? c < 0 : op == '>' ? c > 0 : op == 'l' ? c <= 0 : c >= 0;
		if (!ok) return FALSE;
		x = y;
	}
	return TRUE;
}

static Val *b_streq(Val *a, Env *e) { (void)e; return string_cmp(a, '='); }
static Val *b_strlt(Val *a, Env *e) { (void)e; return string_cmp(a, '<'); }
static Val *b_strgt(Val *a, Env *e) { (void)e; return string_cmp(a, '>'); }
static Val *b_strle(Val *a, Env *e) { (void)e; return string_cmp(a, 'l'); }
static Val *b_strge(Val *a, Env *e) { (void)e; return string_cmp(a, 'g'); }

static Val *string_ci_cmp(Val *a, int op)
{
	if (len(a) < 2) return TRUE;
	char *x = need_str(car(a));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		char *y = need_str(car(a));
		const unsigned char *px = (const unsigned char *)x;
		const unsigned char *py = (const unsigned char *)y;
		int c = 0;
		while (*px && *py) {
			int cx = tolower(*px++), cy = tolower(*py++);
			if (cx != cy) { c = cx < cy ? -1 : 1; break; }
		}
		if (!c && (*px || *py)) c = *px ? 1 : -1;
		int ok = op == '=' ? c == 0 : op == '<' ? c < 0 : op == '>' ? c > 0 : op == 'l' ? c <= 0 : c >= 0;
		if (!ok) return FALSE;
		x = y;
	}
	return TRUE;
}

static Val *b_strcieq(Val *a, Env *e) { (void)e; return string_ci_cmp(a, '='); }
static Val *b_strcilt(Val *a, Env *e) { (void)e; return string_ci_cmp(a, '<'); }
static Val *b_strcigt(Val *a, Env *e) { (void)e; return string_ci_cmp(a, '>'); }
static Val *b_strcile(Val *a, Env *e) { (void)e; return string_ci_cmp(a, 'l'); }
static Val *b_strcige(Val *a, Env *e) { (void)e; return string_ci_cmp(a, 'g'); }

static Val *b_charint(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("integer->char arity");
	uint32_t ch = (uint32_t)need_int(car(a));
	char buf[8];
	int n = tb_utf8_unicode_to_char(buf, ch);
	if (n < 1 || n >= (int)sizeof(buf)) die("integer->char range");
	buf[n] = 0;
	return str(buf);
}

static Val *b_intchar(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("char->integer arity");
	char *s = need_str(car(a));
	uint32_t ch = 0;
	if (tb_utf8_char_to_unicode(&ch, s) < 1) die("bad character");
	return num((double)ch);
}

static int charish(Val *v)
{
	return v->t == T_STR && strlen(v->as.s) == 1;
}

static unsigned char need_char_byte(Val *v)
{
	if (!charish(v)) die("expected character");
	return (unsigned char)v->as.s[0];
}

static Val *b_charp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && charish(car(a))); }

static Val *char_cmp(Val *a, int op)
{
	if (len(a) < 2) return TRUE;
	unsigned char x = need_char_byte(car(a));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		unsigned char y = need_char_byte(car(a));
		int ok = op == '=' ? x == y : op == '<' ? x < y : op == '>' ? x > y : op == 'l' ? x <= y : x >= y;
		if (!ok) return FALSE;
		x = y;
	}
	return TRUE;
}

static Val *b_chareq(Val *a, Env *e) { (void)e; return char_cmp(a, '='); }
static Val *b_charlt(Val *a, Env *e) { (void)e; return char_cmp(a, '<'); }
static Val *b_chargt(Val *a, Env *e) { (void)e; return char_cmp(a, '>'); }
static Val *b_charle(Val *a, Env *e) { (void)e; return char_cmp(a, 'l'); }
static Val *b_charge(Val *a, Env *e) { (void)e; return char_cmp(a, 'g'); }

static Val *char_ci_cmp(Val *a, int op)
{
	if (len(a) < 2) return TRUE;
	unsigned char x = (unsigned char)tolower(need_char_byte(car(a)));
	for (a = cdr(a); !is_nil(a); a = cdr(a)) {
		unsigned char y = (unsigned char)tolower(need_char_byte(car(a)));
		int ok = op == '=' ? x == y : op == '<' ? x < y : op == '>' ? x > y : op == 'l' ? x <= y : x >= y;
		if (!ok) return FALSE;
		x = y;
	}
	return TRUE;
}

static Val *b_charcieq(Val *a, Env *e) { (void)e; return char_ci_cmp(a, '='); }
static Val *b_charcilt(Val *a, Env *e) { (void)e; return char_ci_cmp(a, '<'); }
static Val *b_charcigt(Val *a, Env *e) { (void)e; return char_ci_cmp(a, '>'); }
static Val *b_charcile(Val *a, Env *e) { (void)e; return char_ci_cmp(a, 'l'); }
static Val *b_charcige(Val *a, Env *e) { (void)e; return char_ci_cmp(a, 'g'); }

static Val *b_char_alphabeticp(Val *a, Env *e) { (void)e; if (len(a) != 1) die("char-alphabetic? arity"); return boolean(isalpha(need_char_byte(car(a)))); }
static Val *b_char_numericp(Val *a, Env *e) { (void)e; if (len(a) != 1) die("char-numeric? arity"); return boolean(isdigit(need_char_byte(car(a)))); }
static Val *b_char_whitespacep(Val *a, Env *e) { (void)e; if (len(a) != 1) die("char-whitespace? arity"); return boolean(isspace(need_char_byte(car(a)))); }
static Val *b_char_uppercasep(Val *a, Env *e) { (void)e; if (len(a) != 1) die("char-upper-case? arity"); return boolean(isupper(need_char_byte(car(a)))); }
static Val *b_char_lowercasep(Val *a, Env *e) { (void)e; if (len(a) != 1) die("char-lower-case? arity"); return boolean(islower(need_char_byte(car(a)))); }
static Val *b_char_upcase(Val *a, Env *e) { (void)e; if (len(a) != 1) die("char-upcase arity"); char buf[2] = { (char)toupper(need_char_byte(car(a))), 0 }; return str(buf); }
static Val *b_char_downcase(Val *a, Env *e) { (void)e; if (len(a) != 1) die("char-downcase arity"); char buf[2] = { (char)tolower(need_char_byte(car(a))), 0 }; return str(buf); }

static Val *b_strdown(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("string-downcase arity");
	char *s = need_str(car(a)), *buf = xstrdup(s);
	for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
	Val *r = str(buf);
	free(buf);
	return r;
}

static Val *b_strtrim(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("string-trim-both arity");
	char *s = need_str(car(a));
	while (isspace((unsigned char)*s)) s++;
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1])) n--;
	char *buf = xmalloc(n + 1);
	memcpy(buf, s, n); buf[n] = 0;
	Val *r = str(buf);
	free(buf);
	return r;
}

static Val *b_strcontains(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("string-contains arity");
	char *p = strstr(need_str(car(a)), need_str(cadr(a)));
	return p ? num((double)(p - need_str(car(a)))) : FALSE;
}

static Val *b_strjoin(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("string-join arity");
	Val *xs = car(a);
	char *sep = need_str(cadr(a));
	size_t n = 0, sepn = strlen(sep);
	int count = 0;
	for (Val *p = xs; !is_nil(p); p = cdr(p), count++) n += strlen(need_str(car(p)));
	if (count > 1) n += sepn * (size_t)(count - 1);
	char *buf = xmalloc(n + 1); buf[0] = 0;
	for (Val *p = xs; !is_nil(p); p = cdr(p)) {
		if (p != xs) strcat(buf, sep);
		strcat(buf, need_str(car(p)));
	}
	Val *r = str(buf);
	free(buf);
	return r;
}

static Val *b_tokenize(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 2) die("string-tokenize arity");
	char *s = need_str(car(a));
	Val *head = NIL, **tail = &head;
	for (size_t i = 0; s[i];) {
		while (s[i] && !isalnum((unsigned char)s[i])) i++;
		size_t start = i;
		while (s[i] && isalnum((unsigned char)s[i])) i++;
		if (i > start) {
			char *buf = xmalloc(i - start + 1);
			memcpy(buf, s + start, i - start); buf[i - start] = 0;
			*tail = cons(str(buf), NIL); tail = &cdr(*tail);
			free(buf);
		}
	}
	return head;
}

static Val *b_numstr(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("number->string arity");
	char buf[64];
	double n = need_num(car(a));
	if (fabs(n - (long long)n) < 0.000000001) snprintf(buf, sizeof(buf), "%lld", (long long)n);
	else snprintf(buf, sizeof(buf), "%.15g", n);
	return str(buf);
}

static Val *b_strnum(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("string->number arity");
	char *s = need_str(car(a));
	char *end = NULL;
	errno = 0;
	double n = strtod(s, &end);
	if (errno || end == s) return FALSE;
	while (*end) {
		if (!isspace((unsigned char)*end)) return FALSE;
		end++;
	}
	return num(n);
}

static Val *b_readline(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("read-line arity");
	Val *in = is_nil(a) ? STDIN_PORT : need_input_port(car(a));
	char buf[8192];
	if (!fgets(buf, sizeof(buf), in->as.port.f)) return EOFV;
	size_t n = strlen(buf);
	if (n && buf[n - 1] == '\n') buf[n - 1] = 0;
	return str(buf);
}

static Val *b_curin(Val *a, Env *e) { (void)e; if (len(a) != 0) die("current-input-port arity"); return STDIN_PORT; }
static Val *b_curout(Val *a, Env *e) { (void)e; if (len(a) != 0) die("current-output-port arity"); return STDOUT_PORT; }

static Val *b_portp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_PORT && car(a)->as.port.f); }
static Val *b_inputportp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_PORT && car(a)->as.port.f && car(a)->as.port.input); }
static Val *b_outputportp(Val *a, Env *e) { (void)e; return boolean(len(a) == 1 && car(a)->t == T_PORT && car(a)->as.port.f && car(a)->as.port.output); }

static Val *b_open_input_file(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("open-input-file arity");
	FILE *f = fopen(need_str(car(a)), "rb");
	if (!f) die("%s: %s", need_str(car(a)), strerror(errno));
	return port(f, 1, 0, 1);
}

static Val *b_open_output_file(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("open-output-file arity");
	FILE *f = fopen(need_str(car(a)), "wb");
	if (!f) die("%s: %s", need_str(car(a)), strerror(errno));
	return port(f, 0, 1, 1);
}

static Val *b_close_port(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("close-port arity");
	Val *p = need_port(car(a));
	if (!p->as.port.close) return TRUE;
	if (fclose(p->as.port.f) != 0) die("close-port failed");
	p->as.port.f = NULL;
	p->as.port.input = p->as.port.output = p->as.port.close = 0;
	return TRUE;
}

static Val *b_read_char(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("read-char arity");
	Val *in = is_nil(a) ? STDIN_PORT : need_input_port(car(a));
	int c = fgetc(in->as.port.f);
	if (c == EOF) return EOFV;
	char buf[2] = { (char)c, 0 };
	return str(buf);
}

static Val *b_peek_char(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("peek-char arity");
	Val *in = is_nil(a) ? STDIN_PORT : need_input_port(car(a));
	int c = fgetc(in->as.port.f);
	if (c == EOF) return EOFV;
	if (ungetc(c, in->as.port.f) == EOF) die("peek-char failed");
	char buf[2] = { (char)c, 0 };
	return str(buf);
}

static Val *b_write_char(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 2) die("write-char arity");
	Val *out = len(a) == 2 ? need_output_port(cadr(a)) : STDOUT_PORT;
	char *s = need_str(car(a));
	if (!*s) die("write-char expected character");
	port_putc(out, (unsigned char)*s);
	if (out == STDOUT_PORT && swamp_tb_active) tb_present();
	return NIL;
}

static Val *b_write(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || len(a) > 2) die("write arity");
	Val *out = len(a) == 2 ? need_output_port(cadr(a)) : STDOUT_PORT;
	print_val_port(out, car(a));
	if (out == STDOUT_PORT && swamp_tb_active) tb_present();
	return NIL;
}

static Val *b_flush_output_port(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("flush-output-port arity");
	Val *out = is_nil(a) ? STDOUT_PORT : need_output_port(car(a));
	if (out == STDOUT_PORT && swamp_tb_active) tb_present();
	else if (fflush(out->as.port.f) != 0) die("flush-output-port failed");
	return NIL;
}

static void buf_add(char **buf, size_t *n, size_t *cap, int c)
{
	if (*n + 2 >= *cap) {
		*cap *= 2;
		char *next = realloc(*buf, *cap);
		if (!next) die("out of memory");
		*buf = next;
	}
	(*buf)[(*n)++] = (char)c;
	(*buf)[*n] = 0;
}

static Val *read_datum_from_port(Val *in)
{
	FILE *f = need_input_port(in)->as.port.f;
	int c;
	for (;;) {
		c = fgetc(f);
		if (c == EOF) return EOFV;
		if (isspace((unsigned char)c)) continue;
		if (c == ';') {
			while ((c = fgetc(f)) != EOF && c != '\n') {}
			if (c == EOF) return EOFV;
			continue;
		}
		break;
	}

	if (c == '\'') {
		Val *quoted = read_datum_from_port(in);
		if (quoted == EOFV) die("quote before eof");
		return cons(sym("quote"), cons(quoted, NIL));
	}

	size_t cap = 256, n = 0;
	char *buf = xmalloc(cap);
	buf[0] = 0;
	buf_add(&buf, &n, &cap, c);

	if (c == '#') {
		int next = fgetc(f);
		if (next == '(') {
			buf_add(&buf, &n, &cap, next);
			int depth = 1, in_string = 0, escaped = 0, comment = 0;
			while (depth > 0 && (c = fgetc(f)) != EOF) {
				buf_add(&buf, &n, &cap, c);
				if (comment) {
					if (c == '\n') comment = 0;
				} else if (in_string) {
					if (escaped) escaped = 0;
					else if (c == '\\') escaped = 1;
					else if (c == '"') in_string = 0;
				} else if (c == ';') comment = 1;
				else if (c == '"') in_string = 1;
				else if (c == '(') depth++;
				else if (c == ')') depth--;
			}
			if (depth != 0) die("unterminated vector");
		} else {
			if (next != EOF) ungetc(next, f);
			while ((c = fgetc(f)) != EOF && !delim((unsigned char)c))
				buf_add(&buf, &n, &cap, c);
			if (c != EOF) ungetc(c, f);
		}
	} else if (c == '(') {
		int depth = 1, in_string = 0, escaped = 0, comment = 0;
		while (depth > 0 && (c = fgetc(f)) != EOF) {
			buf_add(&buf, &n, &cap, c);
			if (comment) {
				if (c == '\n') comment = 0;
			} else if (in_string) {
				if (escaped) escaped = 0;
				else if (c == '\\') escaped = 1;
				else if (c == '"') in_string = 0;
			} else if (c == ';') comment = 1;
			else if (c == '"') in_string = 1;
			else if (c == '(') depth++;
			else if (c == ')') depth--;
		}
		if (depth != 0) die("unterminated list");
	} else if (c == '"') {
		int escaped = 0;
		while ((c = fgetc(f)) != EOF) {
			buf_add(&buf, &n, &cap, c);
			if (escaped) escaped = 0;
			else if (c == '\\') escaped = 1;
			else if (c == '"') break;
		}
		if (c == EOF) die("unterminated string");
	} else {
		while ((c = fgetc(f)) != EOF && !delim((unsigned char)c))
			buf_add(&buf, &n, &cap, c);
		if (c != EOF) ungetc(c, f);
	}

	Reader r = { buf, 0 };
	Val *v = read_expr(&r);
	free(buf);
	return v ? v : EOFV;
}

static Val *b_read(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("read arity");
	Val *in = is_nil(a) ? STDIN_PORT : need_input_port(car(a));
	return read_datum_from_port(in);
}
static Val *b_randomize(Val *a, Env *e) { (void)a; (void)e; srand((unsigned)time(NULL) ^ (unsigned)getpid()); return NIL; }
static Val *b_randint(Val *a, Env *e) { (void)e; if (len(a) != 1) die("random-integer arity"); int n = (int)need_num(car(a)); return num(n > 0 ? rand() % n : 0); }
static Val *b_randreal(Val *a, Env *e) { (void)e; if (len(a) != 0) die("random-real arity"); return num((double)rand() / ((double)RAND_MAX + 1.0)); }
static Val *b_now(Val *a, Env *e) { (void)e; if (len(a) != 0) die("current-time arity"); return num((double)time(NULL)); }
static Val *b_localtime(Val *a, Env *e) { (void)e; if (len(a) != 1) die("localtime arity"); return car(a); }

static Val *b_strftime(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("strftime arity");
	time_t t = (time_t)need_num(cadr(a));
	struct tm *tm = localtime(&t);
	char buf[128];
	if (!strftime(buf, sizeof(buf), need_str(car(a)), tm)) buf[0] = 0;
	return str(buf);
}

static char *read_command(const char *cmd)
{
	FILE *p = popen(cmd, "r");
	if (!p) die("popen failed: %s", strerror(errno));

	size_t cap = 4096, n = 0;
	char *buf = xmalloc(cap);
	int c;
	while ((c = fgetc(p)) != EOF) {
		if (n + 1 >= cap) {
			cap *= 2;
			char *next = realloc(buf, cap);
			if (!next) die("out of memory");
			buf = next;
		}
		buf[n++] = (char)c;
	}
	buf[n] = 0;
	if (pclose(p) == -1) {
		free(buf);
		die("pclose failed: %s", strerror(errno));
	}
	return buf;
}

static char *run_argv_capture(char **argv, int *status_out, int capture_stderr)
{
	int fds[2];
	if (pipe(fds) != 0) die("pipe failed: %s", strerror(errno));

	pid_t pid = fork();
	if (pid < 0) die("fork failed: %s", strerror(errno));
	if (pid == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
		if (capture_stderr && dup2(fds[1], STDERR_FILENO) < 0) _exit(127);
		close(fds[1]);
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fds[1]);
	size_t cap = 4096, n = 0;
	char *buf = xmalloc(cap);
	char tmp[1024];
	ssize_t nr;
	while ((nr = read(fds[0], tmp, sizeof(tmp))) > 0) {
		if (n + (size_t)nr + 1 >= cap) {
			while (n + (size_t)nr + 1 >= cap) cap *= 2;
			char *next = realloc(buf, cap);
			if (!next) die("out of memory");
			buf = next;
		}
		memcpy(buf + n, tmp, (size_t)nr);
		n += (size_t)nr;
	}
	close(fds[0]);
	buf[n] = 0;

	int status;
	if (waitpid(pid, &status, 0) < 0) {
		free(buf);
		die("waitpid failed: %s", strerror(errno));
	}
	if (status_out) *status_out = status;
	return buf;
}

static char *run_argv(char **argv, int *status_out)
{
	return run_argv_capture(argv, status_out, 0);
}

static Val *b_system(Val *a, Env *e)
{
	(void)e;
	int argc = len(a);
	if (argc < 1) die("system arity");

	char **argv = xmalloc(sizeof(*argv) * (size_t)(argc + 1));
	int i = 0;
	for (Val *p = a; !is_nil(p); p = cdr(p), i++)
		argv[i] = need_str(car(p));
	argv[i] = NULL;

	int status = 0;
	char *out = run_argv(argv, &status);
	free(argv);
	Val *r = str(out);
	free(out);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) die("system command failed");
	return r;
}

static Val *b_system_status(Val *a, Env *e)
{
	(void)e;
	int argc = len(a);
	if (argc < 1) die("system/status arity");

	char **argv = xmalloc(sizeof(*argv) * (size_t)(argc + 1));
	int i = 0;
	for (Val *p = a; !is_nil(p); p = cdr(p), i++)
		argv[i] = need_str(car(p));
	argv[i] = NULL;

	int status = 0;
	char *out = run_argv_capture(argv, &status, 1);
	free(argv);

	int code = 127;
	int ok = 0;
	if (WIFEXITED(status)) {
		code = WEXITSTATUS(status);
		ok = code == 0;
	} else if (WIFSIGNALED(status)) {
		code = -WTERMSIG(status);
	}

	Val *r = cons(sym("ok"), cons(boolean(ok),
	         cons(sym("status"), cons(num((double)code),
	         cons(sym("output"), cons(str(out), NIL))))));
	free(out);
	return r;
}

static Val *b_error(Val *a, Env *e)
{
	(void)e;
	if (is_nil(a)) die("error");
	if (car(a)->t == T_STR || car(a)->t == T_SYM) die("%s", car(a)->as.s);
	die("error");
	return NIL;
}

static Val *b_last_error(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 0) die("last-error arity");
	return str(last_error);
}

static const char *cmd_name(Val *v)
{
	if (v->t == T_STR || v->t == T_SYM) return v->as.s;
	die("termbox command must be string or symbol");
	return "";
}

static int termbox_color(const char *name, uintattr_t *out)
{
	if (strcmp(name, "default") == 0) *out = TB_DEFAULT;
	else if (strcmp(name, "black") == 0) *out = TB_BLACK;
	else if (strcmp(name, "red") == 0) *out = TB_RED;
	else if (strcmp(name, "green") == 0) *out = TB_GREEN;
	else if (strcmp(name, "yellow") == 0) *out = TB_YELLOW;
	else if (strcmp(name, "blue") == 0) *out = TB_BLUE;
	else if (strcmp(name, "magenta") == 0) *out = TB_MAGENTA;
	else if (strcmp(name, "cyan") == 0) *out = TB_CYAN;
	else if (strcmp(name, "white") == 0) *out = TB_WHITE;
	else return 0;
	return 1;
}

static uintattr_t termbox_attr(Val *v)
{
	if (v->t == T_NUM) return (uintattr_t)need_int(v);
	if (v->t != T_STR && v->t != T_SYM) die("expected termbox attr");

	const char *name = v->as.s;
	uintattr_t out;
	if (termbox_color(name, &out)) return out;
	if (strcmp(name, "bold") == 0) return TB_BOLD;
	if (strcmp(name, "underline") == 0) return TB_UNDERLINE;
	if (strcmp(name, "reverse") == 0) return TB_REVERSE;
	if (strcmp(name, "italic") == 0) return TB_ITALIC;
	die("unknown termbox attr: %s", name);
	return TB_DEFAULT;
}

static uint32_t termbox_ch(Val *v)
{
	if (v->t == T_NUM) return (uint32_t)need_int(v);
	if (v->t != T_STR) die("expected character number or string");
	uint32_t ch = 0;
	if (tb_utf8_char_to_unicode(&ch, v->as.s) < 1) die("bad utf-8 character");
	return ch;
}

static Val *termbox_event_value(struct tb_event *ev)
{
	return cons(sym("type"), cons(num((double)ev->type),
	       cons(sym("key"), cons(num((double)ev->key),
	       cons(sym("ch"), cons(num((double)ev->ch),
	       cons(sym("mod"), cons(num((double)ev->mod),
	       cons(sym("w"), cons(num((double)ev->w),
	       cons(sym("h"), cons(num((double)ev->h),
	       cons(sym("x"), cons(num((double)ev->x),
	       cons(sym("y"), cons(num((double)ev->y), NIL))))))))))))))));
}

static void termbox_ensure(void)
{
	if (swamp_tb_active) return;
	int rv = isatty(STDIN_FILENO) ? tb_init_fd(STDIN_FILENO) : tb_init();
	if (rv != TB_OK) die("termbox init failed: %d", rv);
	tb_set_output_mode(TB_OUTPUT_256);
	swamp_tb_active = 1;
	swamp_tb_x = 0;
	swamp_tb_y = 0;
}

static Val *b_termbox(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1) die("termbox arity");

	const char *cmd = cmd_name(car(a));
	Val *args = cdr(a);
	if (strcmp(cmd, "init") == 0 || strcmp(cmd, "tb_init") == 0) {
		if (!is_nil(args)) die("termbox init arity");
		termbox_ensure();
		return NIL;
	}
	if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "tb_shutdown") == 0) {
		if (!is_nil(args)) die("termbox shutdown arity");
		termbox_cleanup();
		return TRUE;
	}

	termbox_ensure();
	if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "tb_clear") == 0) {
		if (!is_nil(args)) die("termbox clear arity");
		int rv = tb_clear();
		if (rv != TB_OK) die("termbox clear failed: %d", rv);
		swamp_tb_x = swamp_tb_y = 0;
		return NIL;
	}
	if (strcmp(cmd, "set-clear-attrs") == 0 || strcmp(cmd, "clear-attrs") == 0) {
		if (len(args) != 2) die("termbox set-clear-attrs arity");
		int rv = tb_set_clear_attrs(termbox_attr(car(args)), termbox_attr(cadr(args)));
		if (rv != TB_OK) die("termbox set-clear-attrs failed: %d", rv);
		return NIL;
	}
	if (strcmp(cmd, "present") == 0 || strcmp(cmd, "tb_present") == 0) {
		if (!is_nil(args)) die("termbox present arity");
		int rv = tb_present();
		if (rv != TB_OK) die("termbox present failed: %d", rv);
		return NIL;
	}
	if (strcmp(cmd, "setcolor") == 0 || strcmp(cmd, "fg") == 0) {
		if (len(args) != 1) die("termbox setcolor arity");
		swamp_tb_fg = termbox_attr(car(args));
		return NIL;
	}
	if (strcmp(cmd, "setbg") == 0 || strcmp(cmd, "bg") == 0) {
		if (len(args) != 1) die("termbox setbg arity");
		swamp_tb_bg = termbox_attr(car(args));
		return NIL;
	}
	if (strcmp(cmd, "attrs") == 0) {
		if (len(args) != 2) die("termbox attrs arity");
		swamp_tb_fg = termbox_attr(car(args));
		swamp_tb_bg = termbox_attr(cadr(args));
		return NIL;
	}
	if (strcmp(cmd, "goto") == 0 || strcmp(cmd, "move") == 0) {
		if (len(args) != 2) die("termbox goto arity");
		swamp_tb_x = (int)need_num(car(args));
		swamp_tb_y = (int)need_num(cadr(args));
		if (swamp_tb_x < 0) swamp_tb_x = 0;
		if (swamp_tb_y < 0) swamp_tb_y = 0;
		return NIL;
	}
	if (strcmp(cmd, "width") == 0) {
		if (!is_nil(args)) die("termbox width arity");
		return num((double)tb_width());
	}
	if (strcmp(cmd, "height") == 0) {
		if (!is_nil(args)) die("termbox height arity");
		return num((double)tb_height());
	}
	if (strcmp(cmd, "set-cursor") == 0 || strcmp(cmd, "cursor") == 0) {
		if (len(args) != 2) die("termbox set-cursor arity");
		int rv = tb_set_cursor(need_int(car(args)), need_int(cadr(args)));
		if (rv != TB_OK) die("termbox set-cursor failed: %d", rv);
		return NIL;
	}
	if (strcmp(cmd, "hide-cursor") == 0) {
		if (!is_nil(args)) die("termbox hide-cursor arity");
		int rv = tb_hide_cursor();
		if (rv != TB_OK) die("termbox hide-cursor failed: %d", rv);
		return NIL;
	}
	if (strcmp(cmd, "set-cell") == 0 || strcmp(cmd, "cell") == 0) {
		if (len(args) != 5) die("termbox set-cell arity");
		int rv = tb_set_cell(need_int(car(args)), need_int(cadr(args)),
		                     termbox_ch(caddr(args)), termbox_attr(car(cdr(cdr(cdr(args))))),
		                     termbox_attr(car(cdr(cdr(cdr(cdr(args)))))));
		if (rv != TB_OK) die("termbox set-cell failed: %d", rv);
		return NIL;
	}
	if (strcmp(cmd, "extend-cell") == 0) {
		if (len(args) != 3) die("termbox extend-cell arity");
		int rv = tb_extend_cell(need_int(car(args)), need_int(cadr(args)), termbox_ch(caddr(args)));
		if (rv != TB_OK) die("termbox extend-cell failed: %d", rv);
		return NIL;
	}
	if (strcmp(cmd, "print-at") == 0) {
		if (len(args) != 5) die("termbox print-at arity");
		int rv = tb_print(need_int(car(args)), need_int(cadr(args)),
		                  termbox_attr(caddr(args)), termbox_attr(car(cdr(cdr(cdr(args))))),
		                  need_str(car(cdr(cdr(cdr(cdr(args)))))));
		if (rv < 0) die("termbox print-at failed: %d", rv);
		return num((double)rv);
	}
	if (strcmp(cmd, "set-input-mode") == 0 || strcmp(cmd, "input-mode") == 0) {
		if (len(args) != 1) die("termbox set-input-mode arity");
		int rv = tb_set_input_mode(need_int(car(args)));
		if (rv < 0) die("termbox set-input-mode failed: %d", rv);
		return num((double)rv);
	}
	if (strcmp(cmd, "set-output-mode") == 0 || strcmp(cmd, "output-mode") == 0) {
		if (len(args) != 1) die("termbox set-output-mode arity");
		int rv = tb_set_output_mode(need_int(car(args)));
		if (rv < 0) die("termbox set-output-mode failed: %d", rv);
		return num((double)rv);
	}
	if (strcmp(cmd, "poll-event") == 0 || strcmp(cmd, "event") == 0) {
		if (!is_nil(args)) die("termbox poll-event arity");
		struct tb_event ev;
		int rv = tb_poll_event(&ev);
		if (rv < 0) die("termbox poll-event failed: %d", rv);
		return termbox_event_value(&ev);
	}
	if (strcmp(cmd, "peek-event") == 0) {
		if (len(args) != 1) die("termbox peek-event arity");
		struct tb_event ev;
		int rv = tb_peek_event(&ev, (int)need_num(car(args)));
		if (rv < 0) die("termbox peek-event failed: %d", rv);
		return rv == 0 ? FALSE : termbox_event_value(&ev);
	}
	if (strcmp(cmd, "send") == 0) {
		if (len(args) != 1) die("termbox send arity");
		char *s = need_str(car(args));
		int rv = tb_send(s, strlen(s));
		if (rv != TB_OK) die("termbox send failed: %d", rv);
		return NIL;
	}
	if (strcmp(cmd, "last-errno") == 0) {
		if (!is_nil(args)) die("termbox last-errno arity");
		return num((double)tb_last_errno());
	}
	if (strcmp(cmd, "cell-at") == 0) {
		if (len(args) != 2) die("termbox cell-at arity");
		int x = need_int(car(args));
		int y = need_int(cadr(args));
		if (x < 0 || y < 0 || x >= tb_width() || y >= tb_height()) die("termbox cell-at bounds");
		struct tb_cell *cell = tb_cell_buffer() + y * tb_width() + x;
		return cons(sym("ch"), cons(num((double)cell->ch),
		       cons(sym("fg"), cons(num((double)cell->fg),
		       cons(sym("bg"), cons(num((double)cell->bg), NIL))))));
	}
	if (strcmp(cmd, "print") == 0) {
		if (len(args) != 1) die("termbox print arity");
		swamp_puts(need_str(car(args)));
		tb_present();
		return NIL;
	}
	die("unknown termbox command: %s", cmd);
	return FALSE;
}

static Val *b_termbox_demo(Val *a, Env *e)
{
	(void)e;
	if (len(a) > 1) die("termbox-demo arity");
	const char *msg = is_nil(a) ? "swampscheme termbox2 superpowers" : need_str(car(a));

	termbox_ensure();
	tb_clear();
	swamp_tb_x = (tb_width() - (int)strlen(msg)) / 2;
	swamp_tb_y = tb_height() / 2;
	if (swamp_tb_x < 0) swamp_tb_x = 0;
	swamp_tb_fg = TB_GREEN | TB_BOLD;
	swamp_puts(msg);
	swamp_tb_fg = TB_YELLOW;
	swamp_tb_x = 0;
	swamp_tb_y += 2;
	swamp_puts("press any key to escape the swamp");
	tb_present();
	struct tb_event ev;
	tb_poll_event(&ev);
	termbox_cleanup();
	return NIL;
}

#ifdef WITH_SDL3
static int sdl3_ready(void)
{
	if (!swamp_sdl_window || !swamp_sdl_renderer) die("SDL3 is not initialized");
	return 1;
}

static SDL_Texture *need_sdl_texture(Val *v)
{
	if (!v || v->t != T_SDL_TEXTURE || !v->as.sdl_texture) die("expected live SDL texture");
	return v->as.sdl_texture;
}

static Val *sdl3_event_value(const SDL_Event *ev)
{
	Val *r = NIL;
	r = cons(str(ev->type == SDL_EVENT_TEXT_INPUT ? ev->text.text : ""), r);
	r = cons(sym("text"), r);
	r = cons(num((double)ev->key.repeat), r);
	r = cons(sym("repeat"), r);
	r = cons(num((double)ev->key.mod), r);
	r = cons(sym("mod"), r);
	r = cons(num((double)ev->key.scancode), r);
	r = cons(sym("scancode"), r);
	r = cons(num((double)ev->button.button), r);
	r = cons(sym("button"), r);
	r = cons(num((double)ev->motion.y), r);
	r = cons(sym("y"), r);
	r = cons(num((double)ev->motion.x), r);
	r = cons(sym("x"), r);
	r = cons(num((double)ev->key.key), r);
	r = cons(sym("key"), r);
	r = cons(num((double)ev->window.windowID), r);
	r = cons(sym("window"), r);
	r = cons(num((double)ev->type), r);
	r = cons(sym("type"), r);
	return r;
}

static SDL_Scancode sdl3_scancode_from_key(Val *v)
{
	SDL_Keycode key = (SDL_Keycode)need_int(v);
	return SDL_GetScancodeFromKey(key, NULL);
}

static Val *b_sdl3(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1) die("sdl3 arity");
	const char *cmd = cmd_name(car(a));
	Val *args = cdr(a);

	if (!strcmp(cmd, "init")) {
		if (len(args) < 2 || len(args) > 3) die("sdl3 init expects width height [title]");
		if (swamp_sdl_window) return TRUE;
		int w = need_int(car(args)), h = need_int(cadr(args));
		const char *title = is_nil(cddr(args)) ? "swampscheme" : need_str(caddr(args));
		if (w <= 0 || h <= 0) die("sdl3 init dimensions must be positive");
		if (!SDL_Init(SDL_INIT_VIDEO)) die("SDL_Init: %s", SDL_GetError());
		swamp_sdl_window = SDL_CreateWindow(title, w, h, 0);
		if (!swamp_sdl_window) { SDL_QuitSubSystem(SDL_INIT_VIDEO); die("SDL_CreateWindow: %s", SDL_GetError()); }
		swamp_sdl_renderer = SDL_CreateRenderer(swamp_sdl_window, NULL);
		if (!swamp_sdl_renderer) { SDL_DestroyWindow(swamp_sdl_window); swamp_sdl_window = NULL; SDL_QuitSubSystem(SDL_INIT_VIDEO); die("SDL_CreateRenderer: %s", SDL_GetError()); }
		swamp_nk = nk_sdl_init(swamp_sdl_window, swamp_sdl_renderer, nk_sdl_allocator());
		if (!swamp_nk) { sdl3_cleanup(); die("Nuklear initialization failed"); }
		nk_sdl_style_set_debug_font(swamp_nk);
		return TRUE;
	}
	if (!strcmp(cmd, "shutdown") || !strcmp(cmd, "destroy")) {
		if (!is_nil(args)) die("sdl3 shutdown arity");
		sdl3_cleanup();
		return TRUE;
	}
	sdl3_ready();
	if (!strcmp(cmd, "clear")) {
		if (len(args) != 4) die("sdl3 clear expects r g b a");
		if (!SDL_SetRenderDrawColor(swamp_sdl_renderer, (Uint8)need_int(car(args)), (Uint8)need_int(cadr(args)), (Uint8)need_int(caddr(args)), (Uint8)need_int(cadddr(args))) || !SDL_RenderClear(swamp_sdl_renderer)) die("SDL clear: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "color") || !strcmp(cmd, "set-color")) {
		if (len(args) != 4) die("sdl3 color expects r g b a");
		if (!SDL_SetRenderDrawColor(swamp_sdl_renderer, (Uint8)need_int(car(args)), (Uint8)need_int(cadr(args)), (Uint8)need_int(caddr(args)), (Uint8)need_int(cadddr(args)))) die("SDL color: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "pixel")) {
		if (len(args) != 2) die("sdl3 pixel expects x y");
		if (!SDL_RenderPoint(swamp_sdl_renderer, (float)need_num(car(args)), (float)need_num(cadr(args)))) die("SDL pixel: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "line")) {
		if (len(args) != 4) die("sdl3 line expects x1 y1 x2 y2");
		if (!SDL_RenderLine(swamp_sdl_renderer, (float)need_num(car(args)), (float)need_num(cadr(args)), (float)need_num(caddr(args)), (float)need_num(cadddr(args)))) die("SDL line: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "rect") || !strcmp(cmd, "fill-rect")) {
		if (len(args) != 4) die("sdl3 rect expects x y width height");
		SDL_FRect r = {(float)need_num(car(args)), (float)need_num(cadr(args)), (float)need_num(caddr(args)), (float)need_num(cadddr(args))};
		bool ok = !strcmp(cmd, "fill-rect") ? SDL_RenderFillRect(swamp_sdl_renderer, &r) : SDL_RenderRect(swamp_sdl_renderer, &r);
		if (!ok) die("SDL rect: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "texture-load") || !strcmp(cmd, "load-texture")) {
		if (len(args) != 1) die("sdl3 texture-load expects an image path");
		int w, h;
		unsigned char *pixels = stbi_load(need_str(car(args)), &w, &h, NULL, 4);
		if (!pixels) die("stbi_load: %s", stbi_failure_reason());
		SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
		if (!surface) { stbi_image_free(pixels); die("SDL_CreateSurfaceFrom: %s", SDL_GetError()); }
		SDL_Texture *texture = SDL_CreateTextureFromSurface(swamp_sdl_renderer, surface);
		SDL_DestroySurface(surface);
		stbi_image_free(pixels);
		if (!texture) die("SDL_CreateTextureFromSurface: %s", SDL_GetError());
		Val *v = val(T_SDL_TEXTURE); v->as.sdl_texture = texture; return v;
	}
	if (!strcmp(cmd, "texture-size")) {
		if (len(args) != 1) die("sdl3 texture-size arity");
		float w, h;
		if (!SDL_GetTextureSize(need_sdl_texture(car(args)), &w, &h)) die("SDL_GetTextureSize: %s", SDL_GetError());
		return cons(num(w), cons(num(h), NIL));
	}
	if (!strcmp(cmd, "texture-destroy")) {
		if (len(args) != 1) die("sdl3 texture-destroy arity");
		Val *v = car(args); need_sdl_texture(v); SDL_DestroyTexture(v->as.sdl_texture); v->as.sdl_texture = NULL; return TRUE;
	}
	if (!strcmp(cmd, "blit") || !strcmp(cmd, "copy")) {
		if (len(args) != 3 && len(args) != 5) die("sdl3 blit expects texture x y [width height]");
		SDL_Texture *texture = need_sdl_texture(car(args));
		SDL_FRect dst = {(float)need_num(cadr(args)), (float)need_num(caddr(args)), 0, 0};
		if (len(args) == 5) { dst.w = (float)need_num(car(cdr(cdr(cdr(args))))); dst.h = (float)need_num(car(cdr(cdr(cdr(cdr(args)))))); }
		else if (!SDL_GetTextureSize(texture, &dst.w, &dst.h)) die("SDL_GetTextureSize: %s", SDL_GetError());
		if (!SDL_RenderTexture(swamp_sdl_renderer, texture, NULL, &dst)) die("SDL blit: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "ticks")) {
		if (!is_nil(args)) die("sdl3 ticks arity");
		return num((double)SDL_GetTicks());
	}
	if (!strcmp(cmd, "delay")) {
		if (len(args) != 1) die("sdl3 delay arity");
		int ms = need_int(car(args));
		if (ms < 0) die("sdl3 delay requires nonnegative milliseconds");
		SDL_Delay((Uint32)ms);
		return NIL;
	}
	if (!strcmp(cmd, "window-size")) {
		if (!is_nil(args)) die("sdl3 window-size arity");
		int w, h;
		if (!SDL_GetWindowSize(swamp_sdl_window, &w, &h)) die("SDL_GetWindowSize: %s", SDL_GetError());
		return cons(num((double)w), cons(num((double)h), NIL));
	}
	if (!strcmp(cmd, "set-window-size")) {
		if (len(args) != 2) die("sdl3 set-window-size arity");
		if (!SDL_SetWindowSize(swamp_sdl_window, need_int(car(args)), need_int(cadr(args)))) die("SDL_SetWindowSize: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "fullscreen")) {
		if (len(args) != 1) die("sdl3 fullscreen arity");
		if (!SDL_SetWindowFullscreen(swamp_sdl_window, truthy(car(args)))) die("SDL_SetWindowFullscreen: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "key-down?")) {
		if (len(args) != 1) die("sdl3 key-down? arity");
		int count = 0;
		const bool *state = SDL_GetKeyboardState(&count);
		SDL_Scancode scancode = sdl3_scancode_from_key(car(args));
		return boolean((int)scancode >= 0 && (int)scancode < count && state[scancode]);
	}
	if (!strcmp(cmd, "clipboard-set")) {
		if (len(args) != 1) die("sdl3 clipboard-set arity");
		if (!SDL_SetClipboardText(need_str(car(args)))) die("SDL clipboard set: %s", SDL_GetError());
		return TRUE;
	}
	if (!strcmp(cmd, "clipboard-get")) {
		if (!is_nil(args)) die("sdl3 clipboard-get arity");
		char *text = SDL_GetClipboardText();
		if (!text) die("SDL clipboard get: %s", SDL_GetError());
		Val *result = str(text);
		SDL_free(text);
		return result;
	}
	if (!strcmp(cmd, "text-input")) {
		if (len(args) != 1) die("sdl3 text-input arity");
		if (truthy(car(args))) {
			if (!SDL_StartTextInput(swamp_sdl_window)) die("SDL start text input: %s", SDL_GetError());
		} else if (!SDL_StopTextInput(swamp_sdl_window)) die("SDL stop text input: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "present")) {
		if (!is_nil(args)) die("sdl3 present arity");
		if (!SDL_RenderPresent(swamp_sdl_renderer)) die("SDL present: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "audio-init")) {
		if (!is_nil(args)) die("sdl3 audio-init arity");
		if (swamp_sdl_audio) return TRUE;
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) die("SDL audio init: %s", SDL_GetError());
		swamp_sdl_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &swamp_sdl_audio_spec, NULL, NULL);
		if (!swamp_sdl_audio) { SDL_QuitSubSystem(SDL_INIT_AUDIO); die("SDL audio open: %s", SDL_GetError()); }
		if (!SDL_ResumeAudioStreamDevice(swamp_sdl_audio)) die("SDL audio resume: %s", SDL_GetError());
		return TRUE;
	}
	if (!strcmp(cmd, "audio-shutdown")) {
		if (!is_nil(args)) die("sdl3 audio-shutdown arity");
		if (swamp_sdl_audio) { SDL_DestroyAudioStream(swamp_sdl_audio); swamp_sdl_audio = NULL; }
		if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return TRUE;
	}
	if (!strcmp(cmd, "sound-load") || !strcmp(cmd, "load-sound")) {
		if (len(args) != 1) die("sdl3 sound-load expects a WAV path");
		if (!swamp_sdl_audio) die("call sdl3 audio-init first");
		SDL_AudioSpec source_spec;
		Uint8 *source_data = NULL;
		Uint32 source_len = 0;
		if (!SDL_LoadWAV(need_str(car(args)), &source_spec, &source_data, &source_len)) die("SDL_LoadWAV: %s", SDL_GetError());
		Uint8 *converted = NULL;
		int converted_len = 0;
		if (!SDL_ConvertAudioSamples(&source_spec, source_data, (int)source_len, &swamp_sdl_audio_spec, &converted, &converted_len)) {
			SDL_free(source_data);
			die("SDL_ConvertAudioSamples: %s", SDL_GetError());
		}
		SDL_free(source_data);
		Val *sound = val(T_SDL_SOUND);
		sound->as.sdl_sound.data = converted;
		sound->as.sdl_sound.len = (Uint32)converted_len;
		return sound;
	}
	if (!strcmp(cmd, "sound-play") || !strcmp(cmd, "play-sound")) {
		if (len(args) != 1) die("sdl3 sound-play arity");
		if (!swamp_sdl_audio) die("call sdl3 audio-init first");
		Val *sound = car(args);
		if (sound->t != T_SDL_SOUND || !sound->as.sdl_sound.data) die("expected live SDL sound");
		if (!SDL_PutAudioStreamData(swamp_sdl_audio, sound->as.sdl_sound.data, (int)sound->as.sdl_sound.len)) die("SDL audio queue: %s", SDL_GetError());
		return NIL;
	}
	if (!strcmp(cmd, "sound-destroy")) {
		if (len(args) != 1) die("sdl3 sound-destroy arity");
		Val *sound = car(args);
		if (sound->t != T_SDL_SOUND) die("expected SDL sound");
		if (sound->as.sdl_sound.data) SDL_free(sound->as.sdl_sound.data);
		sound->as.sdl_sound.data = NULL;
		sound->as.sdl_sound.len = 0;
		return TRUE;
	}
	if (!strcmp(cmd, "event") || !strcmp(cmd, "poll-event")) {
		if (!is_nil(args)) die("sdl3 event arity");
		SDL_Event ev;
		if (!SDL_PollEvent(&ev)) return FALSE;
		if (swamp_nk) nk_sdl_handle_event(swamp_nk, &ev);
		return sdl3_event_value(&ev);
	}
	if (!strcmp(cmd, "begin-frame")) {
		if (!is_nil(args)) die("sdl3 begin-frame arity");
		if (!swamp_nk || swamp_sdl_frame) die("SDL3 frame already active");
		nk_input_begin(swamp_nk); swamp_sdl_frame = 1; return NIL;
	}
	die("unknown sdl3 command: %s", cmd);
	return FALSE;
}

static Val *b_nuklear(Val *a, Env *e)
{
	(void)e;
	if (len(a) < 1 || !swamp_nk) die("nuklear requires sdl3 init");
	const char *cmd = cmd_name(car(a)); Val *args = cdr(a);
	if (!strcmp(cmd, "begin-frame")) { if (!swamp_sdl_frame) die("nuklear frame not active"); nk_input_end(swamp_nk); return NIL; }
	if (!strcmp(cmd, "begin")) {
		if (len(args) != 5) die("nuklear begin expects title x y width height");
		return boolean(nk_begin(swamp_nk, need_str(car(args)), nk_rect((float)need_num(cadr(args)), (float)need_num(caddr(args)), (float)need_num(car(cdr(cdr(cdr(args))))), (float)need_num(car(cdr(cdr(cdr(cdr(args))))))), NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE));
	}
	if (!strcmp(cmd, "end")) { if (!is_nil(args)) die("nuklear end arity"); nk_end(swamp_nk); return NIL; }
	if (!strcmp(cmd, "row")) { if (len(args) != 2) die("nuklear row expects height columns"); nk_layout_row_dynamic(swamp_nk, (float)need_num(car(args)), need_int(cadr(args))); return NIL; }
	if (!strcmp(cmd, "label")) { if (len(args) != 1) die("nuklear label arity"); nk_label(swamp_nk, need_str(car(args)), NK_TEXT_LEFT); return NIL; }
	if (!strcmp(cmd, "button")) { if (len(args) != 1) die("nuklear button arity"); return boolean(nk_button_label(swamp_nk, need_str(car(args)))); }
	if (!strcmp(cmd, "render")) { if (!is_nil(args)) die("nuklear render arity"); nk_sdl_render(swamp_nk, NK_ANTI_ALIASING_ON); swamp_sdl_frame = 0; return NIL; }
	if (!strcmp(cmd, "font-load")) {
		if (len(args) != 2) die("nuklear font-load expects path size");
		struct nk_font_atlas *atlas = nk_sdl_font_stash_begin(swamp_nk);
		struct nk_font *font = nk_font_atlas_add_from_file(atlas, need_str(car(args)), (float)need_num(cadr(args)), NULL);
		if (!font) { nk_sdl_font_stash_end(swamp_nk); die("could not load font: %s", need_str(car(args))); }
		nk_sdl_font_stash_end(swamp_nk);
		return TRUE;
	}
	die("unknown nuklear command: %s", cmd);
	return FALSE;
}
#endif

static Val *b_cpu_temp(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 0) die("cpu-temp arity");

	char *out = read_command("./scripts/cputemp.sh");
	char *end = NULL;
	errno = 0;
	double temp = strtod(out, &end);
	int ok = !errno && end && end != out;
	free(out);
	if (!ok) die("cpu-temp parse failed");
	return num(temp);
}

static int template_arg_allowed(const char *arg)
{
	static const char *allowed[] = {
		"-mdoc", "-ms", "-org", "-man", "-tex", "-roff", "-c99",
		"-md", "-arrg", "-resume", "-html", "-json", "-sdl2",
		"-makefile", "-sed", NULL
	};
	for (int i = 0; allowed[i]; i++)
		if (strcmp(arg, allowed[i]) == 0) return 1;
	return 0;
}

static Val *b_eztemplate(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("eztemplate arity");

	char *arg = need_str(car(a));
	if (!template_arg_allowed(arg)) die("eztemplate: unsupported template type");

	char cmd[128];
	if (strcmp(arg, "-c99") == 0 || strcmp(arg, "-sdl2") == 0)
		snprintf(cmd, sizeof(cmd), "./scripts/eztemplate.sh %s newfile", arg);
	else
		snprintf(cmd, sizeof(cmd), "./scripts/eztemplate.sh %s", arg);
	char *out = read_command(cmd);
	Val *r = str(out);
	free(out);
	return r;
}

static Val *b_apply(Val *a, Env *e)
{
	if (len(a) != 2) die("apply arity");
	return apply(car(a), cadr(a), e);
}

static Val *b_quit(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 0) die("quit arity");
	termbox_cleanup();
	exit(0);
}

static Val *b_exit(Val *a, Env *e)
{
	(void)e;
	int n = len(a);
	if (n > 1) die("exit arity");
	termbox_cleanup();
	exit(n == 0 ? 0 : (int)need_num(car(a)));
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) die("%s: %s", path, strerror(errno));
	if (fseek(f, 0, SEEK_END) != 0) die("seek failed");
	long n = ftell(f);
	if (n < 0) die("tell failed");
	rewind(f);
	char *buf = xmalloc((size_t)n + 1);
	if (fread(buf, 1, (size_t)n, f) != (size_t)n) die("read failed");
	buf[n] = 0;
	fclose(f);
	return buf;
}

static int absolute_path(const char *path)
{
	return path[0] == '/';
}

static char *path_dirname(const char *path)
{
	const char *slash = strrchr(path, '/');
	if (!slash) return xstrdup(".");
	if (slash == path) return xstrdup("/");
	size_t n = (size_t)(slash - path);
	char *dir = xmalloc(n + 1);
	memcpy(dir, path, n);
	dir[n] = 0;
	return dir;
}

static char *resolve_load_path(const char *path)
{
	if (absolute_path(path) || !current_load_dir || strcmp(current_load_dir, ".") == 0)
		return xstrdup(path);
	size_t n = strlen(current_load_dir) + 1 + strlen(path) + 1;
	char *full = xmalloc(n);
	snprintf(full, n, "%s/%s", current_load_dir, path);
	return full;
}

static char *read_stream(FILE *f)
{
	size_t cap = 4096, n = 0;
	char *buf = xmalloc(cap);
	int c;
	while ((c = fgetc(f)) != EOF) {
		if (n + 1 >= cap) {
			cap *= 2;
			char *next = realloc(buf, cap);
			if (!next) die("out of memory");
			buf = next;
		}
		buf[n++] = (char)c;
	}
	buf[n] = 0;
	return buf;
}

static Val *eval_source(const char *src, Env *e)
{
	Reader r = { src, 0 };
	Val *v, *last = NIL;
	while ((v = read_expr(&r))) last = eval(v, e);
	return last;
}

static Val *eval_file(const char *path, Env *e)
{
	char *src = read_file(path);
	char *dir = path_dirname(path);
	char *prev = current_load_dir;
	current_load_dir = dir;
	Val *r = eval_source(src, e);
	current_load_dir = prev;
	free(dir);
	free(src);
	return r;
}

static void eval_and_print(const char *src, Env *e)
{
	Val *result = eval_source(src, e);
	if (!(swamp_tb_active && is_nil(result))) {
		print_val(result);
		swamp_putc('\n');
		if (swamp_tb_active) tb_present();
	}
}

static Val *b_load(Val *a, Env *e)
{
	if (len(a) != 1) die("load arity");
	char *path = resolve_load_path(need_str(car(a)));
	Val *r = eval_file(path, e);
	free(path);
	return r;
}

static Val *b_load_relative(Val *a, Env *e)
{
	return b_load(a, e);
}

static Val *b_current_load_directory(Val *a, Env *e)
{
	(void)e;
	if (!is_nil(a)) die("current-load-directory arity");
	return str(current_load_dir ? current_load_dir : ".");
}

static Val *b_resolve_load_path(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("resolve-load-path arity");
	char *path = resolve_load_path(need_str(car(a)));
	Val *r = str(path);
	free(path);
	return r;
}

static Val *b_readfile(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("read-file arity");
	char *src = read_file(need_str(car(a)));
	Val *r = str(src);
	free(src);
	return r;
}

static Val *b_writefile(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("write-file arity");
	const char *path = need_str(car(a));
	const char *text = need_str(cadr(a));
	FILE *f = fopen(path, "wb");
	if (!f) die("%s: %s", path, strerror(errno));
	size_t n = strlen(text);
	if (fwrite(text, 1, n, f) != n) {
		fclose(f);
		die("%s: write failed", path);
	}
	if (fclose(f) != 0) die("%s: close failed", path);
	return TRUE;
}

static Val *b_fileexistsp(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("file-exists? arity");
	return boolean(access(need_str(car(a)), F_OK) == 0);
}

static int string_ptr_cmp(const void *a, const void *b)
{
	const char *sa = *(const char * const *)a;
	const char *sb = *(const char * const *)b;
	return strcmp(sa, sb);
}

static Val *b_directory_files(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("directory-files arity");
	const char *path = need_str(car(a));
	DIR *d = opendir(path);
	if (!d) die("%s: %s", path, strerror(errno));
	size_t cap = 32, n = 0;
	char **names = xmalloc(sizeof(*names) * cap);
	struct dirent *de;
	while ((de = readdir(d))) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
		if (n == cap) {
			cap *= 2;
			char **next = realloc(names, sizeof(*names) * cap);
			if (!next) die("out of memory");
			names = next;
		}
		names[n++] = xstrdup(de->d_name);
	}
	if (closedir(d) != 0) die("%s: closedir failed", path);
	qsort(names, n, sizeof(*names), string_ptr_cmp);
	Val *r = NIL;
	for (size_t i = n; i > 0; i--) {
		r = cons(str(names[i - 1]), r);
		free(names[i - 1]);
	}
	free(names);
	return r;
}

static int stat_path(const char *path, struct stat *st)
{
	return stat(path, st) == 0;
}

static Val *b_file_directoryp(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("file-directory? arity");
	struct stat st;
	return boolean(stat_path(need_str(car(a)), &st) && S_ISDIR(st.st_mode));
}

static Val *b_file_regularp(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("file-regular? arity");
	struct stat st;
	return boolean(stat_path(need_str(car(a)), &st) && S_ISREG(st.st_mode));
}

static Val *b_file_size(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("file-size arity");
	struct stat st;
	if (!stat_path(need_str(car(a)), &st)) die("%s: %s", need_str(car(a)), strerror(errno));
	return num((double)st.st_size);
}

static Val *b_file_mtime(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("file-mtime arity");
	struct stat st;
	if (!stat_path(need_str(car(a)), &st)) die("%s: %s", need_str(car(a)), strerror(errno));
	return num((double)st.st_mtime);
}

static Val *b_make_directory(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("make-directory arity");
	if (mkdir(need_str(car(a)), 0777) != 0) die("%s: %s", need_str(car(a)), strerror(errno));
	return TRUE;
}

static Val *b_delete_file(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("delete-file arity");
	if (unlink(need_str(car(a))) != 0) die("%s: %s", need_str(car(a)), strerror(errno));
	return TRUE;
}

static Val *b_delete_directory(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 1) die("delete-directory arity");
	if (rmdir(need_str(car(a))) != 0) die("%s: %s", need_str(car(a)), strerror(errno));
	return TRUE;
}

static Val *b_rename_file(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 2) die("rename-file arity");
	if (rename(need_str(car(a)), need_str(cadr(a))) != 0)
		die("%s -> %s: %s", need_str(car(a)), need_str(cadr(a)), strerror(errno));
	return TRUE;
}

static Val *b_input_terminalp(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 0) die("input-terminal? arity");
	return boolean(isatty(STDIN_FILENO));
}

static Val *b_commandline(Val *a, Env *e)
{
	(void)e;
	if (len(a) != 0) die("command-line arity");
	Val *r = NIL;
	for (int i = saved_argc - 1; i >= 0; i--) r = cons(str(saved_argv[i]), r);
	return r;
}

static void add(Env *e, const char *name, Native f) { env_def(e, name, native(f)); }

static Env *base_env(void)
{
	Env *e = env_new(NULL);
	STDIN_PORT->as.port.f = stdin;
	STDIN_PORT->as.port.input = 1;
	STDIN_PORT->as.port.output = 0;
	STDIN_PORT->as.port.close = 0;
	STDOUT_PORT->as.port.f = stdout;
	STDOUT_PORT->as.port.input = 0;
	STDOUT_PORT->as.port.output = 1;
	STDOUT_PORT->as.port.close = 0;
	env_def(e, "#t", TRUE);
	env_def(e, "#f", FALSE);
	add(e, "+", b_add); add(e, "-", b_sub); add(e, "*", b_mul); add(e, "/", b_div);
	add(e, "abs", b_abs); add(e, "min", b_min); add(e, "max", b_max);
	add(e, "floor", b_floor); add(e, "ceiling", b_ceiling); add(e, "round", b_round); add(e, "truncate", b_truncate);
	add(e, "sqrt", b_sqrt); add(e, "expt", b_expt); add(e, "exp", b_exp); add(e, "log", b_log);
	add(e, "sin", b_sin); add(e, "cos", b_cos); add(e, "tan", b_tan); add(e, "asin", b_asin); add(e, "acos", b_acos); add(e, "atan", b_atan);
	add(e, "modulo", b_modulo); add(e, "remainder", b_remainder);
	add(e, "gcd", b_gcd); add(e, "lcm", b_lcm);
	add(e, "finite?", b_finitep); add(e, "infinite?", b_infinitep); add(e, "nan?", b_nanp);
	add(e, "zero?", b_zerop); add(e, "positive?", b_positivep); add(e, "negative?", b_negativep);
	add(e, "odd?", b_oddp); add(e, "even?", b_evenp);
	add(e, "=", b_eqn); add(e, "<", b_lt); add(e, ">", b_gt); add(e, "<=", b_le); add(e, ">=", b_ge);
	add(e, "bitwise-ior", b_bitor); add(e, "bitwise-and", b_bitand);
	add(e, "cons", b_cons); add(e, "car", b_car); add(e, "cdr", b_cdr); add(e, "list", b_list);
	add(e, "map", b_map); add(e, "for-each", b_foreach); add(e, "append", b_append);
	add(e, "vector", b_vector); add(e, "make-vector", b_make_vector);
	add(e, "vector-length", b_vector_length); add(e, "vector-ref", b_vector_ref); add(e, "vector-copy", b_vector_copy); add(e, "vector-copy!", b_vector_copy_in); add(e, "vector-append", b_vector_append);
	add(e, "vector-set!", b_vector_set); add(e, "vector-fill!", b_vector_fill);
	add(e, "list->vector", b_list_to_vector); add(e, "vector->list", b_vector_to_list);
	add(e, "gap-buffer?", b_gap_bufferp); add(e, "make-gap-buffer", b_make_gap_buffer);
	add(e, "gap-buffer-length", b_gap_buffer_length); add(e, "gap-buffer-point", b_gap_buffer_point);
	add(e, "gap-buffer-set-point!", b_gap_buffer_set_point);
	add(e, "gap-buffer->string", b_gap_buffer_to_string); add(e, "gap-buffer-ref", b_gap_buffer_ref);
	add(e, "gap-buffer-substring", b_gap_buffer_substring);
	add(e, "gap-buffer-insert!", b_gap_buffer_insert); add(e, "gap-buffer-delete!", b_gap_buffer_delete);
	add(e, "gap-buffer-line-count", b_gap_buffer_line_count);
	add(e, "gap-buffer-line-number-at", b_gap_buffer_line_number_at);
	add(e, "gap-buffer-line-start", b_gap_buffer_line_start);
	add(e, "gap-buffer-line-start-from", b_gap_buffer_line_start_from);
	add(e, "gap-buffer-line-end-from", b_gap_buffer_line_end_from);
	add(e, "gap-buffer-find-next", b_gap_buffer_find_next);
	add(e, "gap-buffer-find-prev", b_gap_buffer_find_prev);
	add(e, "record?", b_recordp); add(e, "make-record", b_make_record);
	add(e, "record-type", b_record_type); add(e, "record-fields", b_record_fields);
	add(e, "record-ref", b_record_ref); add(e, "record-set!", b_record_set);
	add(e, "gc", b_gc); add(e, "gc-stats", b_gc_stats);
	add(e, "make-hash-table", b_make_hash_table); add(e, "hash-table?", b_hash_tablep);
	add(e, "hash-set!", b_hash_set); add(e, "hash-ref", b_hash_ref);
	add(e, "hash-exists?", b_hash_exists); add(e, "hash-delete!", b_hash_delete);
	add(e, "hash-keys", b_hash_keys); add(e, "hash-count", b_hash_count);
	add(e, "caar", b_caar); add(e, "cdar", b_cdar);
	add(e, "cadr", b_cadr); add(e, "cddr", b_cddr); add(e, "caddr", b_caddr); add(e, "cadddr", b_cadddr);
	add(e, "null?", b_nullp); add(e, "pair?", b_pairp); add(e, "list?", b_listp); add(e, "symbol?", b_symbolp);
	add(e, "string?", b_stringp); add(e, "number?", b_numberp); add(e, "boolean?", b_booleanp); add(e, "boolean=?", b_boolean_eq); add(e, "vector?", b_vectorp);
	add(e, "eof-object?", b_eofp); add(e, "not", b_not);
	add(e, "length", b_length); add(e, "reverse", b_reverse); add(e, "list-ref", b_listref); add(e, "list-tail", b_list_tail); add(e, "list-copy", b_list_copy);
	add(e, "plist-ref", b_plistref);
	add(e, "memq", b_memq); add(e, "memv", b_memv); add(e, "member", b_member);
	add(e, "assq", b_assq); add(e, "assv", b_assv); add(e, "assoc", b_assoc);
	add(e, "eq?", b_eqp); add(e, "eqv?", b_eqp); add(e, "equal?", b_equalp);
	add(e, "display", b_display); add(e, "newline", b_newline);
	add(e, "string-length", b_strlen); add(e, "make-string", b_make_string);
	add(e, "string-append", b_strappend); add(e, "string-copy", b_string_copy); add(e, "string-copy!", b_string_copy_in); add(e, "string-fill!", b_string_fill); add(e, "string-downcase", b_strdown);
	add(e, "substring", b_substring); add(e, "string-ref", b_stringref); add(e, "integer->char", b_charint);
	add(e, "char->integer", b_intchar);
	add(e, "char?", b_charp);
	add(e, "char=?", b_chareq); add(e, "char<?", b_charlt); add(e, "char>?", b_chargt);
	add(e, "char<=?", b_charle); add(e, "char>=?", b_charge);
	add(e, "char-ci=?", b_charcieq); add(e, "char-ci<?", b_charcilt); add(e, "char-ci>?", b_charcigt);
	add(e, "char-ci<=?", b_charcile); add(e, "char-ci>=?", b_charcige);
	add(e, "char-alphabetic?", b_char_alphabeticp); add(e, "char-numeric?", b_char_numericp);
	add(e, "char-whitespace?", b_char_whitespacep);
	add(e, "char-upper-case?", b_char_uppercasep); add(e, "char-lower-case?", b_char_lowercasep);
	add(e, "char-upcase", b_char_upcase); add(e, "char-downcase", b_char_downcase);
	add(e, "string=?", b_streq); add(e, "string<?", b_strlt); add(e, "string>?", b_strgt);
	add(e, "string<=?", b_strle); add(e, "string>=?", b_strge);
	add(e, "string-ci=?", b_strcieq); add(e, "string-ci<?", b_strcilt); add(e, "string-ci>?", b_strcigt);
	add(e, "string-ci<=?", b_strcile); add(e, "string-ci>=?", b_strcige);
	add(e, "symbol->string", b_symbol_to_string); add(e, "string->symbol", b_string_to_symbol);
	add(e, "string-trim-both", b_strtrim); add(e, "string-contains", b_strcontains);
	add(e, "string-join", b_strjoin); add(e, "string-tokenize", b_tokenize);
	add(e, "number->string", b_numstr); add(e, "string->number", b_strnum);
	add(e, "read", b_read); add(e, "write", b_write);
	add(e, "read-char", b_read_char); add(e, "peek-char", b_peek_char); add(e, "write-char", b_write_char);
	add(e, "read-line", b_readline);
	add(e, "open-input-file", b_open_input_file); add(e, "open-output-file", b_open_output_file);
	add(e, "close-port", b_close_port); add(e, "close-input-port", b_close_port); add(e, "close-output-port", b_close_port);
	add(e, "flush-output-port", b_flush_output_port);
	add(e, "current-input-port", b_curin); add(e, "current-output-port", b_curout);
	add(e, "port?", b_portp); add(e, "input-port?", b_inputportp); add(e, "output-port?", b_outputportp);
	add(e, "input-terminal?", b_input_terminalp); add(e, "command-line", b_commandline);
	add(e, "random-source-randomize!", b_randomize); add(e, "random-integer", b_randint); add(e, "random-real", b_randreal);
	add(e, "current-time", b_now); add(e, "localtime", b_localtime); add(e, "strftime", b_strftime);
	add(e, "cpu-temp", b_cpu_temp); add(e, "eztemplate", b_eztemplate);
	add(e, "system", b_system); add(e, "system/status", b_system_status);
	add(e, "error", b_error); add(e, "last-error", b_last_error);
	add(e, "termbox", b_termbox); add(e, "termbox-demo", b_termbox_demo);
#ifdef WITH_SDL3
	add(e, "sdl3", b_sdl3); add(e, "nuklear", b_nuklear);
	env_def(e, "SDL_EVENT_QUIT", num((double)SDL_EVENT_QUIT));
	env_def(e, "SDL_EVENT_KEY_DOWN", num((double)SDL_EVENT_KEY_DOWN));
	env_def(e, "SDL_EVENT_KEY_UP", num((double)SDL_EVENT_KEY_UP));
	env_def(e, "SDL_EVENT_MOUSE_MOTION", num((double)SDL_EVENT_MOUSE_MOTION));
	env_def(e, "SDL_EVENT_MOUSE_BUTTON_DOWN", num((double)SDL_EVENT_MOUSE_BUTTON_DOWN));
		env_def(e, "SDL_EVENT_MOUSE_BUTTON_UP", num((double)SDL_EVENT_MOUSE_BUTTON_UP));
		env_def(e, "SDL_EVENT_TEXT_INPUT", num((double)SDL_EVENT_TEXT_INPUT));
		env_def(e, "SDL_EVENT_WINDOW_RESIZED", num((double)SDL_EVENT_WINDOW_RESIZED));
		env_def(e, "SDLK_ESCAPE", num((double)SDLK_ESCAPE));
		env_def(e, "SDLK_RETURN", num((double)SDLK_RETURN));
		env_def(e, "SDLK_BACKSPACE", num((double)SDLK_BACKSPACE));
		env_def(e, "SDLK_DELETE", num((double)SDLK_DELETE));
		env_def(e, "SDLK_TAB", num((double)SDLK_TAB));
		env_def(e, "SDLK_HOME", num((double)SDLK_HOME));
		env_def(e, "SDLK_END", num((double)SDLK_END));
		env_def(e, "SDLK_C", num((double)SDLK_C));
		env_def(e, "SDLK_V", num((double)SDLK_V));
		env_def(e, "SDLK_X", num((double)SDLK_X));
		env_def(e, "SDLK_Z", num((double)SDLK_Z));
		env_def(e, "SDL_KMOD_CTRL", num((double)SDL_KMOD_CTRL));
		env_def(e, "SDL_KMOD_SHIFT", num((double)SDL_KMOD_SHIFT));
		env_def(e, "SDL_KMOD_ALT", num((double)SDL_KMOD_ALT));
	env_def(e, "SDLK_UP", num((double)SDLK_UP));
	env_def(e, "SDLK_DOWN", num((double)SDLK_DOWN));
	env_def(e, "SDLK_LEFT", num((double)SDLK_LEFT));
	env_def(e, "SDLK_RIGHT", num((double)SDLK_RIGHT));
	env_def(e, "SDLK_W", num((double)SDLK_W));
	env_def(e, "SDLK_A", num((double)SDLK_A));
	env_def(e, "SDLK_S", num((double)SDLK_S));
	env_def(e, "SDLK_D", num((double)SDLK_D));
	env_def(e, "SDLK_SPACE", num((double)SDLK_SPACE));
#endif
	add(e, "apply", b_apply); add(e, "load", b_load); add(e, "import", b_load); add(e, "load-relative", b_load_relative);
	add(e, "current-load-directory", b_current_load_directory);
	add(e, "resolve-load-path", b_resolve_load_path);
	add(e, "read-file", b_readfile); add(e, "write-file", b_writefile); add(e, "file-exists?", b_fileexistsp);
	add(e, "directory-files", b_directory_files);
	add(e, "file-directory?", b_file_directoryp); add(e, "file-regular?", b_file_regularp);
	add(e, "file-size", b_file_size); add(e, "file-mtime", b_file_mtime);
	add(e, "make-directory", b_make_directory); add(e, "delete-file", b_delete_file);
	add(e, "delete-directory", b_delete_directory); add(e, "remove-directory", b_delete_directory);
	add(e, "rename-file", b_rename_file);
	add(e, "quit", b_quit); add(e, "exit", b_exit);
	env_def(e, "tb_init", str("tb_init"));
	env_def(e, "tb_clear", str("tb_clear"));
	env_def(e, "tb_present", str("tb_present"));
	env_def(e, "tb_shutdown", str("tb_shutdown"));
	env_def(e, "tb_poll_event", str("poll-event"));
	env_def(e, "tb_peek_event", str("peek-event"));
	env_def(e, "TB_DEFAULT", num((double)TB_DEFAULT));
	env_def(e, "TB_BLACK", num((double)TB_BLACK));
	env_def(e, "TB_RED", num((double)TB_RED));
	env_def(e, "TB_GREEN", num((double)TB_GREEN));
	env_def(e, "TB_YELLOW", num((double)TB_YELLOW));
	env_def(e, "TB_BLUE", num((double)TB_BLUE));
	env_def(e, "TB_MAGENTA", num((double)TB_MAGENTA));
	env_def(e, "TB_CYAN", num((double)TB_CYAN));
	env_def(e, "TB_WHITE", num((double)TB_WHITE));
	env_def(e, "TB_BOLD", num((double)TB_BOLD));
	env_def(e, "TB_UNDERLINE", num((double)TB_UNDERLINE));
	env_def(e, "TB_REVERSE", num((double)TB_REVERSE));
	env_def(e, "TB_ITALIC", num((double)TB_ITALIC));
	env_def(e, "TB_EVENT_KEY", num((double)TB_EVENT_KEY));
	env_def(e, "TB_EVENT_RESIZE", num((double)TB_EVENT_RESIZE));
	env_def(e, "TB_EVENT_MOUSE", num((double)TB_EVENT_MOUSE));
	env_def(e, "TB_MOD_ALT", num((double)TB_MOD_ALT));
	env_def(e, "TB_MOD_CTRL", num((double)TB_MOD_CTRL));
	env_def(e, "TB_MOD_SHIFT", num((double)TB_MOD_SHIFT));
	env_def(e, "TB_MOD_MOTION", num((double)TB_MOD_MOTION));
	env_def(e, "TB_INPUT_CURRENT", num((double)TB_INPUT_CURRENT));
	env_def(e, "TB_INPUT_ESC", num((double)TB_INPUT_ESC));
	env_def(e, "TB_INPUT_ALT", num((double)TB_INPUT_ALT));
	env_def(e, "TB_INPUT_MOUSE", num((double)TB_INPUT_MOUSE));
	env_def(e, "TB_OUTPUT_CURRENT", num((double)TB_OUTPUT_CURRENT));
	env_def(e, "TB_OUTPUT_NORMAL", num((double)TB_OUTPUT_NORMAL));
	env_def(e, "TB_OUTPUT_256", num((double)TB_OUTPUT_256));
	env_def(e, "TB_OUTPUT_216", num((double)TB_OUTPUT_216));
	env_def(e, "TB_OUTPUT_GRAYSCALE", num((double)TB_OUTPUT_GRAYSCALE));
	env_def(e, "TB_OUTPUT_TRUECOLOR", num((double)TB_OUTPUT_TRUECOLOR));
	env_def(e, "TB_KEY_ESC", num((double)TB_KEY_ESC));
	env_def(e, "TB_KEY_ENTER", num((double)TB_KEY_ENTER));
	env_def(e, "TB_KEY_SPACE", num((double)TB_KEY_SPACE));
	env_def(e, "TB_KEY_BACKSPACE", num((double)TB_KEY_BACKSPACE));
	env_def(e, "TB_KEY_BACKSPACE2", num((double)TB_KEY_BACKSPACE2));
	env_def(e, "TB_KEY_TAB", num((double)TB_KEY_TAB));
	env_def(e, "TB_KEY_CTRL_C", num((double)TB_KEY_CTRL_C));
	env_def(e, "TB_KEY_CTRL_D", num((double)TB_KEY_CTRL_D));
	env_def(e, "TB_KEY_CTRL_Q", num((double)TB_KEY_CTRL_Q));
	env_def(e, "TB_KEY_ARROW_UP", num((double)TB_KEY_ARROW_UP));
	env_def(e, "TB_KEY_ARROW_DOWN", num((double)TB_KEY_ARROW_DOWN));
	env_def(e, "TB_KEY_ARROW_LEFT", num((double)TB_KEY_ARROW_LEFT));
	env_def(e, "TB_KEY_ARROW_RIGHT", num((double)TB_KEY_ARROW_RIGHT));
	env_def(e, "TB_KEY_INSERT", num((double)TB_KEY_INSERT));
	env_def(e, "TB_KEY_DELETE", num((double)TB_KEY_DELETE));
	env_def(e, "TB_KEY_HOME", num((double)TB_KEY_HOME));
	env_def(e, "TB_KEY_END", num((double)TB_KEY_END));
	env_def(e, "TB_KEY_PGUP", num((double)TB_KEY_PGUP));
	env_def(e, "TB_KEY_PGDN", num((double)TB_KEY_PGDN));
	env_def(e, "TB_KEY_F1", num((double)TB_KEY_F1));
	env_def(e, "TB_KEY_F2", num((double)TB_KEY_F2));
	env_def(e, "TB_KEY_F3", num((double)TB_KEY_F3));
	env_def(e, "TB_KEY_F4", num((double)TB_KEY_F4));
	env_def(e, "TB_KEY_F5", num((double)TB_KEY_F5));
	env_def(e, "TB_KEY_F6", num((double)TB_KEY_F6));
	env_def(e, "TB_KEY_F7", num((double)TB_KEY_F7));
	env_def(e, "TB_KEY_F8", num((double)TB_KEY_F8));
	env_def(e, "TB_KEY_F9", num((double)TB_KEY_F9));
	env_def(e, "TB_KEY_F10", num((double)TB_KEY_F10));
	env_def(e, "TB_KEY_F11", num((double)TB_KEY_F11));
	env_def(e, "TB_KEY_F12", num((double)TB_KEY_F12));
	env_def(e, "TB_KEY_MOUSE_LEFT", num((double)TB_KEY_MOUSE_LEFT));
	env_def(e, "TB_KEY_MOUSE_RIGHT", num((double)TB_KEY_MOUSE_RIGHT));
	env_def(e, "TB_KEY_MOUSE_MIDDLE", num((double)TB_KEY_MOUSE_MIDDLE));
	env_def(e, "TB_KEY_MOUSE_RELEASE", num((double)TB_KEY_MOUSE_RELEASE));
	env_def(e, "TB_KEY_MOUSE_WHEEL_UP", num((double)TB_KEY_MOUSE_WHEEL_UP));
	env_def(e, "TB_KEY_MOUSE_WHEEL_DOWN", num((double)TB_KEY_MOUSE_WHEEL_DOWN));
	env_def(e, "default-random-source", NIL);
	env_def(e, "char-set:letter+digit", NIL);
	return e;
}

enum {
	KEY_NONE = 0,
	KEY_CTRL_C = 3,
	KEY_CTRL_D = 4,
	KEY_BACKSPACE = 127,
	KEY_ARROW_LEFT = 1000,
	KEY_ARROW_RIGHT,
	KEY_ARROW_UP,
	KEY_ARROW_DOWN,
	KEY_DELETE,
	KEY_HOME,
	KEY_END,
	KEY_PASTE_START,
	KEY_PASTE_END
};

static int source_complete(const char *s)
{
	int depth = 0;
	int in_string = 0;
	int escaped = 0;
	int comment = 0;

	for (size_t i = 0; s[i]; i++) {
		unsigned char c = (unsigned char)s[i];
		if (comment) {
			if (c == '\n') comment = 0;
			continue;
		}
		if (in_string) {
			if (escaped) escaped = 0;
			else if (c == '\\') escaped = 1;
			else if (c == '"') in_string = 0;
			continue;
		}
		if (c == ';') comment = 1;
		else if (c == '"') in_string = 1;
		else if (c == '(') depth++;
		else if (c == ')') depth--;
	}
	return depth <= 0 && !in_string && !escaped;
}

static int source_has_expr(const char *s)
{
	int comment = 0;
	for (size_t i = 0; s[i]; i++) {
		unsigned char c = (unsigned char)s[i];
		if (comment) {
			if (c == '\n') comment = 0;
		} else if (c == ';') {
			comment = 1;
		} else if (!isspace(c)) {
			return 1;
		}
	}
	return 0;
}

static void edit_insert_char(char *line, size_t cap, size_t *n, size_t *cursor, int ch)
{
	if (*n + 1 >= cap) return;
	memmove(line + *cursor + 1, line + *cursor, *n - *cursor + 1);
	line[(*cursor)++] = (char)ch;
	(*n)++;
}

static void history_add(char *history[16], const char *line)
{
	if (!*line) return;
	if (history[0] && strcmp(history[0], line) == 0) return;
	free(history[15]);
	for (int i = 15; i > 0; i--) history[i] = history[i - 1];
	history[0] = xstrdup(line);
}

static int plain_read_key(void)
{
	unsigned char c;
	if (read(STDIN_FILENO, &c, 1) != 1) return KEY_NONE;
	if (c != '\033') return c;

	unsigned char seq[6];
	if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\033';
	if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\033';
	if (seq[0] == '[') {
		if (seq[1] >= '0' && seq[1] <= '9') {
			if (read(STDIN_FILENO, &seq[2], 1) != 1) return KEY_NONE;
			if (seq[1] == '3' && seq[2] == '~') return KEY_DELETE;
			if (seq[1] == '2' && seq[2] == '0') {
				if (read(STDIN_FILENO, &seq[3], 1) != 1) return KEY_NONE;
				if (read(STDIN_FILENO, &seq[4], 1) != 1) return KEY_NONE;
				if (seq[3] == '0' && seq[4] == '~') return KEY_PASTE_START;
				if (seq[3] == '1' && seq[4] == '~') return KEY_PASTE_END;
			}
		} else {
			if (seq[1] == 'A') return KEY_ARROW_UP;
			if (seq[1] == 'B') return KEY_ARROW_DOWN;
			if (seq[1] == 'C') return KEY_ARROW_RIGHT;
			if (seq[1] == 'D') return KEY_ARROW_LEFT;
			if (seq[1] == 'H') return KEY_HOME;
			if (seq[1] == 'F') return KEY_END;
		}
	} else if (seq[0] == 'O') {
		if (seq[1] == 'H') return KEY_HOME;
		if (seq[1] == 'F') return KEY_END;
	}
	return KEY_NONE;
}

static void plain_redraw_line(const char *prompt, const char *line, size_t cursor)
{
	size_t n = strlen(line);
	printf("\r%s%s\033[K", prompt, line);
	if (n > cursor) printf("\033[%zuD", n - cursor);
	fflush(stdout);
}

static void tb_redraw_line(const char *prompt, const char *line, size_t cursor,
                           int start_x, int start_y)
{
	size_t prompt_n = strlen(prompt);
	swamp_tb_x = start_x;
	swamp_tb_y = start_y;
	swamp_puts(prompt);
	swamp_puts(line);
	for (int x = swamp_tb_x; x < tb_width(); x++)
		tb_set_cell(x, start_y, ' ', swamp_tb_fg, swamp_tb_bg);
	swamp_tb_x = start_x + (int)prompt_n + (int)cursor;
	swamp_tb_y = start_y;
	tb_set_cursor(swamp_tb_x, swamp_tb_y);
	tb_present();
}

static void history_recall(char *history[16], int *hist, int delta, char *saved,
                           char *line, size_t cap, size_t *n, size_t *cursor)
{
	if (delta > 0) {
		if (*hist == -1) snprintf(saved, cap, "%s", line);
		if (*hist < 15 && history[*hist + 1]) (*hist)++;
	} else {
		if (*hist > 0) (*hist)--;
		else if (*hist == 0) *hist = -1;
	}

	if (*hist >= 0) snprintf(line, cap, "%s", history[*hist]);
	else snprintf(line, cap, "%s", saved);
	*n = *cursor = strlen(line);
}

static int edit_line_plain(const char *prompt, char *line, size_t cap, char *history[16])
{
	struct termios old, raw;
	size_t n = 0, cursor = 0;
	int hist = -1;
	int paste = 0;
	char saved[4096];

	if (tcgetattr(STDIN_FILENO, &old) != 0) {
		fputs(prompt, stdout);
		fflush(stdout);
		if (!fgets(line, (int)cap, stdin)) return 0;
		n = strlen(line);
		if (n && line[n - 1] == '\n') line[n - 1] = 0;
		history_add(history, line);
		return 1;
	}

	raw = old;
	raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	printf("\033[?2004h");

	line[0] = saved[0] = 0;
	plain_redraw_line(prompt, line, cursor);
	for (;;) {
		int key = plain_read_key();
		if (key == KEY_NONE) continue;
		if (key == KEY_PASTE_START) {
			paste = 1;
			continue;
		}
		if (key == KEY_PASTE_END) {
			paste = 0;
			if (source_complete(line)) {
				line[n] = 0;
				printf("\033[?2004l");
				tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
				fputs("\r\n", stdout);
				history_add(history, line);
				return 1;
			}
			plain_redraw_line("... ", line, cursor);
			continue;
		}
		if (key == KEY_CTRL_C || key == KEY_CTRL_D) {
			printf("\033[?2004l");
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
			termbox_cleanup();
			fputs("\r\n", stdout);
			return 0;
		}
		if (key == '\r' || key == '\n') {
			if (paste || !source_complete(line)) {
				edit_insert_char(line, cap, &n, &cursor, '\n');
				if (paste) continue;
			} else {
				line[n] = 0;
				printf("\033[?2004l");
				tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
				fputs("\r\n", stdout);
				history_add(history, line);
				return 1;
			}
		}
		if (key == KEY_ARROW_UP || key == KEY_ARROW_DOWN) {
			history_recall(history, &hist, key == KEY_ARROW_UP ? 1 : -1,
			               saved, line, cap, &n, &cursor);
		} else if (key == KEY_ARROW_LEFT) {
			if (cursor > 0) cursor--;
		} else if (key == KEY_ARROW_RIGHT) {
			if (cursor < n) cursor++;
		} else if (key == KEY_HOME || key == 1) {
			cursor = 0;
		} else if (key == KEY_END || key == 5) {
			cursor = n;
		} else if (key == KEY_BACKSPACE || key == 8) {
			if (cursor > 0) {
				memmove(line + cursor - 1, line + cursor, n - cursor + 1);
				cursor--;
				n--;
			}
		} else if (key == KEY_DELETE) {
			if (cursor < n) {
				memmove(line + cursor, line + cursor + 1, n - cursor);
				n--;
			}
		} else if (key == 21) {
			n = cursor = 0;
			line[0] = 0;
		} else if (paste && key == '\t') {
			edit_insert_char(line, cap, &n, &cursor, key);
			continue;
		} else if (key >= 32 && key < 127 && n + 1 < cap) {
			edit_insert_char(line, cap, &n, &cursor, key);
			if (paste) continue;
		}
		plain_redraw_line(prompt, line, cursor);
	}
}

static int edit_line_termbox(const char *prompt, char *line, size_t cap, char *history[16])
{
	struct tb_event ev;
	size_t n = 0, cursor = 0;
	int hist = -1;
	char saved[4096];
	int start_x = swamp_tb_x;
	int start_y = swamp_tb_y;

	line[0] = saved[0] = 0;
	tb_redraw_line(prompt, line, cursor, start_x, start_y);
	for (;;) {
		if (tb_poll_event(&ev) < 0) return 0;
		if (ev.type != TB_EVENT_KEY) continue;
		if (ev.key == TB_KEY_CTRL_C || ev.key == TB_KEY_CTRL_D) {
			termbox_cleanup();
			return 0;
		}
		if (ev.key == TB_KEY_ENTER || ev.key == TB_KEY_CTRL_J || ev.key == TB_KEY_CTRL_M) {
			line[n] = 0;
			swamp_tb_x = 0;
			if (swamp_tb_y + 1 < tb_height()) swamp_tb_y++;
			tb_set_cursor(swamp_tb_x, swamp_tb_y);
			tb_present();
			history_add(history, line);
			return 1;
		}
		if (ev.key == TB_KEY_ARROW_UP || ev.key == TB_KEY_ARROW_DOWN) {
			history_recall(history, &hist, ev.key == TB_KEY_ARROW_UP ? 1 : -1,
			               saved, line, cap, &n, &cursor);
		} else if (ev.key == TB_KEY_ARROW_LEFT) {
			if (cursor > 0) cursor--;
		} else if (ev.key == TB_KEY_ARROW_RIGHT) {
			if (cursor < n) cursor++;
		} else if (ev.key == TB_KEY_HOME || ev.key == TB_KEY_CTRL_A) {
			cursor = 0;
		} else if (ev.key == TB_KEY_END || ev.key == TB_KEY_CTRL_E) {
			cursor = n;
		} else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2) {
			if (cursor > 0) {
				memmove(line + cursor - 1, line + cursor, n - cursor + 1);
				cursor--;
				n--;
			}
		} else if (ev.key == TB_KEY_DELETE) {
			if (cursor < n) {
				memmove(line + cursor, line + cursor + 1, n - cursor);
				n--;
			}
		} else if (ev.key == TB_KEY_CTRL_U) {
			n = cursor = 0;
			line[0] = 0;
		} else if (ev.ch >= 32 && ev.ch < 127 && n + 1 < cap) {
			memmove(line + cursor + 1, line + cursor, n - cursor + 1);
			line[cursor++] = (char)ev.ch;
			n++;
		}
		tb_redraw_line(prompt, line, cursor, start_x, start_y);
	}
}

static int repl_readline(const char *prompt, char *line, size_t cap, char *history[16])
{
	if (cap > 4096) die("repl line too large");
	if (swamp_tb_active) return edit_line_termbox(prompt, line, cap, history);
	if (isatty(STDIN_FILENO)) return edit_line_plain(prompt, line, cap, history);

	if (!fgets(line, (int)cap, stdin)) return 0;
	size_t n = strlen(line);
	if (n && line[n - 1] == '\n') line[n - 1] = 0;
	history_add(history, line);
	return 1;
}

static void repl(Env *e)
{
	char line[4096];
	char *history[16] = { NULL };
	jmp_buf repl_error;

	while (repl_readline("swamp> ", line, sizeof(line), history)) {
		repl_error_target = &repl_error;
		int prev_gc_roots = gc_root_count;
		if (setjmp(repl_error)) {
			gc_root_count = prev_gc_roots;
			repl_error_target = NULL;
			continue;
		}
		if (!source_has_expr(line)) {
			repl_error_target = NULL;
			continue;
		}
		Val *result = eval_source(line, e);
		if (!(swamp_tb_active && is_nil(result))) {
			print_val(result);
			swamp_putc('\n');
			if (swamp_tb_active) tb_present();
		}
		repl_error_target = NULL;
	}
	repl_error_target = NULL;
	for (int i = 0; i < 16; i++) free(history[i]);
	termbox_cleanup();
}

int main(int argc, char **argv)
{
	atexit(termbox_cleanup);
#ifdef WITH_SDL3
	atexit(sdl3_cleanup);
#endif
	saved_argc = argc;
	saved_argv = argv;
	Env *e = base_env();
	if (argc > 1 && strcmp(argv[1], "-e") == 0) {
		if (argc != 3) die("usage: swampscheme -e expression");
		eval_and_print(argv[2], e);
		return 0;
	}
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			eval_file(argv[i], e);
		}
		return 0;
	}
	if (!isatty(STDIN_FILENO)) {
		char *src = read_stream(stdin);
		eval_source(src, e);
		free(src);
		return 0;
	}
	repl(e);
	return 0;
}
