/*
 *                                                      ()-()
 * hidcur - Utility for hiding the mouse cursor in X11   \"/
 *                                                        `
 * This program is licensed under WTFPL, see COPYING for details.
 *
 * Ricardo Catalinas Jiménez <jimenezrick@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>

#define MOUSE_MASK XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS

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
static bool grab_pointer(x_connection_t xconn, xcb_window_t grab_win, xcb_cursor_t cursor);
static void ungrab_pointer(x_connection_t xconn);
static xcb_cursor_t create_invisible_cursor(x_connection_t xconn);
static void free_cursor(x_connection_t xconn, xcb_cursor_t cursor);
static xcb_window_t get_input_focus(x_connection_t xconn);
static pointer_info_t query_pointer(x_connection_t xconn);
static xcb_window_t create_input_window(x_connection_t xconn, xcb_window_t parent_win);
static void destroy_window(x_connection_t xconn, xcb_window_t win);
static void wait_pointer_idle(x_connection_t xconn, int interval);
static void wait_pointer_movement(x_connection_t xconn);
static bool hide_cursor(x_connection_t xconn, xcb_window_t *win);
static void show_cursor(x_connection_t xconn, xcb_window_t win);

static void error(const char *msg, x_connection_t xconn)
{
	fprintf(stderr, "Error: %s\n", msg);
	if (xconn.conn) disconnect_x(xconn);

	exit(EXIT_FAILURE);
}

static x_connection_t connect_x(void)
{
	x_connection_t xconn = {.conn = NULL};

	xconn.conn = xcb_connect(NULL, &xconn.screen_num);
	if (xcb_connection_has_error(xconn.conn))
		error("can't connect to server", xconn);

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
	int screen_num = xconn->screen_num;

	xconn->screen = NULL;
	it = xcb_setup_roots_iterator(xcb_get_setup(xconn->conn));
	for (; it.rem; xcb_screen_next(&it), screen_num--) {
		if (!screen_num) {
			xconn->screen = it.data;
			break;
		}
	}
	if (!xconn->screen) error("can't find screen", *xconn);
}

static bool grab_pointer(x_connection_t xconn, xcb_window_t grab_win, xcb_cursor_t cursor)
{
	xcb_grab_pointer_cookie_t cookie;
	xcb_grab_pointer_reply_t *reply;
	xcb_generic_error_t      *err;

	cookie = xcb_grab_pointer(xconn.conn, false, grab_win, MOUSE_MASK,
				  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
				  XCB_WINDOW_NONE, cursor,
				  XCB_TIME_CURRENT_TIME);
	reply = xcb_grab_pointer_reply(xconn.conn, cookie, &err);
	if (err) error("can't grab pointer", xconn);

	free_cursor(xconn, cursor);
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

static xcb_cursor_t create_invisible_cursor(x_connection_t xconn)
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

	cookie = xcb_free_gc_checked(xconn.conn, gc);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free graphics context", xconn);

	cookie = xcb_free_pixmap_checked(xconn.conn, pixmap);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free pixmap", xconn);

	return cursor;
}

static void free_cursor(x_connection_t xconn, xcb_cursor_t cursor)
{
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;

	cookie = xcb_free_cursor_checked(xconn.conn, cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free cursor", xconn);
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

static xcb_window_t create_input_window(x_connection_t xconn, xcb_window_t parent_win)
{
	xcb_void_cookie_t    cookie;
	xcb_window_t         win;
	xcb_generic_error_t *err;
	uint32_t             mask = MOUSE_MASK;

	win = xcb_generate_id(xconn.conn);
	cookie = xcb_create_window_checked(xconn.conn, 0, win, parent_win,
					   0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
					   XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create window", xconn);

	cookie = xcb_map_window_checked(xconn.conn, win);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't map window", xconn);

	return win;
}

static void destroy_window(x_connection_t xconn, xcb_window_t win)
{
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;

	cookie = xcb_destroy_window_checked(xconn.conn, win);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't destroy window", xconn);
}

static void wait_pointer_idle(x_connection_t xconn, int interval)
{
	pointer_info_t old_info, info;

	old_info = query_pointer(xconn);
	for (;;) {
		sleep(interval);
		info = query_pointer(xconn);
		if (info.x == old_info.x && info.y == old_info.y)
			break;
		old_info = info;
	}
}

static void wait_pointer_movement(x_connection_t xconn)
{
	xcb_generic_event_t *event;

	// TODO: Probar a quitar MOUSE_MASK de grab() o de create_win()
	while ((event = xcb_wait_for_event(xconn.conn)) != NULL) {
		switch (event->response_type) {
			case XCB_MOTION_NOTIFY:
				goto exit;
			case XCB_BUTTON_PRESS:
				goto exit;
			default:
				error("unknown event", xconn);
		}
	}
	if (!event) error("I/O error happened", xconn);
	// TODO: probar a hacer un disconnect_x() con un DISPLAY erroneo tras el connect_x()

exit:	free(event);
}

static bool hide_cursor(x_connection_t xconn, xcb_window_t *win)
{
	*win = create_input_window(xconn, get_input_focus(xconn));
	if (!grab_pointer(xconn, *win, create_invisible_cursor(xconn)))
		return false;

	return true;
}

static void show_cursor(x_connection_t xconn, xcb_window_t win)
{
	ungrab_pointer(xconn);
	destroy_window(xconn, win);
}

int main(int argc, char *argv[])
{
	x_connection_t xconn;
	xcb_window_t   win;

	xconn = connect_x();
	for (;;) {
		wait_pointer_idle(xconn, 2);
		if (!hide_cursor(xconn, &win))
			continue;
		wait_pointer_movement(xconn);
		show_cursor(xconn, win);
	}
	disconnect_x(xconn);

	return EXIT_SUCCESS;
}
