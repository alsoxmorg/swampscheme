#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define TB_IMPL
#include "../../termbox2.h"

#include <libguile.h>
#include <string.h>

static uintattr_t current_fg = TB_DEFAULT;
static uintattr_t current_bg = TB_DEFAULT;
static int current_x = 0;
static int current_y = 0;

static SCM arg_at(SCM args, int index)
{
	return scm_list_ref(args, scm_from_int(index));
}

static int arg_count(SCM args, const char *who)
{
	long n = scm_ilength(args);
	if (n < 0) scm_misc_error(who, "expected a proper argument list", SCM_EOL);
	return (int)n;
}

static void require_args(SCM args, int expected, const char *who)
{
	if (arg_count(args, who) != expected)
		scm_misc_error(who, "wrong number of arguments", SCM_EOL);
}

static char *scm_text(SCM value, const char *who)
{
	SCM string = scm_is_symbol(value) ? scm_symbol_to_string(value) : value;
	if (!scm_is_string(string)) scm_wrong_type_arg(who, 1, value);
	return scm_to_utf8_string(string);
}

static uintattr_t scm_attr(SCM value, const char *who)
{
	if (scm_is_number(value)) return (uintattr_t)scm_to_int(value);
	char *name = scm_text(value, who);
	uintattr_t result = TB_DEFAULT;
	int known = 1;
	if (!strcmp(name, "default")) result = TB_DEFAULT;
	else if (!strcmp(name, "black")) result = TB_BLACK;
	else if (!strcmp(name, "red")) result = TB_RED;
	else if (!strcmp(name, "green")) result = TB_GREEN;
	else if (!strcmp(name, "yellow")) result = TB_YELLOW;
	else if (!strcmp(name, "blue")) result = TB_BLUE;
	else if (!strcmp(name, "magenta")) result = TB_MAGENTA;
	else if (!strcmp(name, "cyan")) result = TB_CYAN;
	else if (!strcmp(name, "white")) result = TB_WHITE;
	else if (!strcmp(name, "bold")) result = TB_BOLD;
	else if (!strcmp(name, "underline")) result = TB_UNDERLINE;
	else if (!strcmp(name, "reverse")) result = TB_REVERSE;
	else if (!strcmp(name, "italic")) result = TB_ITALIC;
	else known = 0;
	free(name);
	if (!known) scm_misc_error(who, "unknown Termbox attribute", SCM_EOL);
	return result;
}

static uint32_t scm_char(SCM value, const char *who)
{
	if (scm_is_integer(value)) return (uint32_t)scm_to_int(value);
	char *text = scm_text(value, who);
	uint32_t result = 0;
	if (tb_utf8_char_to_unicode(&result, text) < 1) {
		free(text);
		scm_misc_error(who, "expected one UTF-8 character", SCM_EOL);
	}
	free(text);
	return result;
}

static SCM event_value(const struct tb_event *event)
{
	return scm_list_n(scm_from_utf8_symbol("type"), scm_from_int(event->type),
	                  scm_from_utf8_symbol("key"), scm_from_int(event->key),
	                  scm_from_utf8_symbol("ch"), scm_from_uint(event->ch),
	                  scm_from_utf8_symbol("mod"), scm_from_int(event->mod),
	                  scm_from_utf8_symbol("w"), scm_from_int(event->w),
	                  scm_from_utf8_symbol("h"), scm_from_int(event->h),
	                  scm_from_utf8_symbol("x"), scm_from_int(event->x),
	                  scm_from_utf8_symbol("y"), scm_from_int(event->y),
	                  SCM_UNDEFINED);
}

