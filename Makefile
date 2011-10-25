all:
	gcc cbatticon.c -o cbatticon `pkg-config --cflags --libs gtk+-2.0 libnotify libudev` -lm
