CC=gcc
CFLAGS=-g -O0 -Wall -I/usr/local/include
LDFLAGS=-L/usr/local/lib -o
LIBS=-lm -lexpat -lsqlite3

## Output programs
PROGRAM1=xml2sql
PROGRAM2=server
PROGRAM3=mtgcli
PROGRAM4=spectator
PROGRAM5=admin
ALL_PROGRAMS=$(PROGRAM1) $(PROGRAM2) $(PROGRAM3) $(PROGRAM4) $(PROGRAM5)

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

all: $(ALL_PROGRAMS)

$(PROGRAM1): $(PROGRAM1_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM3): $(PROGRAM3_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM2): $(PROGRAM2_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM4): $(PROGRAM4_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

$(PROGRAM5): $(PROGRAM5_OBS)
	$(CC) $(LDFLAGS) $@ $^ $(LIBS)

clean:
	rm -f $(PROGRAM1_OBS) $(PROGRAM2_OBS) $(PROGRAM3_OBS) $(PROGRAM4_OBS) $(PROGRAM5_OBS) $(ALL_PROGRAMS)