static SCM scm_termbox(SCM command, SCM args)
{
	char *cmd = scm_text(command, "termbox");
	int n = arg_count(args, "termbox");
	SCM result = SCM_EOL;

	if (!strcmp(cmd, "init") || !strcmp(cmd, "tb_init")) {
		if (n != 0) scm_misc_error("termbox", "init takes no arguments", SCM_EOL);
		if (tb_init() != TB_OK) scm_misc_error("termbox", "initialization failed", SCM_EOL);
		tb_set_output_mode(TB_OUTPUT_256);
		current_fg = TB_DEFAULT; current_bg = TB_DEFAULT; current_x = 0; current_y = 0;
		free(cmd);
		return result;
	}
	if (!strcmp(cmd, "shutdown") || !strcmp(cmd, "tb_shutdown")) {
		if (n != 0) scm_misc_error("termbox", "shutdown takes no arguments", SCM_EOL);
		tb_shutdown();
		free(cmd);
		return SCM_BOOL_T;
	}
	if (!strcmp(cmd, "clear") || !strcmp(cmd, "tb_clear")) {
		require_args(args, 0, "termbox clear");
		if (tb_clear() != TB_OK) scm_misc_error("termbox", "clear failed", SCM_EOL);
		current_x = current_y = 0;
	} else if (!strcmp(cmd, "set-clear-attrs") || !strcmp(cmd, "clear-attrs")) {
		require_args(args, 2, "termbox set-clear-attrs");
		if (tb_set_clear_attrs(scm_attr(arg_at(args, 0), "termbox set-clear-attrs"),
		                       scm_attr(arg_at(args, 1), "termbox set-clear-attrs")) != TB_OK)
			scm_misc_error("termbox", "set-clear-attrs failed", SCM_EOL);
	} else if (!strcmp(cmd, "present") || !strcmp(cmd, "tb_present")) {
		require_args(args, 0, "termbox present");
		if (tb_present() != TB_OK) scm_misc_error("termbox", "present failed", SCM_EOL);
	} else if (!strcmp(cmd, "setcolor") || !strcmp(cmd, "fg")) {
		require_args(args, 1, "termbox setcolor");
		current_fg = scm_attr(arg_at(args, 0), "termbox setcolor");
	} else if (!strcmp(cmd, "setbg") || !strcmp(cmd, "bg")) {
		require_args(args, 1, "termbox setbg");
		current_bg = scm_attr(arg_at(args, 0), "termbox setbg");
	} else if (!strcmp(cmd, "attrs")) {
		require_args(args, 2, "termbox attrs");
		current_fg = scm_attr(arg_at(args, 0), "termbox attrs");
		current_bg = scm_attr(arg_at(args, 1), "termbox attrs");
	} else if (!strcmp(cmd, "goto") || !strcmp(cmd, "move")) {
		require_args(args, 2, "termbox goto");
		current_x = scm_to_int(arg_at(args, 0));
		current_y = scm_to_int(arg_at(args, 1));
		if (current_x < 0) current_x = 0;
		if (current_y < 0) current_y = 0;
	} else if (!strcmp(cmd, "width")) {
		require_args(args, 0, "termbox width");
		result = scm_from_int(tb_width());
	} else if (!strcmp(cmd, "height")) {
		require_args(args, 0, "termbox height");
		result = scm_from_int(tb_height());
	} else if (!strcmp(cmd, "set-cursor") || !strcmp(cmd, "cursor")) {
		require_args(args, 2, "termbox set-cursor");
		if (tb_set_cursor(scm_to_int(arg_at(args, 0)), scm_to_int(arg_at(args, 1))) != TB_OK)
			scm_misc_error("termbox", "set-cursor failed", SCM_EOL);
	} else if (!strcmp(cmd, "hide-cursor")) {
		require_args(args, 0, "termbox hide-cursor");
		tb_hide_cursor();
	} else if (!strcmp(cmd, "extend-cell")) {
		require_args(args, 3, "termbox extend-cell");
		if (tb_extend_cell(scm_to_int(arg_at(args, 0)), scm_to_int(arg_at(args, 1)),
		                   scm_char(arg_at(args, 2), "termbox extend-cell")) != TB_OK)
			scm_misc_error("termbox", "extend-cell failed", SCM_EOL);
	} else if (!strcmp(cmd, "set-cell") || !strcmp(cmd, "cell")) {
		require_args(args, 5, "termbox set-cell");
		if (tb_set_cell(scm_to_int(arg_at(args, 0)), scm_to_int(arg_at(args, 1)),
		                scm_char(arg_at(args, 2), "termbox set-cell"),
		                scm_attr(arg_at(args, 3), "termbox set-cell"),
		                scm_attr(arg_at(args, 4), "termbox set-cell")) != TB_OK)
			scm_misc_error("termbox", "set-cell failed", SCM_EOL);
	} else if (!strcmp(cmd, "print-at")) {
		require_args(args, 5, "termbox print-at");
		char *text = scm_text(arg_at(args, 4), "termbox print-at");
		int rv = tb_print(scm_to_int(arg_at(args, 0)), scm_to_int(arg_at(args, 1)),
		                  scm_attr(arg_at(args, 2), "termbox print-at"),
		                  scm_attr(arg_at(args, 3), "termbox print-at"), text);
		free(text);
		if (rv < 0) scm_misc_error("termbox", "print-at failed", SCM_EOL);
	} else if (!strcmp(cmd, "print")) {
		require_args(args, 1, "termbox print");
		char *text = scm_text(arg_at(args, 0), "termbox print");
		int rv = tb_print(current_x, current_y, current_fg, current_bg, text);
		free(text);
		if (rv < 0) scm_misc_error("termbox", "print failed", SCM_EOL);
		current_x += rv;
	} else if (!strcmp(cmd, "send")) {
		require_args(args, 1, "termbox send");
		char *text = scm_text(arg_at(args, 0), "termbox send");
		int rv = tb_send(text, strlen(text));
		free(text);
		if (rv != TB_OK) scm_misc_error("termbox", "send failed", SCM_EOL);
	} else if (!strcmp(cmd, "last-errno")) {
		require_args(args, 0, "termbox last-errno");
		result = scm_from_int(tb_last_errno());
	} else if (!strcmp(cmd, "cell-at")) {
		require_args(args, 2, "termbox cell-at");
		int x = scm_to_int(arg_at(args, 0)), y = scm_to_int(arg_at(args, 1));
		if (x < 0 || y < 0 || x >= tb_width() || y >= tb_height())
			scm_misc_error("termbox", "cell-at out of bounds", SCM_EOL);
		struct tb_cell *cell = tb_cell_buffer() + y * tb_width() + x;
		result = scm_list_n(scm_from_utf8_symbol("ch"), scm_from_uint(cell->ch),
		                    scm_from_utf8_symbol("fg"), scm_from_uint(cell->fg),
		                    scm_from_utf8_symbol("bg"), scm_from_uint(cell->bg),
		                    SCM_UNDEFINED);
	} else if (!strcmp(cmd, "input-mode") || !strcmp(cmd, "set-input-mode")) {
		require_args(args, 1, "termbox input-mode");
		result = scm_from_int(tb_set_input_mode(scm_to_int(arg_at(args, 0))));
	} else if (!strcmp(cmd, "output-mode") || !strcmp(cmd, "set-output-mode")) {
		require_args(args, 1, "termbox output-mode");
		result = scm_from_int(tb_set_output_mode(scm_to_int(arg_at(args, 0))));
	} else if (!strcmp(cmd, "poll-event") || !strcmp(cmd, "event")) {
		require_args(args, 0, "termbox poll-event");
		struct tb_event event;
		if (tb_poll_event(&event) != TB_OK) scm_misc_error("termbox", "poll-event failed", SCM_EOL);
		result = event_value(&event);
	} else if (!strcmp(cmd, "peek-event")) {
		require_args(args, 1, "termbox peek-event");
		struct tb_event event;
		int rv = tb_peek_event(&event, scm_to_int(arg_at(args, 0)));
		result = rv == 0 ? SCM_BOOL_F : event_value(&event);
	} else {
		free(cmd);
		scm_misc_error("termbox", "unknown command", SCM_EOL);
	}

	free(cmd);
	return result;
}

void init_swamp_termbox(void)
{
	scm_c_define_gsubr("termbox", 1, 0, 1, scm_termbox);
#define DEFINE_CONST(name) scm_c_define(#name, scm_from_uint(name))
	DEFINE_CONST(TB_DEFAULT); DEFINE_CONST(TB_BLACK); DEFINE_CONST(TB_RED);
	DEFINE_CONST(TB_GREEN); DEFINE_CONST(TB_YELLOW); DEFINE_CONST(TB_BLUE);
	DEFINE_CONST(TB_MAGENTA); DEFINE_CONST(TB_CYAN); DEFINE_CONST(TB_WHITE);
	DEFINE_CONST(TB_BOLD); DEFINE_CONST(TB_UNDERLINE); DEFINE_CONST(TB_REVERSE);
	DEFINE_CONST(TB_ITALIC); DEFINE_CONST(TB_KEY_ARROW_UP);
	DEFINE_CONST(TB_KEY_ARROW_DOWN); DEFINE_CONST(TB_KEY_ARROW_LEFT);
	DEFINE_CONST(TB_KEY_ARROW_RIGHT); DEFINE_CONST(TB_KEY_ENTER); DEFINE_CONST(TB_KEY_ESC);
#undef DEFINE_CONST
}
