#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>

#define MOUSE_MASK  XCB_EVENT_MASK_POINTER_MOTION | \
		    XCB_EVENT_MASK_BUTTON_PRESS   | \
		    XCB_EVENT_MASK_BUTTON_RELEASE
#define CURSOR_CHAR 68

typedef struct {
	xcb_connection_t *conn;
	xcb_screen_t     *screen;
	int               screen_num;
} x_connection_t;

// XXX XXX XXX
// Cabeceras static:
static void disconnect_x(x_connection_t xconn);
static void set_screen(x_connection_t *xconn);
// XXX XXX XXX


void error(const char *msg, x_connection_t xconn)
{
	fprintf(stderr, "Error: %s\n", msg);
	if (xconn.conn) disconnect_x(xconn);

	exit(EXIT_FAILURE);
}

x_connection_t connect_x(void)
{
	x_connection_t xconn;

	xconn.conn = xcb_connect(NULL, &xconn.screen_num);
	if (!xconn.conn) error("can't connect to server", xconn);
	set_screen(&xconn);

	return xconn;
}

void disconnect_x(x_connection_t xconn)
{
	xcb_disconnect(xconn.conn);
}

void set_screen(x_connection_t *xconn)
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

bool grab_pointer(x_connection_t xconn)
{
	xcb_grab_pointer_cookie_t cookie;
	xcb_grab_pointer_reply_t *reply;
	xcb_generic_error_t      *err;

	// TODO: probar owner_events = true, confine_to = root_win
	cookie = xcb_grab_pointer(xconn.conn, false, xconn.screen->root, MOUSE_MASK,
				  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE,
				  XCB_CURSOR_NONE, XCB_TIME_CURRENT_TIME);
	reply = xcb_grab_pointer_reply(xconn.conn, cookie, &err);
	if (err) error("can't grab pointer", xconn);

	if (reply->status == XCB_GRAB_STATUS_SUCCESS) {
		free(reply);

		return true;
	} else {
		free(reply);

		return false;
	}
}

void ungrab_pointer(x_connection_t xconn)
{
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;

	cookie = xcb_ungrab_pointer_checked(xconn.conn, XCB_CURRENT_TIME);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't ungrab pointer", xconn);
}

void restore_cursor(x_connection_t xconn)
{
	xcb_font_t           font;
	xcb_cursor_t         cursor;
	xcb_gcontext_t       gc;
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;
	uint32_t             vmask;
	uint32_t             vlist[3];

	font = xcb_generate_id(xconn.conn);
	cookie = xcb_open_font_checked(xconn.conn, font, strlen("cursor"), "cursor");
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't load cursor font", xconn);

	cursor = xcb_generate_id(xconn.conn);
	cookie = xcb_create_glyph_cursor_checked(xconn.conn, cursor, font, font,
						 CURSOR_CHAR, CURSOR_CHAR + 1, 0, 0, 0,
						 UINT16_MAX, UINT16_MAX, UINT16_MAX);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create glyph cursor", xconn);

	vmask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
	vlist[0] = xconn.screen->black_pixel;
	vlist[1] = xconn.screen->white_pixel;
	vlist[2] = font;

	gc = xcb_generate_id(xconn.conn);
	cookie = xcb_create_gc_checked(xconn.conn, gc, xconn.screen->root, vmask, vlist);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't create graphics context", xconn);

	cookie = xcb_change_window_attributes_checked(xconn.conn, xconn.screen->root,
						      XCB_CW_CURSOR, &cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't change window attributes", xconn);

	cookie = xcb_free_gc_checked(xconn.conn, gc);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free graphics context", xconn);

	cookie = xcb_free_cursor_checked(xconn.conn, cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't free cursor", xconn);

	cookie = xcb_close_font_checked(xconn.conn, font);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) error("can't close font", xconn);
}

void hide_cursor(x_connection_t xconn)
{
	/*
	xcb_font_t            font;
	xcb_cursor_t          cursor;
	xcb_gcontext_t        gc;
	xcb_pixmap_t          pixmap;
	xcb_void_cookie_t     cookie;
	xcb_generic_error_t  *err;
	int                   cursor_glyph = 68;
	uint32_t              vmask;
	uint32_t              vlist[3];
	*/

	/*
	pixmap = xcb_generate_id(conn);
	cookie = xcb_create_pixmap_checked(conn, 1, pixmap, screen->root, 1, 1);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_create_pixmap_checked()", conn);

	cursor = xcb_generate_id(conn);
	cookie = xcb_create_cursor_checked(conn, cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 0, 0);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_create_cursor_checked()", conn);


	gc = xcb_generate_id(conn);
	cookie = xcb_create_gc_checked(conn, gc, screen->root, 0, NULL);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_create_gc_checked()", conn);

	vmask = XCB_CW_CURSOR;
	cookie = xcb_change_window_attributes_checked(conn, screen->root, vmask, &cursor);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_change_window_attributes_checked()", conn);

	// FIXME: arreglar los xcb_free_*(), asegurarse de liberar todo
	xcb_free_gc(conn, gc);
	xcb_free_cursor(conn, cursor);
	*/
}

int main(int argc, char *argv[])
{
	x_connection_t xconn;

	xconn = connect_x();
	//grab_pointer(xconn);
	//ungrab_pointer(xconn);
	//hide_cursor(xconn);
	restore_cursor(xconn);
	disconnect_x(xconn);

	return EXIT_SUCCESS;
}
