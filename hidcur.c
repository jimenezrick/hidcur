/*
 *                                                      ()-()
 * hidcur - Utility for hiding the mouse cursor in X11   \"/
 *                                                        `
 * This program is licensed under WTFPL, see COPYING for details.
 *
 * Ricardo Catalinas Jiménez <jimenezrick@gmail.com>
 */

#define _DEFAULT_SOURCE

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <xcb/xcb.h>

#define MOUSE_MASK XCB_EVENT_MASK_POINTER_MOTION | \
		   XCB_EVENT_MASK_BUTTON_PRESS

typedef struct {
	xcb_connection_t *conn;
	xcb_screen_t     *screen;
	int               screen_num;
} x_connection_t;

typedef struct {
	int16_t x, y;
} pointer_info_t;

static const char *PROG_NAME;

static void perror_exit(const char *msg);
static void xerror_exit(x_connection_t xconn, const char *msg);
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
static int watch_signal(int signum);
static int wait_event(x_connection_t *xconn, int interval);
static uint8_t poll_event(x_connection_t xconn);
static void drop_pending_events(x_connection_t xconn);
static bool hide_cursor(x_connection_t xconn, xcb_window_t *win);
static void show_cursor(x_connection_t xconn, xcb_window_t win, bool destroy_win);
static void usage(void);

static void perror_exit(const char *msg)
{
	fprintf(stderr, "%s: %s: %s\n", PROG_NAME, msg, strerror(errno));
	exit(EXIT_FAILURE);
}

static void xerror_exit(x_connection_t xconn, const char *msg)
{
	disconnect_x(xconn);
	fprintf(stderr, "%s: %s\n", PROG_NAME, msg);
	exit(EXIT_FAILURE);
}

static x_connection_t connect_x(void)
{
	x_connection_t xconn;

	xconn.conn = xcb_connect(NULL, &xconn.screen_num);
	if (xcb_connection_has_error(xconn.conn))
		xerror_exit(xconn, "can't connect to X server");

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
	int                   screen_num = xconn->screen_num;

	xconn->screen = NULL;
	it = xcb_setup_roots_iterator(xcb_get_setup(xconn->conn));
	for (; it.rem; xcb_screen_next(&it), screen_num--) {
		if (!screen_num) {
			xconn->screen = it.data;
			break;
		}
	}
	if (!xconn->screen) xerror_exit(*xconn, "can't find screen");
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
	if (err) xerror_exit(xconn, "can't grab pointer");

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
	if (err) xerror_exit(xconn, "can't ungrab pointer");
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
	if (err) xerror_exit(xconn, "can't create pixmap");

	gc = xcb_generate_id(xconn.conn);
	cookie = xcb_create_gc_checked(xconn.conn, gc, pixmap,
				       XCB_GC_FUNCTION, &fun);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't create graphics context");

	cookie = xcb_poly_fill_rectangle_checked(xconn.conn, pixmap, gc, 1, &rect);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't fill rectangle");

	cursor = xcb_generate_id(xconn.conn);
	cookie = xcb_create_cursor_checked(xconn.conn, cursor, pixmap, pixmap,
					   0, 0, 0, 0, 0, 0, 0, 0);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't create cursor");

	cookie = xcb_free_gc_checked(xconn.conn, gc);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't free graphics context");

	cookie = xcb_free_pixmap_checked(xconn.conn, pixmap);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't free pixmap");

	return cursor;
}

static void free_cursor(x_connection_t xconn, xcb_cursor_t cursor)
{
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;

	cookie = xcb_free_cursor_checked(xconn.conn, cursor);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't free cursor");
}

