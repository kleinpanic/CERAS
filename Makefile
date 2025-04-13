CC = gcc
PKG_CONFIG = pkg-config
CFLAGS = -Wall -O2 -pthread `$(PKG_CONFIG) --cflags gtk+-3.0 x11 alsa`
LDFLAGS = -lX11 -lXrandr -lasound \
          -lavformat -lavcodec -lavutil -lswscale -lavdevice -lswresample \
          `$(PKG_CONFIG) --libs gtk+-3.0`

SRCDIR = src
OBJDIR = obj
INCDIR = include

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))
TARGET = ceras

all: $(TARGET)

$(TARGET): $(OBJDIR) $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) -c -o $@ $< -I$(INCDIR) $(CFLAGS)

clean:
	rm -rf $(OBJDIR) $(TARGET)

