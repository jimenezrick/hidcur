#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // TODO: quitar
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>
#include <X11/cursorfont.h>

// TODO: sin usar
#define MOUSE_MASK  XCB_EVENT_MASK_POINTER_MOTION | \
		    XCB_EVENT_MASK_BUTTON_PRESS   | \
		    XCB_EVENT_MASK_BUTTON_RELEASE
// TODO TODO TODO

typedef struct {
	xcb_connection_t *conn;
	xcb_screen_t     *screen;
	int               screen_num;
} x_connection_t;

typedef struct {
	xcb_window_t root_win, child_win;
	bool         same_screen;
	int16_t      x, y;
} pointer_info_t;

static void error(const char *msg, x_connection_t xconn);
static x_connection_t connect_x(void);
static void disconnect_x(x_connection_t xconn);
static void set_screen(x_connection_t *xconn);
static bool grab_pointer(x_connection_t xconn, xcb_window_t grab_win);
static void ungrab_pointer(x_connection_t xconn);
static void restore_cursor(x_connection_t xconn);
static void hide_cursor(x_connection_t xconn);
static xcb_window_t get_input_focus(x_connection_t xconn);
static pointer_info_t query_pointer(x_connection_t xconn);
static xcb_window_t create_window(x_connection_t xconn, xcb_window_t parent_win);

static void error(const char *msg, x_connection_t xconn)
{
	fprintf(stderr, "Error: %s\n", msg);
	if (xconn.conn) disconnect_x(xconn);

	exit(EXIT_FAILURE);
}

static x_connection_t connect_x(void)
{
	x_connection_t xconn;

	xconn.conn = xcb_connect(NULL, &xconn.screen_num);
	if (!xconn.conn) error("can't connect to server", xconn);
	set_screen(&xconn);

	return xconn;
}

static void disconnect_x(x_connection_t xconn)
{
	xcb_disconnect(xconn.conn);
}

static void set_screen(x_connection_t *xconn)
{
	xcb_screen_iterator_t it;

	xconn->screen = NULL;
	it = xcb_setup_roots_iterator(xcb_get_setup(xconn->conn));
	for (; it.rem; xcb_screen_next(&it), xconn->screen_num--) {
		if (!xconn->screen_num) {
			xconn->screen = it.data;
			break;
		}
	}
	if (!xconn->screen) error("can't find screen", *xconn);
}

static bool grab_pointer(x_connection_t xconn, xcb_window_t grab_win)
{
	xcb_grab_pointer_cookie_t cookie;
	xcb_grab_pointer_reply_t *reply;
	xcb_generic_error_t      *err;

	cookie = xcb_grab_pointer(xconn.conn, false, grab_win, MOUSE_MASK,
				  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
				  XCB_WINDOW_NONE, XCB_CURSOR_NONE,
				  XCB_TIME_CURRENT_TIME);
	reply = xcb_grab_pointer_reply(xconn.conn, cookie, &err);
	if (err) error("can't grab pointer", xconn);

	if (reply->status != XCB_GRAB_STATUS_SUCCESS) {
		free(reply);

		return false;
	}
	free(reply);

	return true;
}

static void ungrab_pointer(x_connection_t xconn)
{
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;

	cookie = xcb_ungrab_pointer_checked(xconn.conn, XCB_CURRENT_TIME);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't ungrab pointer", xconn);
}

