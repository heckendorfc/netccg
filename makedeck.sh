#!/bin/sh

DID=`sqlite3 -line $1 'select count(DeckID)+1 from Deck' | awk '{ print $3 }'`

IFS='
'

echo "Generating deck number $DID"

for x in `cat $2`
do
	CARD="`echo "$x" | sed 's/.*	//'`"
	yes "insert into Library (CardID) select ID from BasicCard where Name=\"$CARD\";" | head -n `echo $x | awk '{print $1}'` | sqlite3 $1
	yes "insert into Deck (DeckID,CardID) select $DID,ID from BasicCard where Name=\"$CARD\";" | head -n `echo $x | awk '{print $1}'` | sqlite3 $1
done
