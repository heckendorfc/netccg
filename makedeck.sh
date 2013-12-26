#!/bin/sh

DID=`sqlite3 -line $1 'SELECT max(DeckID)+1 FROM Deck;' | awk '{ print $3 }'`
if [ -z $DID ]; then
	DID=1
fi

IFS='
'

echo "Generating deck number $DID"

for x in `cat $2`
do
	CARD="`echo "$x" | sed 's/^[0-9]*[ 	 ]*//' | sed 's/^Aether/AEther/'`"
	echo "Inserting $CARD";
	yes "INSERT INTO Library (CardID) SELECT ID FROM BasicCard WHERE Name=\"$CARD\"; INSERT INTO Deck (DeckID,LibraryID) VALUES ($DID,last_insert_rowid());" | head -n `echo $x | awk '{print $1}'` | sqlite3 $1
	#yes "insert into Deck (DeckID,LibraryID) select $DID,ID from BasicCard where Name=\"$CARD\";" | head -n `echo $x | awk '{print $1}'` | sqlite3 $1
done
