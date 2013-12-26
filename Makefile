CC=gcc
CFLAGS=-g -O0 -Wall -I/usr/local/include
LDFLAGS=-L/usr/local/lib -o
LIBS=-lm -lsqlite3 -pthread

## Output programs
PROGRAM1=xml2sql
PROGRAM2=server
PROGRAM3=mtgcli
PROGRAM4=spectator
PROGRAM5=admin
PROGRAM6=mtgcurses
PROGRAM7=mtgdeck
ALL_PROGRAMS=$(PROGRAM1) $(PROGRAM2) $(PROGRAM5) $(PROGRAM6) $(PROGRAM7)

## Output .o files
PROGRAM1_OBS=\
			src/list.o\
			src/parsexml.o

PROGRAM2_OBS=\
			src/list.o\
			src/mtg.o\
			src/common.o\
			src/server.o

PROGRAM3_OBS=\
			src/mtgcli.o\
			src/common.o

PROGRAM4_OBS=\
			src/common.o\
			src/lmain.o

PROGRAM5_OBS=\
			src/common.o\
			src/amain.o

PROGRAM6_OBS=\
			src/mtgcurses.o\
			src/cursesui.o

PROGRAM7_OBS=\
			src/cursesui.o\
			src/mtgdeck.o

all: $(ALL_PROGRAMS)

$(PROGRAM1): $(PROGRAM1_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS) -lexpat

#$(PROGRAM3): $(PROGRAM3_OBS)
	#$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM2): $(PROGRAM2_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM4): $(PROGRAM4_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM5): $(PROGRAM5_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM6): $(PROGRAM6_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS) -lcurses

$(PROGRAM7): $(PROGRAM7_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS) -lcurses

clean:
	rm -f $(PROGRAM1_OBS) $(PROGRAM2_OBS) $(PROGRAM3_OBS) $(PROGRAM4_OBS) $(PROGRAM5_OBS) $(PROGRAM6_OBS) $(PROGRAM7_OBS) $(ALL_PROGRAMS)