static void restore_cursor(x_connection_t xconn)
{
	xcb_font_t           font;
	xcb_cursor_t         cursor;
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;

	font = xcb_generate_id(xconn.conn);
	cookie = xcb_open_font_checked(xconn.conn, font, strlen("cursor"), "cursor");
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't load cursor font", xconn);

	cursor = xcb_generate_id(xconn.conn);
	cookie = xcb_create_glyph_cursor_checked(xconn.conn, cursor, font, font,
						 XC_left_ptr, XC_left_ptr + 1, 0, 0, 0,
						 UINT16_MAX, UINT16_MAX, UINT16_MAX);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create glyph cursor", xconn);

	cookie = xcb_change_window_attributes_checked(xconn.conn, xconn.screen->root,
						      XCB_CW_CURSOR, &cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't change window attributes", xconn);

	cookie = xcb_free_cursor_checked(xconn.conn, cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free cursor", xconn);

	cookie = xcb_close_font_checked(xconn.conn, font);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't close font", xconn);
}

static void hide_cursor(x_connection_t xconn)
{
	xcb_pixmap_t         pixmap;
	xcb_gcontext_t       gc;
	xcb_rectangle_t      rect = {.x = 0, .y = 0, .width = 1, .height = 1};
	xcb_cursor_t         cursor;
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;
	uint32_t             fun = XCB_GX_CLEAR;

	pixmap = xcb_generate_id(xconn.conn);
	cookie = xcb_create_pixmap_checked(xconn.conn, 1, pixmap,
					   xconn.screen->root, 1, 1);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create pixmap", xconn);

	gc = xcb_generate_id(xconn.conn);
	cookie = xcb_create_gc_checked(xconn.conn, gc, pixmap,
				       XCB_GC_FUNCTION, &fun);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create graphics context", xconn);

	cookie = xcb_poly_fill_rectangle_checked(xconn.conn, pixmap, gc, 1, &rect);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't fill rectangle", xconn);

	cursor = xcb_generate_id(xconn.conn);
	cookie = xcb_create_cursor_checked(xconn.conn, cursor, pixmap, pixmap,
					   0, 0, 0, 0, 0, 0, 0, 0);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create cursor", xconn);

	cookie = xcb_change_window_attributes_checked(xconn.conn, xconn.screen->root,
						      XCB_CW_CURSOR, &cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't change window attributes", xconn);

	cookie = xcb_free_cursor_checked(xconn.conn, cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free cursor", xconn);

	cookie = xcb_free_gc_checked(xconn.conn, gc);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free graphics context", xconn);

	cookie = xcb_free_pixmap_checked(xconn.conn, pixmap);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free pixmap", xconn);
}

static xcb_window_t get_input_focus(x_connection_t xconn)
{
	xcb_get_input_focus_cookie_t cookie;
	xcb_get_input_focus_reply_t *reply;
	xcb_generic_error_t         *err;
	xcb_window_t                 focus;

	cookie = xcb_get_input_focus(xconn.conn);
	reply = xcb_get_input_focus_reply(xconn.conn, cookie, &err);
	if (err) error("can't get input focus", xconn);

	focus = reply->focus;
	free(reply);

	return focus;
}

static pointer_info_t query_pointer(x_connection_t xconn)
{
	xcb_query_pointer_cookie_t cookie;
	xcb_query_pointer_reply_t *reply;
	xcb_generic_error_t       *err;
	pointer_info_t             info;

	cookie = xcb_query_pointer(xconn.conn, get_input_focus(xconn));
	reply = xcb_query_pointer_reply(xconn.conn, cookie, &err);
	if (err) error("can't query pointer", xconn);

	info.root_win = reply->root;
	info.child_win = reply->child;
	info.same_screen = reply->same_screen;
	info.x = reply->root_x;
	info.y = reply->root_y;
	free(reply);

	return info;
}

// TODO: create_corner_window
static xcb_window_t create_window(x_connection_t xconn, xcb_window_t parent_win)
{
	xcb_void_cookie_t    cookie;
	xcb_window_t         win;
	xcb_generic_error_t *err;

	// TODO: crear la ventana en la esquina derecha abajo
	win = xcb_generate_id(xconn.conn);
	cookie = xcb_create_window_checked(xconn.conn, 0, win, parent_win,
					   0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
					   xconn.screen->root_visual, 0, NULL);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create window", xconn);

	return win;
}

int main(int argc, char *argv[])
{
	x_connection_t xconn;
	pointer_info_t info;
	xcb_window_t   win;

	xconn = connect_x();
	info = query_pointer(xconn);
	printf("x = %d, y = %d\n", info.x, info.y);

	win = create_window(xconn, get_input_focus(xconn));
	xcb_map_window(xconn.conn, win);
	xcb_flush(xconn.conn);

	if (!grab_pointer(xconn, win))
		error("isn't possible to grab pointer", xconn);
	hide_cursor(xconn);
	xcb_flush(xconn.conn);

	sleep(4);

	ungrab_pointer(xconn);
	restore_cursor(xconn);

	disconnect_x(xconn);

	/*
	 * XXX XXX XXX
	 * each screen needs its own empty cursor.
	 * note each real root id so can find which screen we are on
	 * XXX XXX XXX
	 */

	return EXIT_SUCCESS;
}