static xcb_window_t get_input_focus(x_connection_t xconn)
{
	xcb_get_input_focus_cookie_t cookie;
	xcb_get_input_focus_reply_t *reply;
	xcb_generic_error_t         *err;
	xcb_window_t                 focus;

	cookie = xcb_get_input_focus(xconn.conn);
	reply = xcb_get_input_focus_reply(xconn.conn, cookie, &err);
	if (err) xerror_exit(xconn, "can't get input focus");

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

	cookie = xcb_query_pointer(xconn.conn, xconn.screen->root);
	reply = xcb_query_pointer_reply(xconn.conn, cookie, &err);
	if (err) xerror_exit(xconn, "can't query pointer");

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
	uint32_t             mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY |
				    XCB_EVENT_MASK_LEAVE_WINDOW;

	win = xcb_generate_id(xconn.conn);
	cookie = xcb_create_window_checked(xconn.conn, 0, win, parent_win,
					   0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
					   XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't create window");

	cookie = xcb_map_window_checked(xconn.conn, win);
	err = xcb_request_check(xconn.conn, cookie);
	if (err) xerror_exit(xconn, "can't map window");

	return win;
}

static void destroy_window(x_connection_t xconn, xcb_window_t win)
{
	xcb_void_cookie_t    cookie;
	xcb_generic_error_t *err;

	/*
	 * It is possible the window is already destroyed when the notification
	 * is received, thus drop the error to drain the internal queue and just
	 * ignore it if this happens.
	 */
	cookie = xcb_destroy_window_checked(xconn.conn, win);
	err = xcb_request_check(xconn.conn, cookie);
	free(err);
}

static void wait_pointer_idle(x_connection_t xconn, int interval)
{
	pointer_info_t old_info, info;

	for (;;) {
		old_info = query_pointer(xconn);
		for (;;) {
			if (wait_event(NULL, interval) == -1)
				break;

			info = query_pointer(xconn);
			if (info.x == old_info.x && info.y == old_info.y)
				return;
			old_info = info;
		}
	}
}

static int watch_signal(int signum)
{
	static int sfd = -1;
	sigset_t   mask;

	if (sfd == -1) {
		sigemptyset(&mask);
		sigaddset(&mask, signum);
		if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
			perror_exit("sigprocmask");
		if ((sfd = signalfd(-1, &mask, 0)) == -1)
			perror_exit("signalfd");
	}

	return sfd;
}

static int wait_event(x_connection_t *xconn, int interval)
{
	static bool   enabled = true;
	int           sfd = watch_signal(SIGUSR1);
	struct pollfd fds[2] = {[0] = {sfd, POLLIN}};
	int           nfds = 1;
	int           timeout = -1;

	if (enabled) {
		if (xconn != NULL) {
			fds[1] = (struct pollfd) {xcb_get_file_descriptor(xconn->conn), POLLIN};
			nfds = 2;
		}
		timeout = interval > 0 ? interval * 1000 : -1;
	}

	for (;;) {
		uint8_t ev;
		struct signalfd_siginfo si;

		switch (poll(fds, nfds, timeout)) {
		case -1:
			perror_exit("poll");
		case 0:
			return 0;
		default:
			for (int i = 0; i < nfds; i++) {
				if (fds[i].revents & (POLLERR | POLLNVAL)) {
					fprintf(stderr, "%s: %s\n", PROG_NAME, "poll: an error occurred");
					exit(EXIT_FAILURE);
				} else if (fds[i].revents & POLLIN) {
					switch (i) {
					case 0:
						enabled ^= true;
						fprintf(stderr, "%s: %s\n", PROG_NAME, enabled ? "enabled" : "disabled");

						if (read(sfd, &si, sizeof(struct signalfd_siginfo)) == -1)
							perror_exit("read");

						return -1;
					case 1:
						if ((ev = poll_event(*xconn)) != 0)
							return ev;
					}
				}
			}
		}
	}

	return 0;
}

static uint8_t poll_event(x_connection_t xconn)
{
	xcb_generic_event_t *event;
	uint8_t              response_type;

	while ((event = xcb_poll_for_event(xconn.conn)) != NULL) {
		response_type = event->response_type;
		free(event);

		if (response_type == XCB_MOTION_NOTIFY ||
		    response_type == XCB_BUTTON_PRESS  ||
		    response_type == XCB_LEAVE_NOTIFY  ||
		    response_type == XCB_DESTROY_NOTIFY)
			return response_type;
	}

	if (xcb_connection_has_error(xconn.conn))
		xerror_exit(xconn, "X server shut down connection");

	return 0;
}

static void drop_pending_events(x_connection_t xconn)
{
	xcb_generic_event_t *event;

	while ((event = xcb_poll_for_event(xconn.conn)) != NULL)
		free(event);
	if (xcb_connection_has_error(xconn.conn))
		xerror_exit(xconn, "X server shut down connection");
}

static bool hide_cursor(x_connection_t xconn, xcb_window_t *win)
{
	xcb_window_t focus_win;

	focus_win = get_input_focus(xconn);
	if (focus_win == xconn.screen->root           ||
	    focus_win == XCB_INPUT_FOCUS_POINTER_ROOT ||
	    focus_win == XCB_NONE)
		return false;

	*win = create_input_window(xconn, focus_win);
	if (!grab_pointer(xconn, *win, create_invisible_cursor(xconn)))
		return false;

	return true;
}

static void show_cursor(x_connection_t xconn, xcb_window_t win, bool destroy_win)
{
	ungrab_pointer(xconn);
	if (destroy_win)
		destroy_window(xconn, win);
	drop_pending_events(xconn);
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s [<interval> | -h]\n", PROG_NAME);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	x_connection_t xconn;
	xcb_window_t   win;
	int            interval = DEFAULT_INTERVAL;
	uint8_t        event;

	PROG_NAME = argv[0];
	if ((argc == 2 && !strcmp(argv[1], "-h")) || argc > 2)
		usage();
	else if (argc == 2 && sscanf(argv[1], "%d", &interval) != 1)
		usage();

	xconn = connect_x();
	for (;;) {
		wait_pointer_idle(xconn, interval);
		if (!hide_cursor(xconn, &win))
			continue;
		event = wait_event(&xconn, 0);
		show_cursor(xconn, win, event != XCB_DESTROY_NOTIFY);
	}

	return EXIT_SUCCESS;
}
