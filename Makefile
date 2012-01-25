all:
	gcc cbatticon.c libubat.c -o cbatticon `pkg-config --cflags --libs gtk+-2.0 libnotify ` -lm
