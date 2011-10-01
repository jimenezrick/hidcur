#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>

#define MOUSE_MASK XCB_EVENT_MASK_POINTER_MOTION | \
		   XCB_EVENT_MASK_BUTTON_PRESS   | \
		   XCB_EVENT_MASK_BUTTON_RELEASE

void error(const char *msg, xcb_connection_t *conn);
xcb_connection_t *connect(int *screen_pref);
xcb_screen_t *get_screen_display(xcb_connection_t *conn, int screen_num);
int grab_pointer(xcb_connection_t *conn, xcb_window_t win);

void error(const char *msg, xcb_connection_t *conn)
{
	fprintf(stderr, "Error: %s\n", msg);
	if (conn) xcb_disconnect(conn);

	exit(EXIT_FAILURE);
}

xcb_connection_t *connect(int *screen_pref)
{
	xcb_connection_t *conn;

	conn = xcb_connect(NULL, screen_pref);
	if (!conn) error("couldn't connect to the server", NULL);

	return conn;
}

xcb_screen_t *get_screen(xcb_connection_t *conn, int screen_num)
{
	xcb_screen_iterator_t it;
	xcb_screen_t         *screen = NULL;

	it = xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (; it.rem; xcb_screen_next(&it), screen_num--) {
		if (!screen_num) {
			screen = it.data;
			break;
		}
	}
	if (!screen) error("couldn't find the screen", conn);

	return screen;
}

int grab_pointer(xcb_connection_t *conn, xcb_window_t win)
{
	xcb_grab_pointer_cookie_t cookie;
	xcb_grab_pointer_reply_t *reply;
	xcb_generic_error_t      *err;

	// TODO: probar owner_events = true, confine_to = root_win
	cookie = xcb_grab_pointer(conn, false, win, MOUSE_MASK, XCB_GRAB_MODE_ASYNC,
				  XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
				  XCB_TIME_CURRENT_TIME);
	reply = xcb_grab_pointer_reply(conn, cookie, &err);
	if (err) error("couldn't grab the pointer", conn);
	if (reply->status != XCB_GRAB_STATUS_SUCCESS)
		return -1;

	return 0;
}






int main(int argc, char *argv[])
{
	xcb_connection_t     *conn;
	xcb_screen_t         *screen;
	int                   screen_num;


	xcb_font_t            font;
	xcb_cursor_t          cursor;
	xcb_gcontext_t        gc;
	xcb_pixmap_t          pixmap;
	xcb_void_cookie_t     cookie;
	xcb_generic_error_t  *err;
	int                   cursor_glyph = 68;
	uint32_t              vmask;
	uint32_t              vlist[3];

	//conn = connect(&screen_num);
	conn = xcb_connect(NULL, &screen_num);
	screen = get_screen(conn, screen_num);





	/*
	 * Hide the mouse
	 */
	pixmap = xcb_generate_id(conn);
	cookie = xcb_create_pixmap_checked(conn, 1, pixmap, screen->root, 1, 1);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_create_pixmap_checked()", conn);

	cursor = xcb_generate_id(conn);
	cookie = xcb_create_cursor_checked(conn, cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 0, 0);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_create_cursor_checked()", conn);

	xcb_grab_pointer_cookie_t c = xcb_grab_pointer_unchecked(conn, 0, screen->root, XCB_EVENT_MASK_NO_EVENT, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, screen->root, cursor, XCB_TIME_CURRENT_TIME);
	xcb_grab_pointer_reply_t *r = xcb_grab_pointer_reply(conn, c, NULL);
	printf("reply grab_pointer: %u %u\n", r->status, r->response_type);

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

	sleep(5);

	/*
	 * Show the mouse
	 */
	font = xcb_generate_id(conn);
	cookie = xcb_open_font_checked(conn, font, strlen("cursor"), "cursor");
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_open_font_checked()", conn);

	cursor = xcb_generate_id(conn);
	cookie = xcb_create_glyph_cursor_checked(conn, cursor, font, font, cursor_glyph, cursor_glyph + 1,
						 0, 0, 0, UINT16_MAX, UINT16_MAX, UINT16_MAX);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_create_glyph_cursor_checked()", conn);

	vmask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
	vlist[0] = screen->black_pixel;
	vlist[1] = screen->white_pixel;
	vlist[2] = font;

	gc = xcb_generate_id(conn);
	cookie = xcb_create_gc_checked(conn, gc, screen->root, vmask, vlist);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_create_gc_checked()", conn);

	vmask = XCB_CW_CURSOR;
	cookie = xcb_change_window_attributes_checked(conn, screen->root, vmask, &cursor);
	err = xcb_request_check(conn, cookie);
	if (err) error("xcb_change_window_attributes_checked()", conn);

	xcb_disconnect(conn);

	printf("Connects terminated\n");

	return EXIT_SUCCESS;
}
