/*
A program to demonstrate the use of Unix pipes.
Copyright (C) 2021 Perry Kivolowitz 

This program is Free Software; you can redistribute it and/or modify it under 
the terms of the GNU General Public License as published by the Free Software 
Foundation; either version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for 
more details.

You should have received a copy of the GNU General Public License along with 
this program; if not, write to the 

Free Software Foundation, Inc., 
59 Temple Place - Suite 330, 
Boston, MA 02111-1307, USA.
*/

/*	This program will use `cat` as a loop-back. That is, it creates a pipe
	for sending data from this program to cat and a second pipe that allows
	the output of cat to flow back to this program.

	cat has no idea how it has been manipulated. One of the "Great Ideas"
	of Unix.
*/

#include <iostream>
#include <string>
#include <cassert>
#include <unistd.h>
#include <ncurses.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <sstream>

using namespace std;

WINDOW * tx_window = nullptr;
WINDOW * rx_window = nullptr;

static const int READ_SIDE = 0;
static const int WRITE_SIDE = 1;

void Refresh() {
	char * title = (char *) " Pipe Demo ";

	box(tx_window, 0, 0);
	mvwaddstr(tx_window, 0, 2, (char *) " TX ");
	mvwaddstr(tx_window, LINES - 1, 2, (char *) " ^C to exit ");
	box(rx_window, 0, 0);
	mvwaddstr(rx_window, 0, 2, (char *) " RX ");
	mvwaddstr(rx_window, 0, COLS / 2 - strlen(title) - 2, title);
	wrefresh(tx_window);
	wrefresh(rx_window);
}

void InitializeWindows() {
	if (!initscr())
		throw string("Curses failed to initialize");
	tx_window = subwin(stdscr, LINES, COLS / 2 - 1, 0, 0);
	rx_window = subwin(stdscr, LINES, COLS / 2, 0, COLS / 2);
	// Scrolling will not work within the windows unless these calls are made.
	scrollok(tx_window, true);
	scrollok(rx_window, true);
}

[[noreturn]] void InitializeChild(int *to_child, int *from_child)
{
	// Neither pointer may be NULL.
	assert(to_child);
	assert(from_child);

	/*	The child wants the write side of from_child to be stdout. Also
		it wants the read side of to_child to be stdin.
	*/
	dup2(to_child[READ_SIDE], STDIN_FILENO);
	close(to_child[READ_SIDE]);
	dup2(from_child[WRITE_SIDE], STDOUT_FILENO);
	close(from_child[WRITE_SIDE]);
	/*	Close the *other* sides of the pipes that the child will not
		be using. This is a NECESSARY bookkeeping step.
	*/
	close(to_child[WRITE_SIDE]);
	close(from_child[READ_SIDE]);
	/*	The child now writes STDOUT to the from_child pipe and
		reads STDIN from the to_child pipe. In short, the child
		will have NO IDEA it is connected to pipes.

		We're using standard `cat` as the child. cat will reflect 
		whatever we send to its stdin back out its stdout (which
		is our from_child[READ_SIDE]).
	*/
	execl("/bin/cat", "cat", "-", nullptr);
	throw string("Failed to exec to ./child.");
}

void InitializeParent(int * to_child, int * from_child) {
	// Neither pointer may be NULL.
	assert(to_child);
	assert(from_child);

	/*	The parent will not use the read side of to_child. And, it will
		not use the write side of from_child. Close these to ensure correct
		operation.
	*/
	close(to_child[READ_SIDE]);
	close(from_child[WRITE_SIDE]);
	/*	Set the read side of from_child up to do non-blocking reading. This
		will allow the parent to keep writing to the child when checking to
		see if the child sent anything back.
	*/
	int flags = fcntl(from_child[READ_SIDE], F_GETFL, 0);
	fcntl(from_child[READ_SIDE], F_SETFL, flags | O_NONBLOCK);
}

bool GetLine(int fd, string & buffer, bool & error) {
	bool retval = error = false;
	int read_return_value = 0;
	char c;
	static bool needs_a_reset = true;

	/*	needs_a_reset is switched on after reporting to the caller that we
		have a full line assembled. This is kind of elegant because the caller
		doesn't need to manage this itself - all self contained here.
	*/
	if (needs_a_reset) {
		buffer.erase();
		needs_a_reset = false;
	}

	/*	Remember the file descriptor coming from the child has been set to
		NON BLOCKING. read() returns -1 on errors. BUT if ERRNO is set to
		EAGAIN, there is no error - just no characters available.

		Once again, the following read() will return immediately if no 
		characters are available.
	*/
	while ((read_return_value = read(fd, &c, 1)) == 1) {
		buffer.push_back(c);
		if (c == '\n') {
			retval = needs_a_reset = true;
			break;
		}
	}
	if (read_return_value <= 0) {
		if (errno != EAGAIN)
			error = true;
	}
	return retval;
}

/*	This function scrolls the entire window down and adds a new line of
	text at the top line (accounting for the pretty box that gets drawn
	around the window).
*/
void AddLine(WINDOW * w, char * s) {
	wscrl(w, -1);
	mvwaddstr(w, 1, 1, s);
}

int main(int argc, char ** argv) {
	uint64_t message_counter = 0;
	string buffer;
	bool error = false;

	try {
		InitializeWindows();
		/*	Parent creates both pipes. One will be used to send data to
			the child. The other will be used by the child to send data
			back to the parent. Note that only the parent will be aware
			of the pipes.
		*/
		int to_child[2], from_child[2];
		if (pipe(to_child) < 0)
			throw string("Pipe to child failed to allocate.");
		if (pipe(from_child) < 0)
			throw string("Pipe from child failed to allocate.");
		pid_t pid = fork();
		if (!pid)
			InitializeChild(to_child, from_child);
		InitializeParent(to_child, from_child);
		while (true) {
			/*	Prepare the next line to be sent. 
			*/
			stringstream ss;
			ss << "Line: " << message_counter++ << endl;
			string s = ss.str();
			/* Send it and add to the TX window.
			*/
			write(to_child[WRITE_SIDE], s.c_str(), s.size());
			AddLine(tx_window, (char *) s.c_str());
			/*	IF a fully assembled line is available from the child back
				to us, add it to the RX window. After this, always check
				for an error return from the child.
			*/
			if (GetLine(from_child[READ_SIDE], buffer, error)) {
				AddLine(rx_window, (char *) buffer.c_str());
			}
			if (error)
				break;
			Refresh();
			this_thread::sleep_for(chrono::milliseconds(250));
		}
		endwin();
	}
	catch (const string s) {
		cerr << s << endl;
	}

	return 0;
}
